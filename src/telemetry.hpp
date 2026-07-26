// Copyright 2026 Eric Schneider
// Licensed under the Apache License, Version 2.0

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "mp4_metadata_extractor.hpp"  // ParrotFormat
#include "parrot_proto.hpp"
#include "parrot_v3.hpp"

namespace parrot_to_klv {

/// Piloting mode, normalized across formats.
///
/// The binary and protobuf schemas number these differently — binary `Manual`
/// is 0, protobuf `PM_MANUAL` is 1, and they disagree on the last pair — so
/// both parsers map into this **by name**, per
/// [[field-mapping-analysis]] § Enum maps. Nothing downstream sees a raw number.
enum class CommonPilotingMode : uint8_t {
    Unknown,
    Manual,
    ReturnHome,
    FlightPlan,
    Tracking,
    MagicCarpet,
    MoveTo,
};

/// Imaging spectrum, normalized across formats.
///
/// Named separately from the protobuf `CameraSpectrum` for the same reason
/// `CommonPilotingMode` is: the snapshot is where format-specific types stop.
/// Only proto reports it; v3 leaves it `Unknown`.
enum class ImageSpectrum : uint8_t {
    Unknown,
    Visible,
    Blended,  ///< visible and thermal fused
    Thermal,
};

/// Which physical camera, normalized across formats. Proto only.
enum class ImageSubtype : uint8_t {
    Unknown,
    Left,
    Right,
    Wide,
    Tele,
    Disparity,
    Depth,
};

std::string to_string(CommonPilotingMode mode);
std::string to_string(ImageSpectrum spectrum);
std::string to_string(ImageSubtype subtype);

/// ST 0601 Item 11's value: a name for the active sensor, or nothing when it
/// cannot be named.
///
/// Item 11 is free-form utf8 — "name of currently active sensor" — and the
/// standard's own example configuration names sensors `EO Narrow`, `EO Wide`
/// and `IR Sensor`. So the spectrum gives the stem (`EO`, `IR`, or
/// `EO/IR Blended` for a fused image, which is named for what it is rather than
/// forced into one of its halves) and the subtype qualifies it: `EO Wide`,
/// `IR Tele`.
///
/// An unknown **spectrum** yields nothing even when the subtype is known: a
/// bare `Wide` does not tell a consumer the one thing Item 11 exists to
/// convey. An unknown **subtype** simply leaves the stem unqualified.
std::optional<std::string> image_source_sensor(ImageSpectrum spectrum, ImageSubtype subtype);

/// A target the aircraft is pointing at, if it reports one.
struct TargetLocation {
    double latitude_deg = 0.0;
    double longitude_deg = 0.0;
    double altitude_amsl_m = 0.0;
    /// Parrot's horizontal accuracies. These are *not* literally CE90/LE90 —
    /// see the open question in [[field-mapping-analysis]] — so the transform
    /// records how it treated them rather than reporting them silently.
    std::optional<double> radial_accuracy_m;
    std::optional<double> tangential_accuracy_m;
};

/// One frame of telemetry, format-neutral.
///
/// This is the boundary ADR 0004 draws: per-format knowledge — units, enum
/// numbering, which fields exist — stops here. Angles are **degrees**,
/// velocities m/s, distances metres, regardless of what the wire carried.
/// `std::optional` means "this recording does not supply it", so the mapping's
/// rule is uniformly "omit the item" rather than a per-format special case.
struct TelemetrySnapshot {
    /// Which format this came from. Carried for the coverage report, not for
    /// the mapping — anything the mapping needs to know is in the fields.
    ParrotFormat source_format = ParrotFormat::V3;

    // --- time ---
    /// Absolute time, µs since epoch. Absent when the recording has none we can
    /// trust: proto says so through `utc_timestamp_accuracy == 0`, and v3 has
    /// only a monotonic clock that needs session metadata to anchor.
    std::optional<uint64_t> utc_timestamp_us;
    uint64_t monotonic_us = 0;

    // --- sensor position ---
    std::optional<double> latitude_deg;
    std::optional<double> longitude_deg;
    std::optional<double> altitude_amsl_m;    ///< EGM96
    std::optional<double> altitude_wgs84_m;   ///< ellipsoidal; proto only
    std::optional<uint32_t> sv_count;
    double ground_distance_m = 0.0;           ///< AGL

    // --- attitude ---
    Quaternion drone_quat;
    Quaternion frame_quat;

    // --- velocity, NED (down positive, as both formats carry it) ---
    double north_mps = 0.0;
    double east_mps = 0.0;
    double down_mps = 0.0;
    std::optional<double> air_speed_mps;  ///< v3 only, and only when reported

    // --- camera ---
    /// Degrees. Proto carries radians on the wire; the conversion happens in
    /// `from_proto` so the mapping never has to know which format it came from.
    std::optional<double> hfov_deg;
    std::optional<double> vfov_deg;
    /// Zoom as Parrot reports it: a multiplier, not a percentage. Proto only.
    std::optional<double> zoom_level;
    /// Which spectrum the active camera imaged in. Proto only; the binary
    /// formats carry nothing equivalent.
    ImageSpectrum spectrum = ImageSpectrum::Unknown;
    /// Which physical camera. Proto only; qualifies `spectrum` when naming the
    /// sensor.
    ImageSubtype subtype = ImageSubtype::Unknown;

    // --- state ---
    FlyingState flying_state = FlyingState::Landed;
    CommonPilotingMode piloting_mode = CommonPilotingMode::Unknown;
    bool tracking_active = false;

    // --- target ---
    std::optional<TargetLocation> target;

    /// Parrot fields this frame carried that have **no ST 0601 item at all**,
    /// named as the Parrot schema names them.
    ///
    /// Filled here because normalization is exactly where they are discarded —
    /// ADR 0002 requires that loss be reported rather than implied, and ADR 0004
    /// puts the coverage boundary at this seam. Only fields the frame actually
    /// carried are listed, so the report describes the recording rather than
    /// reciting the schema.
    std::vector<std::string> dropped_fields;
};

/// Normalize a v3 frame.
///
/// v3 carries the FOVs in degrees already, has no ellipsoid height, and reports
/// a monotonic clock rather than wall time — so `altitude_wgs84_m` and
/// `utc_timestamp_us` come back empty by construction, not by accident.
TelemetrySnapshot from_v3(const ParrotV3Frame& frame);

/// Normalize a proto frame.
///
/// Converts the FOVs from radians to degrees, honours the in-band "unknown"
/// sentinels (zero altitude, zero UTC accuracy) by leaving the corresponding
/// optionals empty, and prefers `camera.location` over `drone.location` for the
/// sensor position, per the mapping spec.
TelemetrySnapshot from_proto(const ParrotProtoFrame& frame);

}  // namespace parrot_to_klv
