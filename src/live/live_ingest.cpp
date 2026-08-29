// Copyright 2026 Eric Schneider
// Licensed under the Apache License, Version 2.0

#include "live/live_ingest.hpp"

#include "live/parrot_rtp_header_extension.hpp"
#include "mux.hpp"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <optional>
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

bool is_valid_sink(const std::string& s) {
    return s.starts_with("udp:") || s.starts_with("file:") || s.starts_with("srt:");
}

bool is_valid_video_source(const std::string& s) {
    return s.starts_with("rtsp://") || s.starts_with("rtsps://") ||
           s.starts_with("pipeline:");  // pipeline: is test-only hermetic escape hatch; CLI gates it via PARROT_TEST_ALLOW_PIPELINE
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

// Try to decode one captured extension and emit KLV. The bool is true if
// emitted; an error is returned when generated KLV parsing or the sink rejects
// the packet.
// Handles v3 first, then proto with coalescing across packets.
misbklv::Result<bool> try_decode_and_emit(
    const CapturedExtension& cap, misbklv::KlvSink& sink,
    const SessionMetadata* session, const TransformOptions& opts,
    size_t emitted_index, std::vector<std::byte>& proto_coalesce,
    uint64_t& proto_coalesce_pts) {
    std::span<const uint8_t> payload_u8(reinterpret_cast<const uint8_t*>(cap.payload.data()),
                                        cap.payload.size());
    if (payload_u8.empty()) return misbklv::Result<bool>::ok(false);

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
            if (klv.packet.empty()) return misbklv::Result<bool>::ok(false);
            auto msg_res = misbklv::Message::parse(
                std::as_bytes(std::span<const uint8_t>(klv.packet)));
            if (!msg_res) return misbklv::Result<bool>::err(msg_res.error());
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
            if (!emit_res) return misbklv::Result<bool>::err(emit_res.error());
            return misbklv::Result<bool>::ok(true);
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
        return misbklv::Result<bool>::ok(false);
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
            return misbklv::Result<bool>::ok(false);
        }
        auto msg_res = misbklv::Message::parse(
            std::as_bytes(std::span<const uint8_t>(klv.packet)));
        if (!msg_res) {
            proto_coalesce.clear();
            proto_coalesce_pts = GST_CLOCK_TIME_NONE;
            return misbklv::Result<bool>::err(msg_res.error());
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
        if (!emit_res) return misbklv::Result<bool>::err(emit_res.error());
        return misbklv::Result<bool>::ok(true);
    }
    // If not truncated, discard coalesce to recover from bad data; truncated keeps buffering.
    if (proto_err != ParrotProtoError::Truncated) {
        proto_coalesce.clear();
        proto_coalesce_pts = GST_CLOCK_TIME_NONE;
    }
    return misbklv::Result<bool>::ok(false);
}

}  // namespace

namespace live_detail {

std::string roll_sink_spec(const std::string& sink_spec, unsigned index) {
    // A live sink reuses its address; only a file has a name to roll.
    if (!sink_spec.starts_with("file:")) return sink_spec;
    const std::string path = sink_spec.substr(5);
    // Split on the final extension, and only when it belongs to the basename —
    // a dot in a parent directory must not be mistaken for one.
    const auto slash = path.find_last_of('/');
    const auto dot = path.find_last_of('.');
    const bool has_ext = dot != std::string::npos && (slash == std::string::npos || dot > slash + 1);
    char counter[16];
    std::snprintf(counter, sizeof(counter), "_%04u", index);
    if (!has_ext) return "file:" + path + counter;
    return "file:" + path.substr(0, dot) + counter + path.substr(dot);
}

std::optional<unsigned> first_free_index(const std::string& sink_spec) {
    if (!sink_spec.starts_with("file:")) return 0u;
    // The counter is four digits wide, so the range is 0..9999. Exhausting it
    // returns nullopt rather than the last index: that path exists, and handing
    // it back would have the sink truncate a capture on open.
    for (unsigned i = 0; i <= 9999; ++i) {
        const std::string candidate = roll_sink_spec(sink_spec, i).substr(5);
        std::error_code ec;
        // A stat failure means "cannot prove this is free", which must count as
        // occupied — exists() reports false on error, so check ec explicitly.
        if (!std::filesystem::exists(candidate, ec) && !ec) return i;
    }
    return std::nullopt;
}

}  // namespace live_detail

