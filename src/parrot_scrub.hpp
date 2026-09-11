// Copyright 2026 Eric Schneider
// Licensed under the Apache License, Version 2.0

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace parrot_to_klv {

/// Valid coordinate and altitude limits for scrub operations.
///
/// Altitude bounds match what Parrot's signed 24-bit Q16.8 base frame
/// ([-8388608, 8388607] / 256.0) and signed 32-bit Q16.16 automation extension
/// can represent without integer overflow.
inline constexpr double kMinLatitudeDeg = -90.0;
inline constexpr double kMaxLatitudeDeg = 90.0;
inline constexpr double kMinLongitudeDeg = -180.0;
inline constexpr double kMaxLongitudeDeg = 180.0;
inline constexpr double kMinAltitudeAmslM = -32768.0;
inline constexpr double kMaxAltitudeAmslM = 8388607.0 / 256.0;  // 32767.99609375

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
    /// safe to publish; it does nothing about a recognizable skyline. Set this
    /// when the output is destined to be committed.
    bool zero_media = false;

    /// Blank the payload of every extension this tool does not understand,
    /// keeping its id and length so parsers still skip it correctly.
    ///
    /// On by default, and it must stay that way for the tool's promise to hold.
    /// Real ANAFI recordings carry a 0x4536 extension in every sample that the
    /// reference documentation does not describe at all; if it holds coordinates
    /// — plausible, since the protobuf schema's LFIC does — then passing it
    /// through would leak the location out of a file reported as scrubbed.
    /// An extension we cannot read is an extension we cannot clear.
    ///
    /// Clear this only to study what an unknown extension contains, and treat
    /// the output as unscrubbed.
    bool zero_unknown_extensions = true;
};

struct ScrubReport {
    uint32_t samples_rewritten = 0;
    uint32_t automation_extensions_rewritten = 0;
    uint32_t geotag_boxes_rewritten = 0;
    /// '©day' (media date), '©too' (serial), '©ART' (friendly name), '©nam'
    /// (title) and '©cpy' (copyright) boxes rewritten.
    uint32_t identity_boxes_rewritten = 0;
    uint64_t media_bytes_zeroed = 0;
    uint32_t unknown_extension_payloads_zeroed = 0;
    /// Distinct extension ids encountered that this tool does not decode.
    std::vector<uint16_t> unknown_extension_ids;
    bool ok = false;
    std::string error;

    /// Things found that this tool does not scrub. Non-empty means the output
    /// still needs a human's judgment before it is shared.
    std::vector<std::string> warnings;
};

/// Copy `input` to `output`, replacing location-bearing and identity-bearing
/// fields with synthetic values.
///
/// Byte-for-byte copy plus in-place field overwrites: sample sizes never change,
/// so every box, table and offset in the file survives untouched. That keeps the
/// output a faithful specimen of the original encoder, which is the whole point
/// of scrubbing a real recording rather than synthesizing one.
///
/// Refuses the operation if `input` and `output` identify the same file,
/// including through normalized paths, symbolic links, or hard links. Scrubbing
/// in place is not supported.
///
/// Refuses any format it cannot scrub. A file that this tool cannot fully handle
/// must not leave here looking safe.
ScrubReport scrub_file(const std::string& input, const std::string& output,
                       const ScrubOptions& options);

}  // namespace parrot_to_klv
