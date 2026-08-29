// Copyright 2026 Eric Schneider
// Licensed under the Apache License, Version 2.0
#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "live/parrot_rtp_header_extension.hpp"
#include "misbklv/types.hpp"
#include "session_metadata.hpp"
#include "transform.hpp"

namespace parrot_to_klv {

struct LiveIngestConfig {
    // Video source URI. For live ingest this is typically an RTSP URL
    // (rtsp:// or rtsps://) but hermetic tests may use a GStreamer pipeline
    // description prefixed with "pipeline:" which is forwarded verbatim as
    // the KlvSink video_source. Keeping the field name rtsp_uri for backward
    // compatibility; treat it as the generic live video_source.
    std::string rtsp_uri;  // e.g., rtsp://192.168.0.1/live or pipeline:...
    std::string udp_sink;  // e.g., udp:239.255.0.1:5000 or file:/tmp/out.ts
    std::string udp_mcast_iface;
    int udp_ttl = 1;
    bool udp_loop = true;

    // Optional session metadata and transform options for live KLV mapping.
    // When absent, defaults are used (no session, default TransformOptions).
    std::optional<SessionMetadata> session;
    TransformOptions transform_options;

    // Test-only: when non-empty, injected deterministically after the entry
    // queue clear. This makes hermetic real-decode tests deterministic
    // without an async thread racing the poll window, and lets the normal
    // pipeline: dummy path remain immediate (no 600 ms poll) when no
    // extensions are present.
    std::vector<CapturedExtension> test_injected_extensions;

    // Continuous mode: how long live_ingest should drive before closing.
    // run_duration == nullopt -> indefinite live-unbounded (explicit
    // opt-in, must be set for production live — drive until
    // is_mux_interrupted()/SIGINT — finite live sources must be stopped
    // manually. A failure from emitting a decoded KLV or from the live pipeline
    // is returned immediately; the loop otherwise ends on its deadline or
    // SIGINT.
    // Default is timed 2000 ms for safety (safe for tests that forget to set
    // a duration); run_duration has value -> timed drive until deadline or
    // SIGINT (e.g., 2s for hermetic tests). If caller wants one-shot startup
    // poll (previous behavior), use a small run_duration like 600ms.
    std::optional<std::chrono::milliseconds> run_duration{std::chrono::milliseconds(2000)};

    // End the session when no Parrot RTP header extension has arrived for this
    // long. The primary end-of-source signal (ADR 0018): a clean RTSP teardown
    // is the polite ending, but power loss and hard link failures send no
    // TEARDOWN at all, so idleness is the only signal guaranteed to fire.
    // The timer arms at drive-loop entry, not at the first sample, so a source
    // that connects and never delivers ends the same way as one that stops.
    // Zero disables it, restoring a run that ends only on its deadline or
    // SIGINT. Telemetry rides inside the video stream's header extensions, so
    // telemetry arriving implies video arriving — the converse does not hold,
    // which is why a video-only stream is out of this watchdog's reach.
    std::chrono::seconds idle_timeout{10};
};

// Live ingest: validate cfg, register Parrot RTP header extension, create a
// KlvSink with live video_source and realtime=true, push KLV derived from
// telemetry (via to_0601) with PTS anchored to running_time, then close.
// For pipeline: video_source the path is hermetic (no RTSP server needed);
// for rtsp:// the open attempts a 5 s connection and returns Unsupported if
// unreachable — proving the RTSP URI is forwarded and the extension is wired.
misbklv::Result<std::monostate> live_ingest(const LiveIngestConfig& cfg);

}  // namespace parrot_to_klv
