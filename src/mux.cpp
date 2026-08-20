// Copyright 2026 Eric Schneider
// Licensed under the Apache License, Version 2.0

#include "mux.hpp"

#include <csignal>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "misbklv/message.hpp"
#include "misbklv/stream.hpp"

#include "error_description.hpp"
#include "file_identity.hpp"
#include "mp4_box_scan.hpp"
#include "mp4_metadata_extractor.hpp"
#include "parrot_proto.hpp"
#include "parrot_v3.hpp"
#include "session_metadata.hpp"
#include "telemetry.hpp"

namespace parrot_to_klv {
namespace {

/// Set only from `request_mux_interrupt()` (async-signal-safe: a plain write
/// to a `sig_atomic_t`, nothing else). Checked once per sample in
/// `convert_all()`'s loop, which is the only place a long conversion actually
/// spends time.
volatile std::sig_atomic_t g_interrupt_requested = 0;

/// True for the sample entry formats that hold H.264.
///
/// `avc3` differs from `avc1` only in carrying parameter sets in-band, which is
/// a decoder's problem and not ours — both are `video/x-h264` to the backend.
bool is_h264(uint32_t sample_entry_format) {
    return sample_entry_format == fourcc("avc1") || sample_entry_format == fourcc("avc3");
}

/// The first video track's sample entry format, or 0 if there is no video.
///
/// Deliberately reports the *format* rather than a yes/no, so a conversion can
/// name the codec it declined to stamp instead of just declining.
uint32_t video_sample_entry(FILE* file) {
    for (const auto& track : scan_mp4_tracks(file)) {
        if (track.handler_type == kHandlerVide) return track.sample_entry_format;
    }
    return 0;
}

/// Decode every metadata sample of one format and hand each mapped packet to
/// `emit`, along with the sample's presentation time.
///
/// Templated over the frame type for the same reason the CLI's summary loop is:
/// v3 and proto differ in what a frame *is*, not in how a track is walked.
template <typename Parse, typename Normalize, typename Emit>
void convert_all(Mp4MetadataExtractor& extractor, const SessionMetadata& session,
                 Parse parse, Normalize normalize, Emit emit, MuxReport& report,
                 const TransformOptions& options) {
    // The first sample's clock is the origin the session media date anchors to,
    // so it has to be read before the loop rather than discovered inside it.
    // Left empty (never defaulted to zero) if the origin sample's own
    // extraction or parse fails -- anchor_to_session treats an unknown origin
    // as "do not anchor" rather than assuming a zero clock base.
    std::optional<uint64_t> first_monotonic_us;
    if (auto first = extractor.extract_sample(0)) {
        std::string ignored;
        if (auto frame = parse(first->data, ignored)) {
            first_monotonic_us = normalize(*frame).monotonic_us;
        }
    }

    for (uint32_t i = 0; i < extractor.sample_count() && report.error.empty(); ++i) {
        if (g_interrupt_requested) {
            // Turn the signal into a normal, stack-unwinding return: report an
            // error and stop, same as any other mid-conversion failure, so the
            // caller skips close() and the sink's destructor discards the file.
            report.error = "conversion interrupted";
            break;
        }
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
        auto klv = to_0601(snapshot, &session, options);
        if (klv.packet.empty()) {
            ++report.frames_skipped;
            continue;
        }
        report.coverage.accumulate(klv.coverage);
        emit(klv.packet, sample->timestamp_us);
    }
}

}  // namespace

void request_mux_interrupt() { g_interrupt_requested = 1; }
bool is_mux_interrupted() { return g_interrupt_requested != 0; }

MuxReport mux_to_ts(const std::string& input, const std::string& output,
                    const TransformOptions& options) {
    MuxReport report;

    if (paths_alias(input, output)) {
        report.error = "input and output refer to the same file";
        return report;
    }

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
        // Both of these seek from the start themselves, so they can share the
        // handle in either order.
        report.video_sample_entry = video_sample_entry(handle);
        std::fclose(handle);
    }
    report.st0604_generated = is_h264(report.video_sample_entry);

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
    // Only ask for it on H.264, though. Generation is H.264-only upstream, and
    // asking on any other codec does not degrade — it *refuses the session*, so
    // an H.265 recording would fail to convert at all. Preserve carries such a
    // source through unstamped instead, which is the whole of fork 7
    // (ADR 0007): degrade the guarantee only where it cannot be met. Anything
    // unrecognized takes the same path, so a codec we have never seen converts
    // rather than erroring on a guess.
    //
    // Preflight output directory before constructing the GStreamer sink, so an
    // unwritable or nonexistent destination fails with a clear, specific message
    // rather than the opaque "media pipeline failed" from deep in GStreamer.
    const std::filesystem::path out_path(output);
    if (std::error_code ec; std::filesystem::is_directory(out_path, ec)) {
        report.error = "cannot write " + output + ": path is a directory";
        return report;
    }
    std::filesystem::path parent = out_path.parent_path();
    if (parent.empty()) {
        parent = ".";
    }
    std::error_code ec;
    const auto parent_status = std::filesystem::status(parent, ec);
    if (ec || !std::filesystem::exists(parent_status)) {
        report.error = "cannot write " + output + ": parent directory does not exist";
        return report;
    }
    if (!std::filesystem::is_directory(parent_status)) {
        report.error = "cannot write " + output + ": parent path is not a directory";
        return report;
    }

    misbklv::KlvSink sink("file:" + output, /*realtime=*/false, input,
                          report.st0604_generated ? misbklv::Sei0604::Generate
                                                  : misbklv::Sei0604::Preserve);

    const uint32_t total_samples = extractor->sample_count();
    uint32_t last_progress_pct = 0;

    auto emit = [&](const std::vector<uint8_t>& packet, uint64_t timestamp_us) {
        auto message = misbklv::Message::parse(
            std::as_bytes(std::span<const uint8_t>(packet)));
        if (!message) {
            report.error = "encoded packet did not parse back: " +
                           detail::describe_misbklv_error(message.error());
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
                           detail::describe_misbklv_error(pushed.error());
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
            options);
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
            options);
    }

    // Clear the progress line
    if (report.frames_emitted > 0 && report.error.empty()) {
        std::cerr << "                    \r" << std::flush;  // Clear the "Converting..." line
    }

    // Every failure mode above this point — a loop error, an interruption, zero
    // convertible samples — must be decided *before* close(), not after: close()
    // finishing successfully is what tells the sink to keep the file it wrote
    // (upstream clears its "created by us" flag on a clean finish), so calling
    // it after the fact and only checking `report.error` for a message would
    // leave a complete-looking but wrong output on disk. Skipping close()
    // entirely leaves the sink unfinished, and its destructor discards the file
    // it created — the same path a mid-loop emit failure already took.
    if (report.error.empty() && report.frames_emitted == 0) {
        report.error = "no metadata sample converted to a KLV packet";
    }
    // One more check right here: the per-sample loop only sees a signal
    // between samples, so one arriving during the finish flush below — after
    // the last sample, before close() runs — would otherwise reach close()
    // unnoticed and keep a file that is complete but for whatever the caller
    // wanted to stop for.
    if (report.error.empty() && g_interrupt_requested) {
        report.error = "conversion interrupted";
    }
    if (report.error.empty()) {
        if (auto closed = sink.close(); !closed) {
            report.error = "could not finish the output stream: " +
                           detail::describe_misbklv_error(closed.error());
        }
    }
    return report;
}

}  // namespace parrot_to_klv
