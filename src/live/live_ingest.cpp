// Copyright 2026 Eric Schneider
// Licensed under the Apache License, Version 2.0

#include "live/live_ingest.hpp"

#include "live/parrot_rtp_header_extension.hpp"

#include <span>
#include <string>
#include <vector>

#include "misbklv/backend.hpp"
#include "misbklv/message.hpp"
#include "misbklv/stream.hpp"
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
           starts_with(s, "pipeline:");  // pipeline: is test-only escape hatch for hermetic tests; not documented for end users
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

    // Ensure the Parrot header extension is registered so a future RTSP pull
    // via rtspsrc can negotiate it through extmap. Registration is idempotent.
    (void)register_parrot_extension();

    const std::string video_src = cfg.rtsp_uri;

    misbklv::InsertConfig icfg;
    icfg.sink = cfg.udp_sink;
    icfg.realtime = true;
    icfg.video_source = video_src;
    // Live sources do not support Sei0604::Generate (codec not negotiated until
    // runtime and the SEI probe is not wired for live). Use Preserve so the
    // pipeline opens; file-mode mux still uses Generate.
    icfg.sei_0604 = misbklv::Sei0604::Preserve;
    icfg.udp_ttl_mcast = cfg.udp_ttl;
    icfg.udp_mcast_iface = cfg.udp_mcast_iface;
    icfg.udp_loop = cfg.udp_loop;

    misbklv::KlvSink sink(icfg);
    if (sink.error()) {
        return misbklv::Result<std::monostate>::err(*sink.error());
    }

    // For hermetic pipeline: tests the push path is wired. Three dummy KLV
    // packets with PTS 1 s, 2 s, 3 s on the live timeline (avoid PTS 0 which
    // can be before the pipeline running_time warmup in realtime mode).
    // Also exercised for RTSP when the open succeeded (reachable server); for
    // the unreachable case we already returned via sink.error().
    for (int i = 0; i < 3; ++i) {
        TelemetrySnapshot snap = make_dummy_snapshot(i);
        auto klv = to_0601(snap, nullptr);
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
