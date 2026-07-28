// Copyright 2026 Eric Schneider
// Licensed under the Apache License, Version 2.0

#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "session_metadata.hpp"
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

/// What a rendered report is allowed to reveal.
///
/// Both default to false. They are separate flags rather than one "sensitive"
/// switch because they protect different people: the coordinates say where the
/// operator was standing, the serial says whose aircraft it is.
struct Disclosure {
    bool location = false;  ///< Items 13/14 and 40/41
    bool identity = false;  ///< Item 4, the airframe serial
};

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
    /// **Sensitive values are withheld by default**, so a coverage report from
    /// a real flight is safe to paste into a bug report — the same rule the
    /// frame summaries follow. Two distinct things are withheld, because they
    /// are distinct risks: *where the flight was* (Items 13/14 sensor position,
    /// 40/41 target position) and *which airframe flew it* (Item 4, the
    /// serial). Pass a policy that opts in only to what the caller asked for.
    std::string to_text(const Disclosure& disclosure = {}) const;
};

/// Opt-ins that relax the mapping's default "never state what the recording did
/// not say" rule.
///
/// Every member defaults to false, and the default output is what the recording
/// supports and nothing more. Each flag exists because a real consumer cannot
/// use the strict form, and each one is named in the coverage report when it
/// changes an item — a relaxation the report does not admit to would be
/// indistinguishable from data.
///
/// A struct rather than a parameter list: these are all booleans, and adjacent
/// unnamed bools at a call site are a defect waiting to happen.
struct TransformOptions {
    /// Clamp Items 6/7 to their spec ranges instead of emitting the 0x8000
    /// out-of-range sentinel (ADR 0005). Items 90/91 carry the true attitude
    /// either way.
    bool clamp_attitude = false;
    /// Infer Item 11 (Image Source Sensor) for a recording whose format does
    /// not carry the imaging spectrum, from whether the frame carried thermal
    /// metadata (ADR 0011). **This is an inference, not a conversion** — see
    /// `to_0601` below.
    bool infer_image_source = false;
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
/// mappings. Tier 2 (frame center, corner points, slant range) needs a
/// boresight-to-terrain intersection whose fidelity is still an open fork, so
/// those items are reported as omitted rather than approximated silently.
///
/// Item 2 is mandatory in ST 0601 but is omitted when the recording has no
/// trustworthy absolute time (every v3 recording, and any proto frame whose
/// `utc_timestamp_accuracy` is zero). The packet is then assembled without
/// mandatory-item enforcement and the omission is reported. Emitting a
/// confident wrong timestamp would be worse than emitting none.
///
/// **Attitude handling (Items 6/7)**: By default, when platform pitch exceeds
/// ±20° or roll exceeds ±50°, the spec-mandated 0x8000 "out-of-range" sentinel
/// is emitted. Some consumers treat this as a missing value and fail.
/// - `TransformOptions::clamp_attitude`: clamps to spec limits, ensuring numeric
///   values are always present. The clamped item is labeled as such in the
///   coverage report.
///
/// There is no option to emit the true angle here, and there cannot be: ST 0601
/// maps ±limit onto the whole int16 range, so a larger value has no encoding in
/// these items (ADR 0008). Items 90/91 always carry accurate full-range values,
/// which is where a consumer wanting the real attitude should read.
///
/// **Item 11 inference**: `TransformOptions::infer_image_source` fills Item 11
/// for a recording that carries no spectrum field (every v3 recording) by
/// reading the presence of thermal metadata as the signal — see ADR 0011. It is
/// an inference from a proxy, not a conversion of a field, so it is off by
/// default and the inferred item is marked `(inferred)` in the coverage report.
/// It never overrides a spectrum the recording actually states.
///
/// Returns an empty packet if encoding fails; `coverage` still describes what
/// was attempted.
Klv0601Frame to_0601(const TelemetrySnapshot& snapshot,
                     const SessionMetadata* session = nullptr,
                     const TransformOptions& options = {});

/// Fill in `snapshot.utc_timestamp_us` for a recording whose frames carry only
/// a monotonic clock, by anchoring that clock to the session's media date.
///
/// `first_monotonic_us` is the first frame's clock reading, which becomes the
/// zero point: the media date is when the recording started, so frame *n* is
/// `media_date + (clock_n - clock_0)`.
///
/// **The anchor is only as good as its second-granularity source** — the media
/// date is an ISO 8601 string with no sub-second part — so the result is
/// accurate to within a second in absolute terms, while frame-to-frame spacing
/// stays exact. Does nothing if the snapshot already has a trustworthy absolute
/// time, or if the session has no parsable media date.
void anchor_to_session(TelemetrySnapshot& snapshot, const SessionMetadata& session,
                       uint64_t first_monotonic_us);

}  // namespace parrot_to_klv