namespace {

// One live session: build a pipeline against `sink_spec`, drive it until the
// source stops, and close. `emitted_out` receives the number of real telemetry
// packets emitted, which is what decides whether a reconnect counted (ADR 0018).
misbklv::Result<std::monostate> run_one_session(const LiveIngestConfig& cfg,
                                                const std::string& sink_spec,
                                                size_t* emitted_out, bool* opened_out) {
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
    icfg.sink = sink_spec;
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
    // Past this point the sink and the video branch both exist, so any later
    // failure is the session dying rather than a configuration that cannot run.
    if (opened_out) *opened_out = true;

    const SessionMetadata* session_ptr = cfg.session ? &*cfg.session : nullptr;

    // Attempt real decode path: drain FIFO bounded queue that the
    // ParrotRtpHeaderExtension::read populates. For live RTSP, proto may be
    // split across first packets — coalesce until parse succeeds. PTS is the
    // RTP buffer PTS (running_time) per ADR 0013.
    // Bound to the caller's counter when it supplied one, so an early error
    // return still reports the telemetry this session emitted. Writing it only
    // at the normal exit lost that, and a mid-sortie failure then looked like a
    // session that had produced nothing.
    size_t emitted_local = 0;
    size_t& emitted_real = emitted_out ? *emitted_out : emitted_local;
    std::vector<std::byte> proto_coalesce;
    uint64_t proto_coalesce_pts = GST_CLOCK_TIME_NONE;

    bool is_pipeline = video_src.starts_with("pipeline:");

    // Drive mode (ADR 0013):
    // run_duration == nullopt -> no deadline (indefinite live-unbounded), with
    // the loop ending on interrupt, idle timeout, or an emission/backend error.
    // A live insert cannot observe clean EOS before close() sends it, so EOS
    // cannot end this loop on its own.
    // run_duration has value -> timed live loop while
    // (!is_mux_interrupted() && now < deadline) with 10 ms idle sleep;
    // used for hermetic tests (e.g., 2s). If caller wants one-shot
    // startup poll (previous behavior), use a small run_duration like
    // 600ms. The one-shot drain + 600ms poll when rtsp and
    // emitted==0 is now subsumed by the continuous loop's first
    // iteration (drains within 10ms) and is removed as a whole run.
    // One loop serves both: timed mode sets a deadline, indefinite mode
    // leaves it empty and runs until SIGINT.
    std::optional<std::chrono::steady_clock::time_point> deadline;
    if (cfg.run_duration.has_value()) {
        deadline = std::chrono::steady_clock::now() + *cfg.run_duration;
    }
    bool dummy_emitted = false;
    // Armed here, not at the first sample: a source that connects and never
    // delivers is the same failure as one that stops, and arming late would
    // leave exactly the hang this watchdog removes (ADR 0018).
    auto last_telemetry = std::chrono::steady_clock::now();

    while (!is_mux_interrupted()) {
        if (deadline && std::chrono::steady_clock::now() >= *deadline) break;
        if (cfg.idle_timeout.count() > 0 &&
            std::chrono::steady_clock::now() - last_telemetry >= cfg.idle_timeout) {
            break;
        }

        auto pipeline_status = sink.poll();
        if (!pipeline_status) {
            return misbklv::Result<std::monostate>::err(pipeline_status.error());
        }

        bool had = false;
        while (auto cap = parrot_extension_pop_next()) {
            had = true;
            last_telemetry = std::chrono::steady_clock::now();
            auto emitted = try_decode_and_emit(*cap, sink, session_ptr, cfg.transform_options,
                                                emitted_real, proto_coalesce, proto_coalesce_pts);
            if (!emitted) return misbklv::Result<std::monostate>::err(emitted.error());
            if (*emitted) {
                ++emitted_real;
            }
        }
        if (is_mux_interrupted()) break;

        // For pipeline: hermetic without real telemetry, emit dummy once,
        // after a 50ms grace so injected real telemetry is not shadowed.
        // Timed runs only — the indefinite production path never emits a
        // dummy (pipeline: is test-only).
        if (emitted_real == 0 && !dummy_emitted && is_pipeline &&
            cfg.run_duration.has_value()) {
            if (!had) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                pipeline_status = sink.poll();
                if (!pipeline_status) {
                    return misbklv::Result<std::monostate>::err(pipeline_status.error());
                }
                bool late_arrival = false;
                while (auto cap = parrot_extension_pop_next()) {
                    late_arrival = true;
                    last_telemetry = std::chrono::steady_clock::now();
                    auto emitted = try_decode_and_emit(*cap, sink, session_ptr,
                                                        cfg.transform_options, emitted_real,
                                                        proto_coalesce, proto_coalesce_pts);
                    if (!emitted) return misbklv::Result<std::monostate>::err(emitted.error());
                    if (*emitted) {
                        ++emitted_real;
                    }
                }
                if (emitted_real == 0 && !late_arrival) {
                    if (auto r = emit_dummy_batch(sink, session_ptr, cfg.transform_options); !r) return r;
                }
                dummy_emitted = true;
                continue;
            }
        }

        if (!had) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        // Indefinite sources run until SIGINT, the deadline, or the idle
        // timeout. sink.poll() ends the loop only on a backend failure: a live
        // insert cannot observe a clean EOS before close() sends it
        // (libmisbklv ADR 0035), which is why idleness is the primary
        // end-of-source signal rather than EOS (ADR 0018).
    }

