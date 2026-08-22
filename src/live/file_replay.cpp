// Copyright 2026 Eric Schneider
// Licensed under the Apache License, Version 2.0

#include "live/file_replay.hpp"

#include <chrono>
#include <cstdio>
#include <cstdint>
#include <span>
#include <stop_token>
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

void interruptible_sleep_for(std::chrono::microseconds delta, bool disable) {
    if (disable || delta.count() <= 0) return;
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

// Bridge the SIGINT flag to a stop_source for sink.close(token). The
// signal handler only sets an atomic flag (async-signal-safe), and a handler
// cannot call stop_source::request_stop() safely either, so a watcher thread
// polls the flag and forwards it. Without this, the final close() would still
// drain the video branch at wall-clock speed after the emission loop has
// already honored the interrupt — the drain tail this exists to cut short.
class InterruptBridge {
  public:
    InterruptBridge() {
        watcher_ = std::jthread([src = source_](std::stop_token self) mutable {
            while (!self.stop_requested()) {
                if (is_mux_interrupted()) {
                    src.request_stop();
                    return;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        });
    }
    std::stop_token token() const { return source_.get_token(); }

  private:
    std::stop_source source_;
    std::jthread watcher_;
};

// Build per-iteration sink string: for file: sinks with a test repeat limit,
// emit each pass to a distinct file so the test can read back both loops
// independently (single file sink would be truncated by the second KlvSink).
std::string sink_for_iteration(const std::string& sink, int iteration, int limit) {
    if (limit > 0 && starts_with(sink, "file:")) {
        // For finite test repeats with a file sink, disambiguate per pass.
        // e.g. file:/tmp/out.ts -> file:/tmp/out.ts.pass0 , .pass1
        // The extension stays parseable; KlvStream only cares it exists.
        return sink + ".pass" + std::to_string(iteration);
    }
    return sink;
}

misbklv::Result<std::monostate> do_one_pass(
    const std::vector<MetadataSample>& samples,
    const SessionMetadata& session,
    const std::optional<uint64_t>& first_monotonic_us,
    uint64_t origin_us,
    int64_t pts_base_ns,
    misbklv::KlvSink& sink,
    const TransformOptions& opts,
    ParrotFormat format,
    bool disable_pacing) {
    // Track previous timestamp for pacing.
    bool has_prev = false;
    uint64_t prev_ts = origin_us;

    for (const auto& sample : samples) {
        if (is_mux_interrupted()) break;
        // Paced emission: sleep delta since previous sample.
        if (has_prev) {
            if (sample.timestamp_us > prev_ts) {
                auto delta_us = sample.timestamp_us - prev_ts;
                interruptible_sleep_for(std::chrono::microseconds(delta_us), disable_pacing);
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
        int64_t pts_ns = pts_base_ns + static_cast<int64_t>((sample.timestamp_us - origin_us) * 1000ULL);
        // Guard against underflow if origin > timestamp (out-of-order): clamp to base.
        if (sample.timestamp_us < origin_us) pts_ns = pts_base_ns;
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
    // Validate sink scheme. For test_repeat_limit>0 we validate the base sink
    // prefix; per-pass disambiguation preserves the file: prefix.
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

    // Cache samples once — reused for origin/max/anchor and every repeat loop.
    std::vector<MetadataSample> samples = extractor.extract_all_samples();
    if (samples.empty()) {
        return misbklv::Result<std::monostate>::err(misbklv::Error::Backend);
    }

    // Session metadata: caller (main.cpp) always parses once and passes it;
    // file_replay uses the provided session without re-opening the file.
    SessionMetadata session;
    if (cfg.session.has_value()) {
        session = *cfg.session;
    } // empty session is valid (no media_date anchor), no re-parse fallback.

    // First monotonic for anchor (v3 and proto generically) from cached sample 0.
    std::optional<uint64_t> first_monotonic_us;
    {
        const auto& first = samples.front();
        if (info.format == ParrotFormat::V3) {
            ParrotV3Error err{};
            if (auto f = parse_v3(std::span<const uint8_t>(first.data.data(), first.data.size()), &err)) {
                first_monotonic_us = from_v3(*f).monotonic_us;
            }
        } else if (info.format == ParrotFormat::Proto) {
            ParrotProtoError err{};
            if (auto f = parse_proto(std::span<const uint8_t>(first.data.data(), first.data.size()), &err)) {
                first_monotonic_us = from_proto(*f).monotonic_us;
            }
        }
    }

    // Origin: earliest presentation time (min timestamp_us) for zero-based PTS.
    uint64_t origin_us = samples.front().timestamp_us;
    uint64_t max_ts_us = samples.front().timestamp_us;
    for (const auto& s : samples) {
        if (s.timestamp_us < origin_us) origin_us = s.timestamp_us;
        if (s.timestamp_us > max_ts_us) max_ts_us = s.timestamp_us;
    }
    // Continue step uses the recording's full length (track duration including
    // tail), so loop 2 starts cleanly past loop 1's last PTS rather than
    // overlapping it. Fall back to max-origin only if duration unavailable.
    const int64_t duration_ns = info.duration_us > 0
        ? static_cast<int64_t>(info.duration_us * 1000ULL)
        : static_cast<int64_t>((max_ts_us - origin_us) * 1000ULL);

    auto build_config = [&](const std::string& sink_str) {
        misbklv::InsertConfig icfg;
        icfg.sink = sink_str;
        icfg.realtime = true;
        icfg.video_source = "file:" + cfg.input_path;
        icfg.sei_0604 = misbklv::Sei0604::Generate;
        icfg.udp_ttl_mcast = cfg.udp_ttl;
        icfg.udp_mcast_iface = cfg.udp_iface;
        icfg.udp_loop = cfg.udp_loop;
        return icfg;
    };

    // One bridge for the whole call: every close() below takes its token, so
    // an interrupt that lands during any drain tail cuts it short (ADR 0032
    // upstream). The watcher joins when this scope exits.
    InterruptBridge interrupt;

    if (!cfg.repeat) {
        misbklv::KlvSink sink(build_config(cfg.output_sink));
        if (sink.error()) return misbklv::Result<std::monostate>::err(*sink.error());
        auto r = do_one_pass(samples, session, first_monotonic_us, origin_us, 0, sink, cfg.transform_options, info.format, cfg.test_disable_pacing);
        if (!r) {
            (void)sink.close(interrupt.token());
            return r;
        }
        if (is_mux_interrupted()) {
            (void)sink.close(interrupt.token());
            return misbklv::Result<std::monostate>::ok(std::monostate{});
        }
        return sink.close(interrupt.token());
    } else {
        // Finite test mode: run exactly test_repeat_limit passes then return,
        // without requiring SIGINT. This keeps pre-interrupt tests hermetic
        // and avoids latching the global interrupt.
        if (cfg.test_repeat_limit > 0) {
            int64_t pts_base_ns = 0;
            for (int iter = 0; iter < cfg.test_repeat_limit; ++iter) {
                if (is_mux_interrupted()) break;
                std::string sink_str = sink_for_iteration(cfg.output_sink, iter, cfg.test_repeat_limit);
                misbklv::KlvSink sink(build_config(sink_str));
                if (sink.error()) return misbklv::Result<std::monostate>::err(*sink.error());
                auto r = do_one_pass(samples, session, first_monotonic_us, origin_us, pts_base_ns, sink, cfg.transform_options, info.format, cfg.test_disable_pacing);
                if (!r) {
                    (void)sink.close(interrupt.token());
                    return r;
                }
                if (is_mux_interrupted()) {
                    (void)sink.close(interrupt.token());
                    break;
                }
                auto close_res = sink.close(interrupt.token());
                if (!close_res) return close_res;
                if (is_mux_interrupted()) break;
                if (cfg.repeat_pts_continue) {
                    pts_base_ns += duration_ns;
                } else {
                    pts_base_ns = 0;
                }
            }
            return misbklv::Result<std::monostate>::ok(std::monostate{});
        }
        int64_t pts_base_ns = 0;
        while (!is_mux_interrupted()) {
            misbklv::KlvSink sink(build_config(cfg.output_sink));
            if (sink.error()) return misbklv::Result<std::monostate>::err(*sink.error());
            auto r = do_one_pass(samples, session, first_monotonic_us, origin_us, pts_base_ns, sink, cfg.transform_options, info.format, cfg.test_disable_pacing);
            if (!r) {
                (void)sink.close(interrupt.token());
                return r;
            }
            if (is_mux_interrupted()) {
                (void)sink.close(interrupt.token());
                break;
            }
            auto close_res = sink.close(interrupt.token());
            if (!close_res) return close_res;
            if (is_mux_interrupted()) break;
            if (cfg.repeat_pts_continue) {
                pts_base_ns += duration_ns;
            } else {
                pts_base_ns = 0;
            }
        }
        return misbklv::Result<std::monostate>::ok(std::monostate{});
    }
}

}  // namespace parrot_to_klv
