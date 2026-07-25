// Copyright 2026 Eric Schneider
// Licensed under the Apache License, Version 2.0

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace parrot_to_klv {

/// Synthetic values written over the real ones.
struct ScrubValues {
    double latitude_deg = 0.0;
    double longitude_deg = 0.0;
    double altitude_amsl_m = 0.0;
};

struct ScrubOptions {
    ScrubValues values;

    /// Overwrite video and audio sample payloads with zeroes.
    ///
    /// Telemetry is not the only thing in a recording that reveals where it was
    /// taken — the imagery does too. Scrubbing coordinates makes the *metadata*
    /// safe to publish; it does nothing about a recognisable skyline. Set this
    /// when the output is destined to be committed.
    bool zero_media = false;
};

struct ScrubReport {
    uint32_t samples_rewritten = 0;
    uint32_t automation_extensions_rewritten = 0;
    uint32_t geotag_boxes_rewritten = 0;
    uint64_t media_bytes_zeroed = 0;
    bool ok = false;
    std::string error;

    /// Things found that this tool does not scrub. Non-empty means the output
    /// still needs a human's judgement before it is shared.
    std::vector<std::string> warnings;
};

/// Copy `input` to `output`, replacing location-bearing fields with synthetic
/// values.
///
/// Byte-for-byte copy plus in-place field overwrites: sample sizes never change,
/// so every box, table and offset in the file survives untouched. That keeps the
/// output a faithful specimen of the original encoder, which is the whole point
/// of scrubbing a real recording rather than synthesising one.
///
/// Refuses any format it cannot scrub. A file that this tool cannot fully handle
/// must not leave here looking safe.
ScrubReport scrub_file(const std::string& input, const std::string& output,
                       const ScrubOptions& options);

}  // namespace parrot_to_klv