    if (emitted_real == 0 && !dummy_emitted && is_pipeline &&
        cfg.run_duration.has_value()) {
        if (auto r = emit_dummy_batch(sink, session_ptr, cfg.transform_options); !r) return r;
    }

    // For RTSP with no metadata, no dummy is emitted — just close (video
    // only). The loop reached SIGINT, its deadline, or the idle timeout.
    // Deliberately NOT cancellable, unlike file replay. libmisbklv discards the
    // sink file on a cancelled drain (ADR 0022 — no output unless the session
    // succeeded), so forwarding the interrupt here would delete the capture the
    // operator just pressed Ctrl-C to stop. A live capture must survive its own
    // shutdown, so this waits for the drain instead. The exposure is a stalled
    // pipeline holding shutdown until libmisbklv's drain timeout; losing a
    // sortie is the worse failure.
    return sink.close();
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

    if (!cfg.reconnect) {
        return run_one_session(cfg, cfg.udp_sink, nullptr, nullptr);
    }

    // Reconnect mode: each sortie is its own session and its own finalized
    // capture (ADR 0018 mode 2). Four things end the run:
    //   - an interrupt (SIGINT/SIGTERM), the ordinary way an operator stops it;
    //   - a failure that retrying cannot fix — anything at open time that is
    //     not SourceUnavailable;
    //   - max_empty_sessions consecutive sessions that opened and carried no
    //     telemetry, which is how a broken output stops the run;
    //   - exhausting the capture index, when every _000n name is taken.
    // A source that is merely absent matches none of these and is waited on
    // for as long as the run is up.

    // Consecutive sessions that opened and produced nothing. Reset by any
    // session that emits, so a flaky link costs attempts rather than the run.
    unsigned empty_sessions = 0;
    misbklv::Error last_empty_error = misbklv::Error::Backend;
    while (!is_mux_interrupted()) {
        // Recomputed every session, never incremented. A capture that appeared
        // since the last scan — a gap in the numbering, a concurrent writer —
        // would otherwise be truncated when the counter walked onto it.
        const auto index = live_detail::first_free_index(cfg.udp_sink);
        if (!index) {
            std::fprintf(stderr, "live: no free capture index below 10000; stopping\n");
            return misbklv::Result<std::monostate>::err(misbklv::Error::ResourceLimit);
        }
        const std::string sink_spec = live_detail::roll_sink_spec(cfg.udp_sink, *index);
        size_t emitted = 0;
        bool opened = false;
        auto r = run_one_session(cfg, sink_spec, &emitted, &opened);
        if (!r) {
            // A session that never opened and reports SourceUnavailable is a
            // source that is not back yet — the normal state between sorties.
            // Anything else at open time is a configuration that cannot run:
            // an unwritable sink, a missing element, a description that will
            // not build, an RTSP server that answered and refused the request.
            // Retrying those would spin until the process is killed
            // (libmisbklv ADR 0036).
            const bool transient = opened || r.error() == misbklv::Error::SourceUnavailable;
            if (!transient) {
                std::fprintf(stderr, "live: session failed to start with error %d; not retrying\n",
                             static_cast<int>(r.error()));
                return r;
            }
            // A session that failed after opening has already discarded its
            // partial output (libmisbklv ADR 0022), so there is nothing to
            // clean up. It is reported as a generic Backend error whether the
            // source died or the sink did, so the empty-session bound below is
            // what stops a broken output from retrying forever.
            std::fprintf(stderr, "live: session ended with error %d; retrying\n",
                         static_cast<int>(r.error()));
            last_empty_error = r.error();
        } else if (emitted == 0 && sink_spec.starts_with("file:")) {
            // A source that connected and then said nothing must not leave a
            // near-empty capture behind, nor consume an index — otherwise an
            // endpoint that accepts and stalls fills the directory unattended.
            // Removing it also makes the next scan reuse this same index.
            std::error_code ec;
            std::filesystem::remove(sink_spec.substr(5), ec);
        }

        if (emitted > 0) {
            empty_sessions = 0;
        } else if (opened && cfg.max_empty_sessions > 0 &&
                   ++empty_sessions >= cfg.max_empty_sessions) {
            // Only a session that opened counts. A source that is merely absent
            // never opens, and must keep retrying for as long as the operator
            // leaves the run up — that is the flying-day case this mode is for.
            std::fprintf(stderr,
                         "live: %u consecutive sessions produced no telemetry; stopping\n",
                         empty_sessions);
            return misbklv::Result<std::monostate>::err(last_empty_error);
        }
        if (is_mux_interrupted()) break;
        // Interruptible wait, so an interrupt is noticed within a tick rather
        // than after a full interval.
        const auto wake = std::chrono::steady_clock::now() + cfg.reconnect_interval;
        while (!is_mux_interrupted() && std::chrono::steady_clock::now() < wake) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    return misbklv::Result<std::monostate>::ok({});
}

}  // namespace parrot_to_klv
