// Copyright 2026 Eric Schneider
// Licensed under the Apache License, Version 2.0

#include "live/live_ingest.hpp"

#include "live/parrot_rtp_header_extension.hpp"
#include "mux.hpp"

#include <chrono>
#include <cstdint>
#include <span>
#include <string>
#include <thread>
#include <vector>

#include <gst/gst.h>

#include "misbklv/backend.hpp"
#include "misbklv/message.hpp"
#include "misbklv/stream.hpp"
#include "parrot_proto.hpp"
#include "parrot_v3.hpp"
#include "telemetry.hpp"
#include "transform.hpp"

namespace parrot_to_klv {
namespace {

bool starts_with(const std::string& s, const std::string& prefix) {
    return s.rfind(prefix, 0) == 0;
}

bool is_valid_sink(const std::string& s) {
    return starts_with(s, "udp:") || starts_with(s, "file:") || starts_with(s, "srt:");
}

bool is_valid_video_source(const std::string& s) {
    return starts_with(s, "rtsp://") || starts_with(s, "rtsps://") ||
           starts_with(s, "pipeline:");  // pipeline: is test-only hermetic escape hatch; CLI gates it via PARROT_TEST_ALLOW_PIPELINE
}

TelemetrySnapshot make_dummy_snapshot(int idx) {
    TelemetrySnapshot snap;
    snap.source_format = ParrotFormat::V3;
    snap.monotonic_us = static_cast<uint64_t>(idx) * 1'000'000ULL;
    // Absolute timestamp: arbitrary epoch with 1 s spacing, ensures Item 2 present.
    snap.utc_timestamp_us = 1720000000000000ULL + static_cast<uint64_t>(idx) * 1'000'000ULL;
    snap.latitude_deg = 48.8566 + idx * 0.001;
    snap.longitude_deg = 2.3522 + idx * 0.001;
    snap.altitude_amsl_m = 120.0;
    snap.altitude_wgs84_m = std::nullopt;
    snap.ground_distance_m = 50.0;
    snap.sv_count = 12;
    snap.drone_quat = Quaternion{1.0, 0.0, 0.0, 0.0};
    snap.frame_quat = Quaternion{1.0, 0.0, 0.0, 0.0};
    snap.north_mps = 5.0;
    snap.east_mps = 0.0;
    snap.down_mps = -1.0;
    snap.hfov_deg = 70.0;
    snap.vfov_deg = 40.0;
    snap.flying_state = FlyingState::Hovering;
    snap.piloting_mode = CommonPilotingMode::Manual;
    return snap;
}

misbklv::Result<std::monostate> emit_dummy_batch(misbklv::KlvSink& sink,
                                                 const SessionMetadata* session,
                                                 const TransformOptions& opts) {
    for (int i = 0; i < 3; ++i) {
        TelemetrySnapshot snap = make_dummy_snapshot(i);
        auto klv = to_0601(snap, session, opts);
        if (klv.packet.empty()) continue;
        auto msg_res = misbklv::Message::parse(
            std::as_bytes(std::span<const uint8_t>(klv.packet)));
        if (!msg_res) return misbklv::Result<std::monostate>::err(msg_res.error());
        int64_t pts_ns = 300'000'000LL + static_cast<int64_t>(i) * 300'000'000LL;
        msg_res->set_pts(pts_ns);
        auto emit_res = sink.emit(*msg_res);
        if (!emit_res) return emit_res;
    }
    return misbklv::Result<std::monostate>::ok(std::monostate{});
}

// Try to decode one captured extension and emit KLV. Returns true if emitted.
// Handles v3 first, then proto with coalescing across packets.
bool try_decode_and_emit(const CapturedExtension& cap, misbklv::KlvSink& sink,
                         const SessionMetadata* session, const TransformOptions& opts,
                         size_t emitted_index,
                         std::vector<std::byte>& proto_coalesce, uint64_t& proto_coalesce_pts) {
    std::span<const uint8_t> payload_u8(reinterpret_cast<const uint8_t*>(cap.payload.data()),
                                        cap.payload.size());
    if (payload_u8.empty()) return false;

    // Try v3 first — payload for v3 starts with 0x5033 and declared length matches.
    {
        ParrotV3Error v3_err{};
        auto v3_frame = parse_v3(payload_u8, &v3_err);
        if (v3_frame) {
            // New frame type, clear any pending proto coalesce.
            proto_coalesce.clear();
            proto_coalesce_pts = GST_CLOCK_TIME_NONE;

            TelemetrySnapshot snap = from_v3(*v3_frame);
            auto klv = to_0601(snap, session, opts);
            if (klv.packet.empty()) return false;
            auto msg_res = misbklv::Message::parse(
                std::as_bytes(std::span<const uint8_t>(klv.packet)));
            if (!msg_res) return false;
            // Assumption: RTP buffer PTS at header-extension read equals reception
            // running_time per ADR 0013/ADR 0021; if rtspsrc buffering or
            // RTP-timestamp mode diverges, live KLV will drift from video. Hermetic
            // test injects PTS directly so cannot validate — validate against real/mock
            // RTSP source in follow-up.
            int64_t pts_ns;
            if (cap.pts_ns != GST_CLOCK_TIME_NONE) {
                pts_ns = static_cast<int64_t>(cap.pts_ns);
            } else {
                pts_ns = 1'000'000'000LL + static_cast<int64_t>(emitted_index) * 1'000'000'000LL;
            }
            msg_res->set_pts(pts_ns);
            auto emit_res = sink.emit(*msg_res);
            return static_cast<bool>(emit_res);
        }
    }

    // Not v3 — treat as proto (possibly split across first packets of a frame).
    // Coalesce: append payload to buffer until parse_proto succeeds.
    if (proto_coalesce.empty()) {
        proto_coalesce_pts = cap.pts_ns;
    }
    proto_coalesce.insert(proto_coalesce.end(), cap.payload.begin(), cap.payload.end());
    // Bound coalesce to 1 MiB to avoid unbounded growth on corrupt stream.
    if (proto_coalesce.size() > 1024 * 1024) {
        proto_coalesce.clear();
        proto_coalesce_pts = GST_CLOCK_TIME_NONE;
        return false;
    }
    std::span<const uint8_t> coalesced_u8(reinterpret_cast<const uint8_t*>(proto_coalesce.data()),
                                          proto_coalesce.size());
    ParrotProtoError proto_err{};
    auto proto_frame = parse_proto(coalesced_u8, &proto_err);
    if (proto_frame) {
        TelemetrySnapshot snap = from_proto(*proto_frame);
        auto klv = to_0601(snap, session, opts);
        if (klv.packet.empty()) {
            proto_coalesce.clear();
            proto_coalesce_pts = GST_CLOCK_TIME_NONE;
            return false;
        }
        auto msg_res = misbklv::Message::parse(
            std::as_bytes(std::span<const uint8_t>(klv.packet)));
        if (!msg_res) {
            proto_coalesce.clear();
            proto_coalesce_pts = GST_CLOCK_TIME_NONE;
            return false;
        }
        // Assumption: RTP buffer PTS at header-extension read equals reception
        // running_time per ADR 0013/ADR 0021; if rtspsrc buffering or
        // RTP-timestamp mode diverges, live KLV will drift from video. Hermetic
        // test injects PTS directly so cannot validate — validate against real/mock
        // RTSP source in follow-up.
        int64_t pts_ns;
        if (proto_coalesce_pts != GST_CLOCK_TIME_NONE) {
            pts_ns = static_cast<int64_t>(proto_coalesce_pts);
        } else {
            pts_ns = 1'000'000'000LL + static_cast<int64_t>(emitted_index) * 1'000'000'000LL;
        }
        msg_res->set_pts(pts_ns);
        auto emit_res = sink.emit(*msg_res);
        proto_coalesce.clear();
        proto_coalesce_pts = GST_CLOCK_TIME_NONE;
        return static_cast<bool>(emit_res);
    }
    // If not truncated, discard coalesce to recover from bad data; truncated keeps buffering.
    if (proto_err != ParrotProtoError::Truncated) {
        proto_coalesce.clear();
        proto_coalesce_pts = GST_CLOCK_TIME_NONE;
    }
    return false;
}

}  // namespace

misbklv::Result<std::monostate> live_ingest(const LiveIngestConfig& cfg) {
    if (cfg.rtsp_uri.empty() || cfg.udp_sink.empty()) {
        return misbklv::Result<std::monostate>::err(misbklv::Error::Unsupported);
    }
    if (!is_valid_sink(cfg.udp_sink)) {
        return misbklv::Result<std::monostate>::err(misbklv::Error::Unsupported);
    }
    if (!is_valid_video_source(cfg.rtsp_uri)) {
        return misbklv::Result<std::monostate>::err(misbklv::Error::Unsupported);
    }

    // Clear any residual captures from prior sessions or late RTSP pushes after
    // the previous close() so they do not bleed across sessions.
    parrot_extension_clear();
    // Test-only deterministic injection: when cfg carries payloads, populate
    // the FIFO after the clear so the immediate drain sees them without an
    // async poll. Production cfg leaves this empty.
    for (const auto& cap : cfg.test_injected_extensions) {
        parrot_extension_push_for_test(std::span<const std::byte>(cap.payload.data(), cap.payload.size()),
                                       cap.pts_ns);
    }

    // Ensure the Parrot header extension is registered so a future RTSP pull
    // via rtspsrc can negotiate it through extmap. Registration is idempotent.
    (void)register_parrot_extension();

    const std::string video_src = cfg.rtsp_uri;

    misbklv::InsertConfig icfg;
    icfg.sink = cfg.udp_sink;
    icfg.realtime = true;
    icfg.video_source = video_src;
    // Live H.264 now uses Generate so KLV and video stay time-synced via ST 0604 SEI.
    // libmisbklv's pipeline and RTSP branches wire the same h264parse SEI probe that
    // the file path uses; probe is gated on H.264 caps (libmisbklv #23 #1) to avoid
    // corruption on non-H.264, so non-H.264 live degrades to unstamped (no SEI)
    // per ADR 0007 — H.265 is carried through without SEI.
    icfg.sei_0604 = misbklv::Sei0604::Generate;
    icfg.udp_ttl_mcast = cfg.udp_ttl;
    icfg.udp_mcast_iface = cfg.udp_mcast_iface;
    icfg.udp_loop = cfg.udp_loop;

    misbklv::KlvSink sink(icfg);
    if (sink.error()) {
        return misbklv::Result<std::monostate>::err(*sink.error());
    }

    const SessionMetadata* session_ptr = cfg.session ? &*cfg.session : nullptr;

    // Attempt real decode path: drain FIFO bounded queue that the
    // ParrotRtpHeaderExtension::read populates. For live RTSP, proto may be
    // split across first packets — coalesce until parse succeeds. PTS is the
    // RTP buffer PTS (running_time) per ADR 0013.
    size_t emitted_real = 0;
    std::vector<std::byte> proto_coalesce;
    uint64_t proto_coalesce_pts = GST_CLOCK_TIME_NONE;

    bool is_rtsp = starts_with(video_src, "rtsp://") || starts_with(video_src, "rtsps://");
    bool is_pipeline = starts_with(video_src, "pipeline:");

    // Continuous mode when run_duration > 0: drive until interrupt or deadline.
    // This is the live loop (ADR 0013): RTSP never EOS, pipeline with is-live
    // and no num-buffers is unbounded, so we run until stopped. For hermetic
    // tests, run_duration=2s gives a bounded window that still exercises the
    // continuous machinery. When run_duration==0 we preserve the one-shot
    // behavior for backward compatibility and fast unit tests.
    if (cfg.run_duration.count() > 0) {
        auto deadline = std::chrono::steady_clock::now() + cfg.run_duration;
        bool dummy_emitted = false;

        while (true) {
            if (is_mux_interrupted()) break;
            if (std::chrono::steady_clock::now() >= deadline) break;
            if (sink.error()) break;

            bool had = false;
            while (auto cap = parrot_extension_pop_next()) {
                had = true;
                if (try_decode_and_emit(*cap, sink, session_ptr, cfg.transform_options,
                                        emitted_real, proto_coalesce, proto_coalesce_pts)) {
                    ++emitted_real;
                }
                if (sink.error()) break;
            }
            if (sink.error()) break;
            if (is_mux_interrupted()) break;

            // If no real telemetry has arrived yet and this is a pipeline: hermetic
            // source, emit dummy once so the output still carries KLV for the wiring
            // check. For RTSP we emit nothing when metadata is absent (real stream
            // would have no KLV). Dummy is emitted at most once, on the first
            // idle window, to mimic the one-shot path but inside the live loop.
            if (emitted_real == 0 && !dummy_emitted && is_pipeline) {
                // Wait a short grace period before deciding no real telemetry is coming,
                // so an injected real payload drained in the first iteration is not
                // shadowed by dummy. If we already had data, emitted_real>0 and we
                // never reach here.
                if (!had) {
                    // No data this iteration — sleep a bit then emit dummy if still none
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                    // Re-check queue before committing to dummy
                    bool late_arrival = false;
                    while (auto cap = parrot_extension_pop_next()) {
                        late_arrival = true;
                        if (try_decode_and_emit(*cap, sink, session_ptr, cfg.transform_options,
                                                emitted_real, proto_coalesce, proto_coalesce_pts)) {
                            ++emitted_real;
                        }
                    }
                    if (emitted_real == 0 && !late_arrival) {
                        if (auto r = emit_dummy_batch(sink, session_ptr, cfg.transform_options); !r) return r;
                    }
                    dummy_emitted = true;
                    if (sink.error()) break;
                    continue;
                }
            }

            if (!had) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }

        // If loop ended without dummy and we still have no real, emit dummy for
        // pipeline hermetic (covers edge where deadline fired before dummy window).
        if (emitted_real == 0 && !dummy_emitted && is_pipeline) {
            // Reached deadline before dummy grace — ensure file still valid for
            // hermetic checks.
            if (auto r = emit_dummy_batch(sink, session_ptr, cfg.transform_options); !r) return r;
        }

        // For RTSP with no metadata, no dummy is emitted — just close (video only).
        return sink.close();
    }

    // One-shot mode (run_duration==0): immediate drain + short RTSP poll.
    // Immediate drain for captured or test-injected payloads.
    while (auto cap = parrot_extension_pop_next()) {
        if (try_decode_and_emit(*cap, sink, session_ptr, cfg.transform_options,
                                emitted_real, proto_coalesce, proto_coalesce_pts)) {
            ++emitted_real;
        }
    }

    // For rtsp://, extensions arrive asynchronously after KlvSink starts its
    // internal rtspsrc pipeline. Poll briefly for late arrivals. For
    // pipeline: hermetic path, never poll — metadata-less pipeline must emit
    // dummy immediately rather than waiting ~600 ms. Hermetic real-decode
    // tests inject via cfg.test_injected_extensions and are already drained
    // above without polling.
    if (is_rtsp && emitted_real == 0) {
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(600);
        while (std::chrono::steady_clock::now() < deadline) {
            bool had = false;
            while (auto cap = parrot_extension_pop_next()) {
                had = true;
                if (try_decode_and_emit(*cap, sink, session_ptr, cfg.transform_options,
                                        emitted_real, proto_coalesce, proto_coalesce_pts)) {
                    ++emitted_real;
                }
            }
            if (had) {
                // Got data, extend deadline a bit to allow remaining splits.
                deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(200);
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
            // If sink errored during poll, stop.
            if (sink.error()) break;
            if (is_mux_interrupted()) break;
            // If we already emitted at least one, we can stop after queue drains.
            if (emitted_real > 0 && parrot_extension_count() == 0) break;
        }
        // If polling timed out with no data, emitted_real stays 0 — fall through
        // to not emit dummy for rtsp:// (real stream would have no KLV).
    }

    if (emitted_real > 0) {
        return sink.close();
    }

    // No real extensions captured — hermetic pipeline: path uses dummy KLV so
    // the pipeline wiring test stays hermetic without an RTSP server.
    // For rtsp:// unreachable, we already returned via sink.error(); for
    // reachable rtsp:// that genuinely had no metadata, emitting nothing is
    // also reasonable, but preserve dummy for pipeline: hermetic compatibility.
    if (!is_pipeline && is_rtsp) {
        // Reachable RTSP but no metadata — no dummy, just close (may produce
        // file with video only via KlvSink's video branch).
        return sink.close();
    }

    for (int i = 0; i < 3; ++i) {
        TelemetrySnapshot snap = make_dummy_snapshot(i);
        auto klv = to_0601(snap, session_ptr, cfg.transform_options);
        if (klv.packet.empty()) {
            continue;
        }
        auto msg_res = misbklv::Message::parse(
            std::as_bytes(std::span<const uint8_t>(klv.packet)));
        if (!msg_res) {
            return misbklv::Result<std::monostate>::err(msg_res.error());
        }
        // PTS in nanoseconds from start of source — matches live running_time.
        int64_t pts_ns = 1'000'000'000LL + static_cast<int64_t>(i) * 1'000'000'000LL;
        msg_res->set_pts(pts_ns);
        auto emit_res = sink.emit(*msg_res);
        if (!emit_res) {
            return emit_res;
        }
    }

    return sink.close();
}

}  // namespace parrot_to_klv
