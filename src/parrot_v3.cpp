// Copyright 2026 Eric Schneider
// Licensed under the Apache License, Version 2.0

#include "parrot_v3.hpp"

#include <cmath>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace parrot_to_klv {
namespace {

// Fixed-point scales. Q<int>.<frac> divides by 2^frac.
constexpr double kQ16_16 = 65536.0;    // 1 << 16
constexpr double kQ10_22 = 4194304.0;  // 1 << 22
constexpr double kQ16_8 = 256.0;       // 1 << 8
constexpr double kQ8_8 = 256.0;        // 1 << 8
constexpr double kQ2_14 = 16384.0;     // 1 << 14
constexpr double kQ11_5 = 32.0;        // 1 << 5

constexpr uint8_t kFlyingStateMask = 0x7F;
constexpr uint8_t kBinningMask = 0x80;
constexpr uint8_t kPilotingModeMask = 0x7F;
constexpr uint8_t kAnimationMask = 0x80;

/// Big-endian readers. The format is network byte order throughout.
uint16_t be16(std::span<const uint8_t> b, size_t off) {
    return static_cast<uint16_t>((static_cast<uint16_t>(b[off]) << 8) | b[off + 1]);
}

uint32_t be32(std::span<const uint8_t> b, size_t off) {
    return (static_cast<uint32_t>(b[off]) << 24) | (static_cast<uint32_t>(b[off + 1]) << 16) |
           (static_cast<uint32_t>(b[off + 2]) << 8) | b[off + 3];
}

int16_t be16s(std::span<const uint8_t> b, size_t off) {
    return static_cast<int16_t>(be16(b, off));
}

int32_t be32s(std::span<const uint8_t> b, size_t off) {
    return static_cast<int32_t>(be32(b, off));
}

Quaternion read_quat(std::span<const uint8_t> b, size_t off) {
    return Quaternion{
        be16s(b, off) / kQ2_14,
        be16s(b, off + 2) / kQ2_14,
        be16s(b, off + 4) / kQ2_14,
        be16s(b, off + 6) / kQ2_14,
    };
}

ThermalSpot read_spot(std::span<const uint8_t> b, size_t off, bool valid) {
    ThermalSpot spot;
    spot.x = be16s(b, off) / kQ11_5;
    spot.y = be16s(b, off + 2) / kQ11_5;
    spot.temperature_k = be16s(b, off + 4) / kQ11_5;
    spot.valid = valid;
    return spot;
}

void parse_automation(std::span<const uint8_t> payload, ParrotV3Frame& frame) {
    // 6 x int32 + 2 x uint8 + uint16 reserved.
    if (payload.size() < 28) {
        return;
    }
    AutomationExt ext;
    ext.framing_target_latitude_deg = be32s(payload, 0) / kQ10_22;
    ext.framing_target_longitude_deg = be32s(payload, 4) / kQ10_22;
    ext.framing_target_altitude_m = be32s(payload, 8) / kQ16_16;
    ext.flight_destination_latitude_deg = be32s(payload, 12) / kQ10_22;
    ext.flight_destination_longitude_deg = be32s(payload, 16) / kQ10_22;
    ext.flight_destination_altitude_m = be32s(payload, 20) / kQ16_16;
    ext.animation = payload[24];
    const uint8_t flags = payload[25];
    ext.follow_me = (flags & 0x01) != 0;
    ext.look_at_me = (flags & 0x02) != 0;
    ext.angle_locked = (flags & 0x04) != 0;
    frame.automation = ext;
}

void parse_thermal(std::span<const uint8_t> payload, ParrotV3Frame& frame) {
    // 9 x int16 + 2 x uint8.
    if (payload.size() < 20) {
        return;
    }
    const uint8_t flags = payload[19];
    ThermalExt ext;
    ext.min = read_spot(payload, 0, (flags & 0x01) != 0);
    ext.max = read_spot(payload, 6, (flags & 0x02) != 0);
    ext.probe = read_spot(payload, 12, (flags & 0x04) != 0);
    ext.calibration_state = payload[18];
    frame.thermal = ext;
}

void parse_extensions(std::span<const uint8_t> ext_area, ParrotV3Frame& frame) {
    size_t off = 0;
    // Each extension is ext_id(2) + ext_length(2) + payload, 4-byte aligned.
    while (off + 4 <= ext_area.size()) {
        const uint16_t id = be16(ext_area, off);
        const size_t payload_bytes = static_cast<size_t>(be16(ext_area, off + 2)) * 4;
        const size_t body = off + 4;
        if (body + payload_bytes > ext_area.size()) {
            // Truncated extension: stop rather than read past the sample.
            frame.skipped_extension_ids.push_back(id);
            return;
        }
        const auto payload = ext_area.subspan(body, payload_bytes);

        switch (id) {
            case kExtTimestamp:
                if (payload.size() >= 8) {
                    frame.frame_timestamp_us =
                        (static_cast<uint64_t>(be32(payload, 0)) << 32) | be32(payload, 4);
                }
                break;
            case kExtAutomation:
                parse_automation(payload, frame);
                break;
            case kExtThermal:
                parse_thermal(payload, frame);
                break;
            default:
                // Unknown extensions must be skipped, not treated as an error.
                frame.skipped_extension_ids.push_back(id);
                break;
        }

        off = body + payload_bytes;
    }
}

}  // namespace

