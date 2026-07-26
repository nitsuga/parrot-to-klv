// Copyright 2026 Eric Schneider
// Licensed under the Apache License, Version 2.0

#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "parrot_v3.hpp"  // Quaternion, FlyingState — shared with the binary formats

namespace parrot_to_klv {

/// Identifier of the proto format in an RTP header extension ('Pb').
///
/// Unlike v1/v2/v3 the recording samples carry no leading identifier — a sample
/// *is* a serialized `vmeta.TimedMetadata`. This constant exists for the
/// streaming case and for symmetry with `kParrotV3Id`.
inline constexpr uint16_t kParrotProtoId = 0x5062;

/// Piloting mode as numbered by the **protobuf** schema.
///
/// Deliberately a separate type from the binary `PilotingMode`: the protobuf
/// schema prepends `PM_UNKNOWN` and swaps the last two, so the two enums
/// disagree on every value. Keeping them distinct makes mixing them a compile
/// error rather than a silent mis-decode.
enum class ProtoPilotingMode : uint8_t {
    Unknown = 0,
    Manual = 1,
    ReturnHome = 2,
    FlightPlan = 3,
    Tracking = 4,
    MoveTo = 5,
    MagicCarpet = 6,
};

/// LFIC entry type. `Cot` is cursor-on-target, which the 0601 mapping prefers
/// as the Target Location source when several entries are present.
enum class LficType : uint8_t {
    Cot = 0,
    User = 1,
};

/// Camera spectrum. The only signal in the recording of whether the imagery is
/// visible-light or thermal, which is what ST 0601 Item 11 names.
enum class CameraSpectrum : uint8_t {
    Unknown = 0,
    Visible = 1,
    Blended = 2,  ///< visible and thermal fused into one image
    Thermal = 3,
};

/// Which physical camera produced the image. Qualifies the spectrum when
/// naming the sensor for ST 0601 Item 11.
enum class CameraSubtype : uint8_t {
    Unknown = 0,
    Left = 1,
    Right = 2,
    Wide = 3,
    Tele = 4,
    Disparity = 5,
    Depth = 6,
};

enum class TrackingState : uint8_t {
    Tracking = 0,
    Searching = 1,
    Pause = 2,
};

std::string to_string(ProtoPilotingMode mode);
std::string to_string(CameraSpectrum spectrum);
std::string to_string(CameraSubtype subtype);
std::string to_string(LficType type);
std::string to_string(TrackingState state);

/// A geodetic position as the proto format carries it.
///
/// The proto format supplies **both** altitude datums, which is the main reason
/// it is worth parsing separately from v3: no geoid model is needed. Zero means
/// unknown for either altitude — the schema has no presence wrapper here, so
/// the sentinel is the only signal.
struct ProtoLocation {
    double latitude_deg = 0.0;
    double longitude_deg = 0.0;
    double altitude_wgs84_m = 0.0;   ///< ellipsoidal; 0 means unknown
    double altitude_egm96_m = 0.0;   ///< AMSL/EGM96; 0 means unknown
    double horizontal_accuracy_m = 0.0;
    double vertical_accuracy_m = 0.0;
    uint32_t sv_count = 0;
};

/// North-East-Down vector. Down is positive.
struct ProtoNed {
    double north_mps = 0.0;
    double east_mps = 0.0;
    double down_mps = 0.0;
};

/// One Location-From-Image-Coordinates entry.
struct ProtoLfic {
    double x = 0.0;  ///< image coordinate, normalized
    double y = 0.0;
    ProtoLocation location;
    double grid_precision_m = 0.0;
    LficType type = LficType::Cot;
    double horizontal_tangential_accuracy_m = 0.0;
    double horizontal_radial_accuracy_m = 0.0;
};

/// Tracking target bounding box, normalized to frame width/height.
struct ProtoBoundingBox {
    double x = 0.0;
    double y = 0.0;
    double width = 0.0;
    double height = 0.0;
    uint32_t object_class = 0;
    double confidence = 0.0;
    uint32_t uid = 0;
};

struct ProtoTracking {
    std::optional<ProtoBoundingBox> target;
    uint64_t timestamp_us = 0;
    uint32_t quality = 0;
    TrackingState state = TrackingState::Tracking;
};

struct ProtoWifiLink {
    uint32_t goodput_kbps = 0;
    uint32_t quality = 0;  ///< 0..5, 5 best
    int32_t rssi_dbm = 0;
};

/// One decoded proto (`vmeta.TimedMetadata`) sample.
///
/// Shaped to parallel `ParrotV3Frame` so the transform stage sees one idea of a
/// frame, but it is a distinct type: the proto format carries fields v3 does not
/// (both altitude datums, zoom, LFIC, tracking) and uses different units and
/// enum numbering for some it shares.
///
/// **Units follow the wire**, as in `ParrotV3Frame`: quaternions and the FOVs
/// are radians here — note that v3 carries the FOVs in *degrees* — latitude and
/// longitude are degrees. Conversion happens once, in the transform stage.
struct ParrotProtoFrame {
    // --- drone ---
    Quaternion drone_quat;
    ProtoLocation drone_location;
    double ground_distance_m = 0.0;
    double altitude_ato_m = 0.0;  ///< above take-off
    ProtoNed speed;
    ProtoNed position;
    int32_t battery_percentage = 0;
    FlyingState flying_state = FlyingState::Landed;
    ProtoPilotingMode piloting_mode = ProtoPilotingMode::Unknown;
    bool animation_in_progress = false;

