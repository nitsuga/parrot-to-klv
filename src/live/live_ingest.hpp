// Copyright 2026 Eric Schneider
// Licensed under the Apache License, Version 2.0
#pragma once

#include <optional>
#include <string>
#include <variant>

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
};

// Live ingest: validate cfg, register Parrot RTP header extension, create a
// KlvSink with live video_source and realtime=true, push KLV derived from
// telemetry (via to_0601) with PTS anchored to running_time, then close.
// For pipeline: video_source the path is hermetic (no RTSP server needed);
// for rtsp:// the open attempts a 5 s connection and returns Unsupported if
// unreachable — proving the RTSP URI is forwarded and the extension is wired.
misbklv::Result<std::monostate> live_ingest(const LiveIngestConfig& cfg);

}  // namespace parrot_to_klv
