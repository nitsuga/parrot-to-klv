// Copyright 2026 Eric Schneider
// Licensed under the Apache License, Version 2.0

#include "live/file_replay.hpp"

#include <chrono>
#include <cstdio>
#include <cstdint>
#include <span>
#include <string>
#include <thread>
#include <vector>

#include "misbklv/backend.hpp"
#include "misbklv/message.hpp"
#include "misbklv/stream.hpp"

#include "mp4_metadata_extractor.hpp"
#include "mux.hpp"
#include "parrot_proto.hpp"
#include "parrot_v3.hpp"
#include "session_metadata.hpp"
#include "telemetry.hpp"
#include "transform.hpp"

namespace parrot_to_klv {
namespace {

bool starts_with(const std::string& s, const std::string& prefix) {
    return s.rfind(prefix, 0) == 0;
}

void interruptible_sleep_for(std::chrono::microseconds delta) {
    auto deadline = std::chrono::steady_clock::now() + delta;
    while (!is_mux_interrupted()) {
        auto now = std::chrono::steady_clock::now();
        if (now >= deadline) break;
        auto remaining = deadline - now;
        // Sleep at most 10 ms so SIGINT is noticed promptly even on long gaps.
        auto chunk = remaining > std::chrono::milliseconds(10)
                         ? std::chrono::milliseconds(10)
                         : remaining;
        std::this_thread::sleep_for(chunk);
    }
}

misbklv::Result<std::monostate> do_one_pass(
    Mp4MetadataExtractor& extractor,
    const SessionMetadata& session,
    const std::optional<uint64_t>& first_monotonic_us,
    uint64_t origin_us,
    misbklv::KlvSink& sink,
    const TransformOptions& opts,
    ParrotFormat format) {
    const uint32_t count = extractor.sample_count();
    // Track previous timestamp for pacing.
    bool has_prev = false;
    uint64_t prev_ts = origin_us;

    for (uint32_t i = 0; i < count; ++i) {
        if (is_mux_interrupted()) break;
        auto sample_opt = extractor.extract_sample(i);
        if (!sample_opt) continue;
        const auto& sample = *sample_opt;
        // Paced emission: sleep delta since previous sample.
        if (has_prev) {
            if (sample.timestamp_us > prev_ts) {
                auto delta_us = sample.timestamp_us - prev_ts;
                // Cap single delta to avoid multi-second stall on sparse recordings
                // blocking interrupt for too long — interruptible sleep already chunks.
                interruptible_sleep_for(std::chrono::microseconds(delta_us));
                if (is_mux_interrupted()) break;
            } else if (sample.timestamp_us < prev_ts) {
                // Out-of-order timestamp: don't sleep backwards.
            }
        }
        prev_ts = sample.timestamp_us;
        has_prev = true;

        std::optional<TelemetrySnapshot> snapshot_opt;
        if (format == ParrotFormat::V3) {
            ParrotV3Error err{};
            auto frame = parse_v3(std::span<const uint8_t>(sample.data.data(), sample.data.size()), &err);
            if (!frame) continue;
            snapshot_opt = from_v3(*frame);
        } else if (format == ParrotFormat::Proto) {
            ParrotProtoError err{};
            auto frame = parse_proto(std::span<const uint8_t>(sample.data.data(), sample.data.size()), &err);
            if (!frame) continue;
            snapshot_opt = from_proto(*frame);
        } else {
            // Unsupported format (v1/v2) — should have been rejected before sink creation.
            continue;
        }
        if (!snapshot_opt) continue;
        TelemetrySnapshot snap = *snapshot_opt;
        anchor_to_session(snap, session, first_monotonic_us);
        auto klv = to_0601(snap, &session, opts);
        if (klv.packet.empty()) continue;
        auto msg_res = misbklv::Message::parse(std::as_bytes(std::span<const uint8_t>(klv.packet)));
        if (!msg_res) continue;
        int64_t pts_ns = static_cast<int64_t>((sample.timestamp_us - origin_us) * 1000ULL);
        // Guard against underflow if origin > timestamp (out-of-order): clamp to 0.
        if (sample.timestamp_us < origin_us) pts_ns = 0;
        msg_res->set_pts(pts_ns);
        auto emit_res = sink.emit(*msg_res);
        if (!emit_res) {
            return emit_res;
        }
        if (is_mux_interrupted()) break;
    }
    return misbklv::Result<std::monostate>::ok(std::monostate{});
}

}  // namespace

misbklv::Result<std::monostate> replay_file_to_live(const FileReplayConfig& cfg) {
    if (cfg.input_path.empty() || cfg.output_sink.empty()) {
        return misbklv::Result<std::monostate>::err(misbklv::Error::Unsupported);
    }
    // Validate sink scheme.
    bool is_udp = starts_with(cfg.output_sink, "udp:");
    bool is_srt = starts_with(cfg.output_sink, "srt:");
    bool is_file = starts_with(cfg.output_sink, "file:");
    if (!is_udp && !is_srt && !is_file) {
        return misbklv::Result<std::monostate>::err(misbklv::Error::Unsupported);
    }

    auto extractor_opt = Mp4MetadataExtractor::open(cfg.input_path);
    if (!extractor_opt) {
        return misbklv::Result<std::monostate>::err(misbklv::Error::Backend);
    }
    Mp4MetadataExtractor& extractor = *extractor_opt;
    const auto& info = extractor.track_info();
    if (info.format != ParrotFormat::V3 && info.format != ParrotFormat::Proto) {
        return misbklv::Result<std::monostate>::err(misbklv::Error::Unsupported);
    }
    if (extractor.sample_count() == 0) {
        return misbklv::Result<std::monostate>::err(misbklv::Error::Backend);
    }

    // Session metadata.
    SessionMetadata session;
    if (cfg.session.has_value()) {
        session = *cfg.session;
    } else {
        if (FILE* h = std::fopen(cfg.input_path.c_str(), "rb")) {
            session = parse_session_metadata(h);
            std::fclose(h);
        }
    }

    // First monotonic for anchor (v3 only, but compute generically).
    std::optional<uint64_t> first_monotonic_us;
    if (auto first = extractor.extract_sample(0)) {
        if (info.format == ParrotFormat::V3) {
            ParrotV3Error err{};
            if (auto f = parse_v3(std::span<const uint8_t>(first->data.data(), first->data.size()), &err)) {
                first_monotonic_us = from_v3(*f).monotonic_us;
            }
        } else if (info.format == ParrotFormat::Proto) {
            ParrotProtoError err{};
            if (auto f = parse_proto(std::span<const uint8_t>(first->data.data(), first->data.size()), &err)) {
                first_monotonic_us = from_proto(*f).monotonic_us;
            }
        }
    }

    // Origin: earliest presentation time (min timestamp_us) for zero-based PTS.
    uint64_t origin_us = 0;
    bool origin_set = false;
    for (uint32_t i = 0; i < extractor.sample_count(); ++i) {
        auto s = extractor.extract_sample(i);
        if (!s) continue;
        if (!origin_set || s->timestamp_us < origin_us) {
            origin_us = s->timestamp_us;
            origin_set = true;
        }
    }
    if (!origin_set) origin_us = 0;

    auto build_config = [&]() {
        misbklv::InsertConfig icfg;
        icfg.sink = cfg.output_sink;
        icfg.realtime = true;
        icfg.video_source = "file:" + cfg.input_path;
        icfg.sei_0604 = misbklv::Sei0604::Generate;
        icfg.udp_ttl_mcast = cfg.udp_ttl;
        icfg.udp_mcast_iface = cfg.udp_iface;
        icfg.udp_loop = cfg.udp_loop;
        return icfg;
    };

    if (!cfg.repeat) {
        misbklv::KlvSink sink(build_config());
        if (sink.error()) return misbklv::Result<std::monostate>::err(*sink.error());
        auto r = do_one_pass(extractor, session, first_monotonic_us, origin_us, sink, cfg.transform_options, info.format);
        if (!r) {
            (void)sink.close();
            return r;
        }
        if (is_mux_interrupted()) {
            (void)sink.close();
            return misbklv::Result<std::monostate>::ok(std::monostate{});
        }
        return sink.close();
    } else {
        while (!is_mux_interrupted()) {
            misbklv::KlvSink sink(build_config());
            if (sink.error()) return misbklv::Result<std::monostate>::err(*sink.error());
            auto r = do_one_pass(extractor, session, first_monotonic_us, origin_us, sink, cfg.transform_options, info.format);
            if (!r) {
                (void)sink.close();
                return r;
            }
            if (is_mux_interrupted()) {
                (void)sink.close();
                break;
            }
            auto close_res = sink.close();
            if (!close_res) return close_res;
            if (is_mux_interrupted()) break;
        }
        return misbklv::Result<std::monostate>::ok(std::monostate{});
    }
}

}  // namespace parrot_to_klv
