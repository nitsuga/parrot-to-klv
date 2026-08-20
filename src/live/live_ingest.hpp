// Copyright 2026 Eric Schneider
// Licensed under the Apache License, Version 2.0
#pragma once

#include <string>
#include <variant>

#include "misbklv/types.hpp"

namespace parrot_to_klv {

struct LiveIngestConfig {
    std::string rtsp_uri;  // e.g., rtsp://192.168.0.1/live
    std::string udp_sink;  // e.g., udp:239.255.0.1:5000
    std::string udp_mcast_iface;
    int udp_ttl = 1;
    bool udp_loop = true;
};

// Stub — will later: pull RTSP, capture RTP extensions, decode, transform,
// push to KlvSink with video_source="rtsp://...". For now just validates
// config and returns Unsupported to prove the API exists.
misbklv::Result<std::monostate> live_ingest(const LiveIngestConfig& cfg);

}  // namespace parrot_to_klv
