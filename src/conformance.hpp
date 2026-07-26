// Copyright 2026 Eric Schneider
// Licensed under the Apache License, Version 2.0

#pragma once

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace parrot_to_klv {

/// A conformance report: what a real Parrot recording told us about the format,
/// in a form that can be committed.
///
/// The point is to get real-encoder evidence into the repository without the
/// recording. Everything here is deliberately non-identifying — see
/// `kReportSchemaVersion` and the invariant below.
///
/// **Invariant: no field of this struct may carry position, absolute time, or
/// device identity.** Latitude, longitude, altitude, UTC timestamps, serial
/// numbers and friendly names are excluded by construction, not by filtering:
/// nothing that reads them ever writes here. Range checks appear as booleans
/// ("latitude was within ±90") never as values. If you add a field, ask what an
/// adversary holding the report alone could locate.
struct ConformanceReport {
    std::string label;        ///< caller-supplied name for the recording
    std::string input_hash; ///< xxHash XXH3_128 identifying the file
    uint64_t input_bytes = 0;

    std::string format;  ///< "v3", "proto", ...
    uint32_t sample_count = 0;
    uint32_t decoded = 0;
    uint32_t failed = 0;
    std::map<std::string, uint32_t> decode_errors;  ///< error text -> count

    /// Sample size in bytes -> how many samples had it. A real encoder's size
    /// distribution is itself a fact worth pinning.
    std::map<uint32_t, uint32_t> sample_size_histogram;

    /// Extension id -> the distinct ext_length values seen (in 32-bit words).
    /// This is what identifies an undocumented extension's shape.
    std::map<uint16_t, std::set<uint16_t>> extension_lengths;
    std::map<uint16_t, uint32_t> extension_counts;

    /// Structural assertions: name -> held for every sample.
    std::map<std::string, bool> checks;

    /// Quaternion norms, which should sit at 1. The strongest single signal
    /// that the field offsets and fixed-point scales are right.
    double quat_norm_min = 0.0;
    double quat_norm_max = 0.0;

    /// Benign scalar ranges — aircraft state, not place.
    int sv_count_min = 0, sv_count_max = 0;
    int battery_min = 0, battery_max = 0;
    int rssi_min = 0, rssi_max = 0;
    std::set<std::string> flying_states_seen;
    std::set<std::string> piloting_modes_seen;
    std::set<std::string> fov_seen;  ///< "hfov x vfov", a camera property

    /// Frame-to-frame timestamp deltas in µs. Relative, so no wall clock.
    uint64_t timestamp_delta_min = 0;
    uint64_t timestamp_delta_max = 0;

    bool ok = false;
    /// Not a recording this checker applies to (no Parrot track, or not v3).
    /// Distinct from a failure: `data/` holds whatever the user put there, and
    /// an unrelated video is not a regression.
    bool not_applicable = false;
    std::string error;
};

/// Report format version. Bump when fields change so a stale committed report
/// is recognisable rather than silently compared against a new shape.
/// Version 2: Changed the hash from SHA-256 to xxHash XXH3_128 for speed.
/// Version 3: Renamed that field `input_sha256` -> `input_hash`, which is what
/// it had held since v2.
inline constexpr int kReportSchemaVersion = 3;

/// Analyse a recording and build its report. Reads the file; writes nothing.
ConformanceReport run_conformance(const std::string& input, const std::string& label);

/// Render deterministically, so regenerating an unchanged input over an
/// unchanged parser produces byte-identical output and `--check` is meaningful.
std::string to_json(const ConformanceReport& report);

}  // namespace parrot_to_klv
