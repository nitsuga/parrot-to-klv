// Copyright 2026 Eric Schneider
// Licensed under the Apache License, Version 2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace parrot_to_klv {

/// Parrot's sentinel for "SV count not available" — VMETA_LOCATION_INVALID_SV_COUNT
/// ((uint8_t)-1) == 0xFF. Header decouples count from valid flag: 0xFF means
/// count unavailable, not "no fix". v3 packs only altitude+count and offers no
/// validity signal other than sv_count, so 0xFF leaves nothing to trust — fail
/// closed (withhold position). Cost: a real fix whose count is unreported loses
/// position.
inline constexpr uint8_t kVmetaInvalidSvCount = 0xFF;

/// Identifier of the v3 base structure ('P3').
inline constexpr uint16_t kParrotV3Id = 0x5033;

/// Extension identifiers. The reference defines these four and no others; in
/// particular there is no binary LFIC extension — LFIC exists only in the
/// protobuf schema.
inline constexpr uint16_t kExtTimestamp = 0x4531;   ///< 'E1', v2 and v3
inline constexpr uint16_t kExtFollowMe = 0x4532;    ///< 'E2', v2 only
inline constexpr uint16_t kExtAutomation = 0x4533;  ///< 'E3', v3 only
inline constexpr uint16_t kExtThermal = 0x4534;     ///< 'E4'

/// Flying state. v3 extends the v1/v2 set with the last three.
enum class FlyingState : uint8_t {
    Landed = 0,
    TakingOff = 1,
    Hovering = 2,
    Flying = 3,
    Landing = 4,
    Emergency = 5,
    UserTakeoff = 6,
    MotorRamping = 7,
    EmergencyLanding = 8,
};

/// Piloting mode.
///
/// These are the *binary* format's values. The protobuf schema numbers the same
/// concepts differently (it has a leading UNKNOWN, and swaps the last two), so
/// the two must never be mixed.
enum class PilotingMode : uint8_t {
    Manual = 0,
    ReturnHome = 1,
    FlightPlan = 2,
    Tracking = 3,  ///< also known as follow-me
    MagicCarpet = 4,
    MoveTo = 5,
};

std::string to_string(FlyingState state);
std::string to_string(PilotingMode mode);

/// Scalar-first quaternion (W, X, Y, Z).
struct Quaternion {
    double w = 1.0;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;

    /// Norm, which a well-formed quaternion keeps near 1. Useful as a decode
    /// sanity check: a wrong field offset almost always breaks it.
    double norm() const;
};

/// Automation extension (0x4533).
struct AutomationExt {
    double framing_target_latitude_deg = 0.0;
    double framing_target_longitude_deg = 0.0;
    double framing_target_altitude_m = 0.0;  ///< ASL
    double flight_destination_latitude_deg = 0.0;
    double flight_destination_longitude_deg = 0.0;
    double flight_destination_altitude_m = 0.0;  ///< ASL
    uint8_t animation = 0;                       ///< 0 = none in progress
    bool follow_me = false;
    bool look_at_me = false;
    bool angle_locked = false;
};

/// One thermal spot. Coordinates are relative to frame width/height.
struct ThermalSpot {
    double x = 0.0;
    double y = 0.0;
    double temperature_k = 0.0;
    bool valid = false;
};

/// Thermal extension (0x4534).
struct ThermalExt {
    ThermalSpot min;
    ThermalSpot max;
    ThermalSpot probe;
    uint8_t calibration_state = 0;
};

/// One decoded v3 metadata sample.
///
/// Fixed-point fields are converted to SI units at parse time. Angles stay in
/// the units the wire format uses — radians for quaternions, degrees for
/// latitude/longitude and the FOVs — so that unit conversion happens once, in
/// the transform stage, rather than being spread across layers.
struct ParrotV3Frame {
    // --- base structure ---
    double ground_distance_m = 0.0;
    double latitude_deg = 0.0;
    double longitude_deg = 0.0;
    double altitude_amsl_m = 0.0;  ///< EGM96 geoid
    uint8_t sv_count = 0;          ///< GPS satellite vehicles used

    double north_speed_mps = 0.0;
    double east_speed_mps = 0.0;
    double down_speed_mps = 0.0;       ///< NED: positive is *down*
    std::optional<double> air_speed_mps;  ///< absent when the drone reports no data

    Quaternion drone_quat;
    Quaternion frame_base_quat;  ///< frame view without user pan/tilt
    Quaternion frame_quat;       ///< frame view

    double exposure_time_ms = 0.0;
    uint16_t gain = 0;  ///< ISO gain
    double awb_r_gain = 0.0;
    double awb_b_gain = 0.0;
    double picture_hfov_deg = 0.0;  ///< already degrees, unlike the proto format
    double picture_vfov_deg = 0.0;

    uint32_t link_goodput_kbps = 0;
    uint8_t link_quality = 0;  ///< 0..5, 5 best
    int8_t wifi_rssi_dbm = 0;
    uint8_t battery_percentage = 0;

    FlyingState flying_state = FlyingState::Landed;
    PilotingMode piloting_mode = PilotingMode::Manual;
    bool binning = false;
    bool animation_in_progress = false;

    // --- extensions ---
    std::optional<uint64_t> frame_timestamp_us;  ///< 0x4531, monotonic
    std::optional<AutomationExt> automation;     ///< 0x4533
    std::optional<ThermalExt> thermal;           ///< 0x4534

    /// Extension ids present but not decoded. Skipping unknown extensions is
    /// required by the format; recording them keeps that visible rather than
    /// silent.
    std::vector<uint16_t> skipped_extension_ids;
};

/// Why a sample failed to parse.
enum class ParrotV3Error {
    TooShort,           ///< fewer bytes than the base structure needs
    BadIdentifier,      ///< leading id is not 0x5033
    LengthMismatch,     ///< declared length disagrees with the buffer
    InvalidQuaternion,  ///< quaternion norm deviates from unit norm
};

std::string to_string(ParrotV3Error error);

/// Decode one v3 metadata sample.
///
/// `sample` is one MP4 metadata-track sample, big-endian as written. Returns
/// the error rather than a partial frame — a sample that does not decode
/// cleanly is not telemetry we should map.
std::optional<ParrotV3Frame> parse_v3(std::span<const uint8_t> sample,
                                      ParrotV3Error* error = nullptr);

/// Size of the v3 base structure, extensions excluded.
inline constexpr size_t kParrotV3BaseSize = 72;

/// Minimum payload size of the v3 automation extension: 6 x int32, 2 x uint8,
/// and one uint16 reserved field.
inline constexpr size_t kParrotV3AutomationPayloadSize = 28;

}  // namespace parrot_to_klv
