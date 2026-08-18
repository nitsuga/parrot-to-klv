// Copyright 2026 Eric Schneider
// Licensed under the Apache License, Version 2.0

#include "telemetry.hpp"

#include <cmath>
#include <cstdint>
#include <optional>
#include <string>

namespace parrot_to_klv {
namespace {

constexpr double kRadToDeg = 180.0 / 3.14159265358979323846;

/// Map the binary piloting mode by name. The numeric values differ from
/// protobuf's, which is exactly why this is a switch and not a cast.
CommonPilotingMode common_mode(PilotingMode mode) {
    switch (mode) {
        case PilotingMode::Manual: return CommonPilotingMode::Manual;
        case PilotingMode::ReturnHome: return CommonPilotingMode::ReturnHome;
        case PilotingMode::FlightPlan: return CommonPilotingMode::FlightPlan;
        case PilotingMode::Tracking: return CommonPilotingMode::Tracking;
        case PilotingMode::MagicCarpet: return CommonPilotingMode::MagicCarpet;
        case PilotingMode::MoveTo: return CommonPilotingMode::MoveTo;
    }
    return CommonPilotingMode::Unknown;
}

CommonPilotingMode common_mode(ProtoPilotingMode mode) {
    switch (mode) {
        case ProtoPilotingMode::Unknown: return CommonPilotingMode::Unknown;
        case ProtoPilotingMode::Manual: return CommonPilotingMode::Manual;
        case ProtoPilotingMode::ReturnHome: return CommonPilotingMode::ReturnHome;
        case ProtoPilotingMode::FlightPlan: return CommonPilotingMode::FlightPlan;
        case ProtoPilotingMode::Tracking: return CommonPilotingMode::Tracking;
        case ProtoPilotingMode::MoveTo: return CommonPilotingMode::MoveTo;
        case ProtoPilotingMode::MagicCarpet: return CommonPilotingMode::MagicCarpet;
    }
    return CommonPilotingMode::Unknown;
}

/// Map the protobuf spectrum by name, as with the piloting modes.
ImageSpectrum common_spectrum(CameraSpectrum spectrum) {
    switch (spectrum) {
        case CameraSpectrum::Unknown: return ImageSpectrum::Unknown;
        case CameraSpectrum::Visible: return ImageSpectrum::Visible;
        case CameraSpectrum::Blended: return ImageSpectrum::Blended;
        case CameraSpectrum::Thermal: return ImageSpectrum::Thermal;
    }
    return ImageSpectrum::Unknown;
}

/// Map the protobuf camera subtype by name.
ImageSubtype common_subtype(CameraSubtype subtype) {
    switch (subtype) {
        case CameraSubtype::Unknown: return ImageSubtype::Unknown;
        case CameraSubtype::Left: return ImageSubtype::Left;
        case CameraSubtype::Right: return ImageSubtype::Right;
        case CameraSubtype::Wide: return ImageSubtype::Wide;
        case CameraSubtype::Tele: return ImageSubtype::Tele;
        case CameraSubtype::Disparity: return ImageSubtype::Disparity;
        case CameraSubtype::Depth: return ImageSubtype::Depth;
    }
    return ImageSubtype::Unknown;
}

/// Proto's in-band sentinel: zero altitude means "unknown", since proto3 gives
/// scalars no presence bit. Treating it as a real 0 m would put the aircraft at
/// sea level.
std::optional<double> altitude_or_unknown(double meters) {
    if (meters == 0.0) return std::nullopt;
    return meters;
}

}  // namespace

std::string to_string(CommonPilotingMode mode) {
    switch (mode) {
        case CommonPilotingMode::Unknown: return "unknown";
        case CommonPilotingMode::Manual: return "manual";
        case CommonPilotingMode::ReturnHome: return "return-home";
        case CommonPilotingMode::FlightPlan: return "flight-plan";
        case CommonPilotingMode::Tracking: return "tracking";
        case CommonPilotingMode::MagicCarpet: return "magic-carpet";
        case CommonPilotingMode::MoveTo: return "move-to";
    }
    return "invalid";
}

std::string to_string(ImageSpectrum spectrum) {
    switch (spectrum) {
        case ImageSpectrum::Unknown: return "unknown";
        case ImageSpectrum::Visible: return "visible";
        case ImageSpectrum::Blended: return "blended";
        case ImageSpectrum::Thermal: return "thermal";
    }
    return "invalid";
}

std::string to_string(ImageSubtype subtype) {
    switch (subtype) {
        case ImageSubtype::Unknown: return "unknown";
        case ImageSubtype::Left: return "left";
        case ImageSubtype::Right: return "right";
        case ImageSubtype::Wide: return "wide";
        case ImageSubtype::Tele: return "tele";
        case ImageSubtype::Disparity: return "disparity";
        case ImageSubtype::Depth: return "depth";
    }
    return "invalid";
}

std::optional<std::string> image_source_sensor(ImageSpectrum spectrum, ImageSubtype subtype) {
    std::string name;
    switch (spectrum) {
        case ImageSpectrum::Visible: name = "EO"; break;
        case ImageSpectrum::Thermal: name = "IR"; break;
        // Item 11 is free-form utf8, so a fused image can be named for what it
        // is instead of being mislabeled as one of its halves.
        case ImageSpectrum::Blended: name = "EO/IR Blended"; break;
        // No stem, no name: a bare "Wide" would not tell a consumer whether the
        // imagery is visible or thermal, which is the point of the item.
        case ImageSpectrum::Unknown: return std::nullopt;
    }
    switch (subtype) {
        case ImageSubtype::Left: name += " Left"; break;
        case ImageSubtype::Right: name += " Right"; break;
        case ImageSubtype::Wide: name += " Wide"; break;
        case ImageSubtype::Tele: name += " Tele"; break;
        case ImageSubtype::Disparity: name += " Disparity"; break;
        case ImageSubtype::Depth: name += " Depth"; break;
        case ImageSubtype::Unknown: break;  // stem stands alone
    }
    return name;
}

TelemetrySnapshot from_v3(const ParrotV3Frame& f) {
    TelemetrySnapshot s;
    s.source_format = ParrotFormat::V3;

    // v3 has no wall clock of its own; anchoring the monotonic one needs
    // session media_date, which nothing parses yet. Left absent rather than
    // guessed — see the open question in field-mapping-analysis.
    if (f.frame_timestamp_us) s.monotonic_us = *f.frame_timestamp_us;

    s.latitude_deg = f.latitude_deg;
    s.longitude_deg = f.longitude_deg;
    s.altitude_amsl_m = f.altitude_amsl_m;
    // No ellipsoid height in v3: Item 75 would need an EGM96 lookup.
    s.sv_count = f.sv_count;
    s.ground_distance_m = f.ground_distance_m;

    s.drone_quat = f.drone_quat;
    s.frame_quat = f.frame_quat;

    s.north_mps = f.north_speed_mps;
    s.east_mps = f.east_speed_mps;
    s.down_mps = f.down_speed_mps;
    s.air_speed_mps = f.air_speed_mps;  // already absent when unreported

    // Already degrees — the asymmetry with proto is resolved here.
    s.hfov_deg = f.picture_hfov_deg;
    s.vfov_deg = f.picture_vfov_deg;

    s.flying_state = f.flying_state;
    s.piloting_mode = common_mode(f.piloting_mode);
    s.tracking_active = f.piloting_mode == PilotingMode::Tracking;

    // v3 states no spectrum, so `s.spectrum` stays Unknown. The thermal
    // extension is the only trace of which sensor was imaging; carried across
    // for the opt-in Item 11 inference, which is the sole thing that reads it.
    s.thermal_data_present = f.thermal.has_value();

    // ADR 0002's residue: fields 0601 has nowhere to put. Listed only when the
    // frame actually carried them.
    s.dropped_fields.push_back("battery_percentage");
    s.dropped_fields.push_back("wifi_rssi");
    s.dropped_fields.push_back("link_quality");
    s.dropped_fields.push_back("link_goodput");
    s.dropped_fields.push_back("exposure_time");
    s.dropped_fields.push_back("iso_gain");
    s.dropped_fields.push_back("awb_gains");
    if (f.thermal) s.dropped_fields.push_back("thermal");
    if (f.automation) s.dropped_fields.push_back("automation.animation");
    for (uint16_t id : f.skipped_extension_ids) {
        s.dropped_fields.push_back("undecoded extension 0x" + [id] {
            const char* hex = "0123456789abcdef";
            std::string out;
            for (int shift = 12; shift >= 0; shift -= 4) out += hex[(id >> shift) & 0xF];
            return out;
        }());
    }

    // v3's target comes from the automation extension's framing target. It
    // carries no accuracy estimates, so Items 45/46 have no source here.
    if (f.automation) {
        TargetLocation target;
        target.latitude_deg = f.automation->framing_target_latitude_deg;
        target.longitude_deg = f.automation->framing_target_longitude_deg;
        target.altitude_amsl_m = f.automation->framing_target_altitude_m;
        s.target = target;
    }
    return s;
}

TelemetrySnapshot from_proto(const ParrotProtoFrame& f) {
    TelemetrySnapshot s;
    s.source_format = ParrotFormat::Proto;

    s.monotonic_us = f.camera_timestamp_us;
    // Zero accuracy means the UTC stamp is not usable. Emitting it anyway would
    // put a confident wrong time into Item 2, which is mandatory and trusted.
    if (f.utc_timestamp_accuracy != 0 && f.utc_timestamp_us != 0) {
        s.utc_timestamp_us = f.utc_timestamp_us;
    }

    // Prefer the camera's own position; fall back to the drone's. Zero
    // lat/lon is proto3's absent-value sentinel (no scalar presence bit) —
    // when neither location has a fix, leaving both optionals empty stops a
    // GPS-denied frame from reporting a confident 0°N 0°E.
    const bool camera_has_fix =
        f.camera_location.latitude_deg != 0.0 || f.camera_location.longitude_deg != 0.0;
    const ProtoLocation& location = camera_has_fix ? f.camera_location : f.drone_location;
    const bool has_fix = location.latitude_deg != 0.0 || location.longitude_deg != 0.0;
    if (has_fix) {
        s.latitude_deg = location.latitude_deg;
        s.longitude_deg = location.longitude_deg;
    }
    s.altitude_amsl_m = altitude_or_unknown(location.altitude_egm96_m);
    // The reason proto is worth parsing separately: both datums, no geoid model.
    s.altitude_wgs84_m = altitude_or_unknown(location.altitude_wgs84_m);
    s.sv_count = location.sv_count;
    // Same proto3 sentinel: a landed drone can genuinely read 0 m AGL, but
    // wire-identical to "not reported", same tradeoff this project already
    // accepts for altitude, zoom, and the FOVs below.
    if (f.ground_distance_m != 0.0) s.ground_distance_m = f.ground_distance_m;

    s.drone_quat = f.drone_quat;
    s.frame_quat = f.frame_quat;

    s.north_mps = f.speed.north_mps;
    s.east_mps = f.speed.east_mps;
    s.down_mps = f.speed.down_mps;
    // Proto carries no air speed at all — distinct from v3 reporting "no data".

    // Radians on the wire, degrees from here on.
    if (f.picture_hfov_rad != 0.0) s.hfov_deg = f.picture_hfov_rad * kRadToDeg;
    if (f.picture_vfov_rad != 0.0) s.vfov_deg = f.picture_vfov_rad * kRadToDeg;
    if (f.zoom_level != 0.0) s.zoom_level = f.zoom_level;
    s.spectrum = common_spectrum(f.spectrum);
    s.subtype = common_subtype(f.subtype);

    s.flying_state = f.flying_state;
    s.piloting_mode = common_mode(f.piloting_mode);
    s.tracking_active =
        f.tracking.has_value() && f.tracking->state == TrackingState::Tracking;

    s.dropped_fields.push_back("battery_percentage");
    if (!f.wifi_links.empty()) {
        s.dropped_fields.push_back("wifi rssi/quality/goodput");
    }
    s.dropped_fields.push_back("exposure_time");
    s.dropped_fields.push_back("iso_gain");
    s.dropped_fields.push_back("awb_gains");
    s.dropped_fields.push_back("principal_point");
    if (f.altitude_ato_m != 0.0) s.dropped_fields.push_back("altitude_ato");
    s.dropped_fields.push_back("position (NED)");
    if (f.tracking) s.dropped_fields.push_back("tracking.quality/cookie");
    if (!f.lfic.empty()) s.dropped_fields.push_back("lfic.grid_precision");
    for (uint32_t number : f.skipped_field_numbers) {
        s.dropped_fields.push_back("undecoded field " + std::to_string(number));
    }

    // LFIC *is* cursor-on-target. Prefer a COT entry when several are present;
    // otherwise take the first, which is still a target the operator marked.
    if (!f.lfic.empty()) {
        const ProtoLfic* chosen = &f.lfic.front();
        for (const auto& entry : f.lfic) {
            if (entry.type == LficType::Cot) {
                chosen = &entry;
                break;
            }
        }
        TargetLocation target;
        target.latitude_deg = chosen->location.latitude_deg;
        target.longitude_deg = chosen->location.longitude_deg;
        target.altitude_amsl_m = chosen->location.altitude_egm96_m;
        if (chosen->horizontal_radial_accuracy_m != 0.0) {
            target.radial_accuracy_m = chosen->horizontal_radial_accuracy_m;
        }
        if (chosen->horizontal_tangential_accuracy_m != 0.0) {
            target.tangential_accuracy_m = chosen->horizontal_tangential_accuracy_m;
        }
        s.target = target;
    }
    return s;
}

}  // namespace parrot_to_klv