    // --- camera ---
    uint64_t camera_timestamp_us = 0;  ///< monotonic frame clock
    uint64_t utc_timestamp_us = 0;     ///< epoch µs; see accuracy below
    /// Zero means `utc_timestamp_us` is **invalid** — the frame has no usable
    /// absolute time and Item 2 must fall back to the monotonic clock.
    uint32_t utc_timestamp_accuracy = 0;
    Quaternion frame_base_quat;
    Quaternion frame_quat;
    Quaternion frame_local_quat;
    ProtoLocation camera_location;
    double principal_point_x = 0.0;
    double principal_point_y = 0.0;
    double exposure_time_ms = 0.0;
    uint32_t iso_gain = 0;
    double awb_r_gain = 0.0;
    double awb_b_gain = 0.0;
    double picture_hfov_rad = 0.0;  ///< RADIANS — v3 carries degrees
    double picture_vfov_rad = 0.0;  ///< RADIANS — v3 carries degrees
    double zoom_level = 0.0;        ///< multiplier, not a percentage
    CameraSpectrum spectrum = CameraSpectrum::Unknown;
    CameraSubtype subtype = CameraSubtype::Unknown;

    // --- links, LFIC, tracking ---
    std::vector<ProtoWifiLink> wifi_links;
    std::vector<ProtoLfic> lfic;
    std::optional<ProtoTracking> tracking;

    /// Top-level `TimedMetadata` field numbers present but not decoded.
    /// Skipping unknown fields is the whole point of the wire format; recording
    /// them keeps the lossiness visible rather than silent, exactly as
    /// `ParrotV3Frame::skipped_extension_ids` does for the binary formats.
    std::vector<uint32_t> skipped_field_numbers;
};

/// Why a sample failed to parse.
/// There is no depth error: the reader never recurses on itself. Each message
/// parser calls only the specific nested parsers its schema names, so nesting
/// is bounded at compile time (three deep, `TimedMetadata` → `DroneMetadata` →
/// `Location`) and a malformed sample cannot drive it deeper.
enum class ParrotProtoError {
    Truncated,      ///< a field ran past the end of the buffer
    BadWireType,    ///< wire type 3, 4, 6 or 7 — none valid in proto3
    VarintTooLong,  ///< more than 10 continuation bytes
};

std::string to_string(ParrotProtoError error);

/// Decode one proto metadata sample.
///
/// `sample` is one MP4 metadata-track sample: a serialized `vmeta.TimedMetadata`
/// with no framing of its own. Fields the mapping does not use are skipped by
/// wire type, so a recording from a newer Parrot SDK decodes rather than
/// failing.
///
/// Returns the error rather than a partial frame. An empty sample is a *valid*
/// `TimedMetadata` with every field defaulted — proto3 cannot distinguish that
/// from absent — so it decodes to a default frame rather than an error.
std::optional<ParrotProtoFrame> parse_proto(std::span<const uint8_t> sample,
                                            ParrotProtoError* error = nullptr);

}  // namespace parrot_to_klv
