// Copyright 2026 Eric Schneider
// Licensed under the Apache License, Version 2.0

#pragma once

#include <cstdint>
#include <string>

#include "transform.hpp"

namespace parrot_to_klv {

/// What one conversion produced.
///
/// `error` empty means the output file was written; anything else is the reason
/// it was not, and the other counts describe how far the run got.
struct MuxReport {
    uint32_t samples_seen = 0;      ///< metadata samples the track declared
    uint32_t frames_emitted = 0;    ///< ST 0601 packets muxed into the output
    uint32_t frames_skipped = 0;    ///< samples that did not decode to a packet
    /// Coverage of the first emitted frame — the mapping is the same for every
    /// frame of a recording, so one report characterises the conversion.
    CoverageReport coverage;
    std::string error;
};

/// Convert `input` (a Parrot MP4) into an MPEG-TS at `output`, carrying the
/// source's video elementary stream plus a synchronous ST 0601 KLV stream.
///
/// The video is **never decoded or re-encoded** — parsed only — so the output
/// holds the same H.264/H.265 the drone recorded, with no generation loss. It
/// is *not* bit-identical: libmisbklv rewrites the timing NALs, stripping
/// Picture Timing SEI and injecting ST 0604 from the KLV sensor timestamp, and
/// repeats parameter sets per IDR. See ADR 0006. Each metadata sample becomes one
/// KLV packet, timestamped with that sample's own presentation time from the
/// start of the file, which is the timeline libmisbklv gives the video branch.
///
/// Samples that fail to decode are skipped and counted rather than aborting the
/// conversion: one corrupt sample in a flight should cost that frame's
/// telemetry, not the file.
///
/// `clamp_attitude`: when true, clamps Items 6/7 to their spec ranges (±20°,
/// ±50°) instead of emitting the 0x8000 "out-of-range" sentinel. Use for
/// consumers that can't handle missing values gracefully.
///
/// `force_emit_attitude`: when true, emits actual values for Items 6/7 even when
/// out-of-spec (extrapolates encoding beyond defined range). **Non-compliant**.
MuxReport mux_to_ts(const std::string& input, const std::string& output,
                    bool clamp_attitude = false, bool force_emit_attitude = false);

}  // namespace parrot_to_klv
