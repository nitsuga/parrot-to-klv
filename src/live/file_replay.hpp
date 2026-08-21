// Copyright 2026 Eric Schneider
// Licensed under the Apache License, Version 2.0
#pragma once

#include <optional>
#include <string>

#include "misbklv/types.hpp"
#include "session_metadata.hpp"
#include "transform.hpp"

namespace parrot_to_klv {

struct FileReplayConfig {
    std::string input_path;
    std::string output_sink;  // udp:… , srt:…, or file:… (udp/srt are the replay case)
    TransformOptions transform_options;
    std::optional<SessionMetadata> session;
    int udp_ttl = 1;
    std::string udp_iface;
    bool udp_loop = true;
    bool repeat = false;
    // When looping, how PTS behaves across the loop boundary. `continue` means
    // the next repetition picks up where the previous left off (monotonic
    // cumulative PTS); `reset` restarts at zero each pass. Only relevant when
    // `repeat` is true; `reset` is the default — it matches the per-loop
    // video re-mux from PTS 0 (see PR #58 review), while `continue` would
    // desync KLV from the per-loop video until a single-mux implementation.
    bool repeat_pts_continue = false;
};

misbklv::Result<std::monostate> replay_file_to_live(const FileReplayConfig& cfg);

}  // namespace parrot_to_klv
