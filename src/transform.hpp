// Copyright 2026 Eric Schneider
// Licensed under the Apache License, Version 2.0

#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "telemetry.hpp"

namespace parrot_to_klv {

/// Euler angles in degrees, as ST 0601 wants them.
struct Euler {
    double yaw_deg = 0.0;    ///< [0, 360)
    double pitch_deg = 0.0;  ///< [-90, 90]
    double roll_deg = 0.0;   ///< [-180, 180]
};

/// Quaternion → Euler, per [[field-mapping-analysis]] § Conversions.
/// Yaw is wrapped to [0, 360) because that is the range 0601 Items 5/18 use.
Euler to_euler(const Quaternion& q);

/// Gimbal orientation relative to the airframe: the frame quaternion composed
/// with the conjugate of the drone quaternion (Items 18/19/20).
Euler relative_euler(const Quaternion& drone, const Quaternion& frame);

/// Why an ST 0601 item the mapping knows about was not emitted for a frame.
///
/// ADR 0002 requires conversion loss to be *reported* rather than implied, and
/// an omitted item is loss just as much as a dropped field is.
enum class OmissionReason {
    NoSourceInFormat,   ///< the recording's format never carries it
    SourceAbsent,       ///< the format carries it, this frame did not
    NeedsDerivation,    ///< Tier 2 geometry — not implemented (open fork)
    NeedsSessionData,   ///< MP4 session metadata, which nothing parses yet
    Unresolved,         ///< the mapping itself is an open question
};

std::string to_string(OmissionReason reason);

/// What one conversion carried, and what it did not.
///
/// This is ADR 0002's "conversion coverage report" — a first-class output, not
/// a debug log. A user who wants battery telemetry has to be able to learn from
/// the tool that it was dropped, rather than inferring it downstream.
struct CoverageReport {
    /// ST 0601 tags emitted, in tag order, with the value rendered for display.
    std::map<uint16_t, std::string> emitted;
    /// Tags the mapping knows about but did not emit, and why.
    std::map<uint16_t, OmissionReason> omitted;
    /// Parrot fields with no ST 0601 item at all (ADR 0002's residue), named as
    /// the Parrot schema names them. Populated only for fields this frame
    /// actually carried, so the report describes the recording, not the schema.
    std::vector<std::string> dropped_fields;

    /// Human-readable summary, one line per section.
    ///
    /// **Location is withheld by default.** Items 13/14 (sensor latitude and
    /// longitude) and 40/41 (target latitude and longitude) are rendered as
    /// `withheld` unless `with_location` is set, so a coverage report from a
    /// real flight is safe to paste into a bug report — the same rule the
    /// frame summaries follow. Pass true only when the caller has asked to see
    /// the coordinates.
    std::string to_text(bool with_location = false) const;
};

/// One frame mapped to ST 0601.
struct Klv0601Frame {
    /// Encoded packet: UL key, BER length, items, checksum.
    std::vector<uint8_t> packet;
    CoverageReport coverage;
};

/// Map one telemetry snapshot to an ST 0601 packet.
///
/// Covers Tier 1 of [[field-mapping-analysis]] — the direct field-to-field
/// mappings. Tier 2 (frame centre, corner points, slant range) needs a
/// boresight-to-terrain intersection whose fidelity is still an open fork, so
/// those items are reported as omitted rather than approximated silently.
///
/// Item 2 is mandatory in ST 0601 but is omitted when the recording has no
/// trustworthy absolute time (every v3 recording, and any proto frame whose
/// `utc_timestamp_accuracy` is zero). The packet is then assembled without
/// mandatory-item enforcement and the omission is reported. Emitting a
/// confident wrong timestamp would be worse than emitting none.
///
/// Returns an empty packet if encoding fails; `coverage` still describes what
/// was attempted.
Klv0601Frame to_0601(const TelemetrySnapshot& snapshot);

}  // namespace parrot_to_klv