double Quaternion::norm() const {
    return std::sqrt(w * w + x * x + y * y + z * z);
}

std::string to_string(FlyingState state) {
    switch (state) {
        case FlyingState::Landed: return "landed";
        case FlyingState::TakingOff: return "taking-off";
        case FlyingState::Hovering: return "hovering";
        case FlyingState::Flying: return "flying";
        case FlyingState::Landing: return "landing";
        case FlyingState::Emergency: return "emergency";
        case FlyingState::UserTakeoff: return "user-takeoff";
        case FlyingState::MotorRamping: return "motor-ramping";
        case FlyingState::EmergencyLanding: return "emergency-landing";
    }
    return "unknown";
}

std::string to_string(PilotingMode mode) {
    switch (mode) {
        case PilotingMode::Manual: return "manual";
        case PilotingMode::ReturnHome: return "return-home";
        case PilotingMode::FlightPlan: return "flight-plan";
        case PilotingMode::Tracking: return "tracking";
        case PilotingMode::MagicCarpet: return "magic-carpet";
        case PilotingMode::MoveTo: return "move-to";
    }
    return "unknown";
}

std::string to_string(ParrotV3Error error) {
    switch (error) {
        case ParrotV3Error::TooShort: return "sample shorter than the v3 base structure";
        case ParrotV3Error::BadIdentifier: return "leading identifier is not 0x5033";
        case ParrotV3Error::LengthMismatch: return "declared length disagrees with the sample";
    }
    return "unknown error";
}

std::optional<ParrotV3Frame> parse_v3(std::span<const uint8_t> sample, ParrotV3Error* error) {
    const auto fail = [&](ParrotV3Error e) -> std::optional<ParrotV3Frame> {
        if (error) *error = e;
        return std::nullopt;
    };

    if (sample.size() < kParrotV3BaseSize) {
        return fail(ParrotV3Error::TooShort);
    }
    if (be16(sample, 0) != kParrotV3Id) {
        return fail(ParrotV3Error::BadIdentifier);
    }

    // `length` counts 32-bit words after the id/length pair, extensions included.
    const size_t declared = 4 + static_cast<size_t>(be16(sample, 2)) * 4;
    if (declared < kParrotV3BaseSize || declared > sample.size()) {
        return fail(ParrotV3Error::LengthMismatch);
    }

    ParrotV3Frame frame;
    frame.ground_distance_m = be32s(sample, 4) / kQ16_16;
    frame.latitude_deg = be32s(sample, 8) / kQ10_22;
    frame.longitude_deg = be32s(sample, 12) / kQ10_22;

    // Bits 31..8 are altitude Q16.8, bits 7..0 the satellite count. The shift is
    // arithmetic on a signed value, which is what sign-extends the altitude.
    const int32_t altitude_and_sv = be32s(sample, 16);
    frame.altitude_amsl_m = (altitude_and_sv >> 8) / kQ16_8;
    frame.sv_count = static_cast<uint8_t>(altitude_and_sv & 0xFF);

    frame.north_speed_mps = be16s(sample, 20) / kQ8_8;
    frame.east_speed_mps = be16s(sample, 22) / kQ8_8;
    frame.down_speed_mps = be16s(sample, 24) / kQ8_8;

    // Negative air speed is the documented "no data" signal, not a value.
    const int16_t air_speed = be16s(sample, 26);
    if (air_speed >= 0) {
        frame.air_speed_mps = air_speed / kQ8_8;
    }

    frame.drone_quat = read_quat(sample, 28);
    frame.frame_base_quat = read_quat(sample, 36);
    frame.frame_quat = read_quat(sample, 44);

    frame.exposure_time_ms = be16(sample, 52) / kQ8_8;
    frame.gain = be16(sample, 54);
    frame.awb_r_gain = be16(sample, 56) / kQ2_14;
    frame.awb_b_gain = be16(sample, 58) / kQ2_14;
    frame.picture_hfov_deg = be16(sample, 60) / kQ8_8;
    frame.picture_vfov_deg = be16(sample, 62) / kQ8_8;

    const uint32_t link = be32(sample, 64);
    frame.link_goodput_kbps = link >> 8;
    frame.link_quality = static_cast<uint8_t>(link & 0xFF);

    frame.wifi_rssi_dbm = static_cast<int8_t>(sample[68]);
    frame.battery_percentage = sample[69];

    const uint8_t state = sample[70];
    const uint8_t mode = sample[71];
    frame.flying_state = static_cast<FlyingState>(state & kFlyingStateMask);
    frame.binning = (state & kBinningMask) != 0;
    frame.piloting_mode = static_cast<PilotingMode>(mode & kPilotingModeMask);
    frame.animation_in_progress = (mode & kAnimationMask) != 0;

    // Extensions run from the end of the base structure to the declared end.
    if (declared > kParrotV3BaseSize) {
        parse_extensions(sample.subspan(kParrotV3BaseSize, declared - kParrotV3BaseSize), frame);
    }

    return frame;
}

}  // namespace parrot_to_klv
