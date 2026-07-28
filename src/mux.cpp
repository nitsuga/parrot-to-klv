// Copyright 2026 Eric Schneider
// Licensed under the Apache License, Version 2.0

#include "mux.hpp"

#include <cstdint>
#include <cstdio>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "misbklv/message.hpp"
#include "misbklv/stream.hpp"

#include "mp4_metadata_extractor.hpp"
#include "parrot_proto.hpp"
#include "parrot_v3.hpp"
#include "session_metadata.hpp"
#include "telemetry.hpp"

namespace parrot_to_klv {
namespace {

std::string describe(misbklv::Error error) {
    switch (error) {
        case misbklv::Error::Truncated:        return "packet truncated";
        case misbklv::Error::BadLength:        return "bad BER length";
        case misbklv::Error::UnknownTag:       return "unknown tag";
        case misbklv::Error::OutOfRange:       return "value out of range";
        case misbklv::Error::MissingMandatory: return "mandatory item missing";
        case misbklv::Error::RangeError:       return "value not encodable";
        case misbklv::Error::Backend:          return "media pipeline failed";
        case misbklv::Error::Unsupported:      return "unsupported by the backend";
    }
    return "unknown error";
}

/// Decode every metadata sample of one format and hand each mapped packet to
/// `emit`, along with the sample's presentation time.
///
/// Templated over the frame type for the same reason the CLI's summary loop is:
/// v3 and proto differ in what a frame *is*, not in how a track is walked.
template <typename Parse, typename Normalize, typename Emit>
void convert_all(Mp4MetadataExtractor& extractor, const SessionMetadata& session,
                 Parse parse, Normalize normalize, Emit emit, MuxReport& report,
                 bool clamp_attitude, bool force_emit_attitude) {
    // The first sample's clock is the origin the session media date anchors to,
    // so it has to be read before the loop rather than discovered inside it.
    uint64_t first_monotonic_us = 0;
    if (auto first = extractor.extract_sample(0)) {
        std::string ignored;
        if (auto frame = parse(first->data, ignored)) {
            first_monotonic_us = normalize(*frame).monotonic_us;
        }
    }

    for (uint32_t i = 0; i < extractor.sample_count() && report.error.empty(); ++i) {
        auto sample = extractor.extract_sample(i);
        if (!sample) {
            ++report.frames_skipped;
            continue;
        }
        std::string error;
        auto frame = parse(sample->data, error);
        if (!frame) {
            ++report.frames_skipped;
            continue;
        }
        auto snapshot = normalize(*frame);
        anchor_to_session(snapshot, session, first_monotonic_us);
        auto klv = to_0601(snapshot, &session, clamp_attitude, force_emit_attitude);
        if (klv.packet.empty()) {
            ++report.frames_skipped;
            continue;
        }
        if (report.frames_emitted == 0) report.coverage = std::move(klv.coverage);
        emit(klv.packet, sample->timestamp_us);
    }
}

}  // namespace

MuxReport mux_to_ts(const std::string& input, const std::string& output,
                    bool clamp_attitude, bool force_emit_attitude) {
    MuxReport report;

    auto extractor = Mp4MetadataExtractor::open(input);
    if (!extractor) {
        report.error = "no Parrot metadata track in " + input;
        return report;
    }
    const auto& info = extractor->track_info();
    if (info.format != ParrotFormat::V3 && info.format != ParrotFormat::Proto) {
        report.error = "the " + to_string(info.format) +
                       " format is not converted (see ROADMAP § Scope)";
        return report;
    }
    report.samples_seen = extractor->sample_count();

    SessionMetadata session;
    if (FILE* handle = std::fopen(input.c_str(), "rb")) {
        session = parse_session_metadata(handle);
        std::fclose(handle);
    }

    // The sink owns both branches: it re-muxes `input`'s video elementary stream
    // alongside the KLV we push — never decoding it, but rewriting its timing
    // NALs to carry ST 0604 (ADR 0006).
    //
    // Sei0604::Generate is required as of libmisbklv's ADR 0024: SEI generation
    // used to be implied by passing a video source, and is now opt-in, so that
    // consumers who only want their video carried across get it back untouched.
    // Without this argument the output has no ST 0604 and the downstream client
    // loses frame-accurate time. Under Generate our KLV is the stream's one
    // timestamp authority — any ST 0604 the source carried is replaced, which
    // for Parrot MP4s is moot (they carry none) but matters if an input ever
    // arrives already stamped.
    //
    // A failure to open the source surfaces on the first emit(), which is where
    // the error below comes from. Nothing here cleans up a half-written output:
    // libmisbklv guarantees that a failed insert session leaves no file it
    // created, whether the failure lands in the open, a push, or the finish
    // (its ADR 0022).
    misbklv::KlvSink sink("file:" + output, /*realtime=*/false, input,
                          misbklv::Sei0604::Generate);

    const uint32_t total_samples = extractor->sample_count();
    uint32_t last_progress_pct = 0;

    auto emit = [&](const std::vector<uint8_t>& packet, uint64_t timestamp_us) {
        auto message = misbklv::Message::parse(
            std::as_bytes(std::span<const uint8_t>(packet)));
        if (!message) {
            report.error = "encoded packet did not parse back: " +
                           describe(message.error());
            return;
        }
        // libmisbklv times the video branch from the start of the source file,
        // and demands the KLV land on that same timeline. The metadata track's
        // own sample times are already on it — both tracks are timed from the
        // start of the same MP4 — so they convert straight to nanoseconds.
        message->set_pts(static_cast<int64_t>(timestamp_us) * 1000);
        if (auto pushed = sink.emit(*message); !pushed) {
            report.error = "could not mux frame " +
                           std::to_string(report.frames_emitted) + ": " +
                           describe(pushed.error());
            return;
        }
        ++report.frames_emitted;

        // Progress indicator: print every 10%
        if (total_samples > 0) {
            uint32_t progress_pct = (report.frames_emitted * 100) / total_samples;
            if (progress_pct >= last_progress_pct + 10 && progress_pct <= 100) {
                std::cerr << "Converting... " << progress_pct << "%\r" << std::flush;
                last_progress_pct = progress_pct;
            }
        }
    };

    if (info.format == ParrotFormat::V3) {
        convert_all(
            *extractor, session,
            [](std::span<const uint8_t> data,
               std::string& error) -> std::optional<ParrotV3Frame> {
                ParrotV3Error err{};
                auto frame = parse_v3(data, &err);
                if (!frame) error = to_string(err);
                return frame;
            },
            [](const ParrotV3Frame& f) { return from_v3(f); }, emit, report,
            clamp_attitude, force_emit_attitude);
    } else {
        convert_all(
            *extractor, session,
            [](std::span<const uint8_t> data,
               std::string& error) -> std::optional<ParrotProtoFrame> {
                ParrotProtoError err{};
                auto frame = parse_proto(data, &err);
                if (!frame) error = to_string(err);
                return frame;
            },
            [](const ParrotProtoFrame& f) { return from_proto(f); }, emit, report,
            clamp_attitude, force_emit_attitude);
    }

    // Clear the progress line
    if (report.frames_emitted > 0 && report.error.empty()) {
        std::cerr << "                    \r" << std::flush;  // Clear the "Converting..." line
    }

    if (auto closed = sink.close(); !closed && report.error.empty()) {
        report.error = "could not finish the output stream: " + describe(closed.error());
    }
    if (report.error.empty() && report.frames_emitted == 0) {
        report.error = "no metadata sample converted to a KLV packet";
    }
    return report;
}

}  // namespace parrot_to_klv
