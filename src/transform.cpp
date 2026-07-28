// Copyright 2026 Eric Schneider
// Licensed under the Apache License, Version 2.0

#include "transform.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "misbklv/builder.hpp"
#include "misbklv/packet.hpp"
#include "misbklv/registries.hpp"

namespace parrot_to_klv {
namespace {

constexpr double kRadToDeg = 180.0 / 3.14159265358979323846;

/// Below this ground speed, course angle (Item 112) is undefined — atan2 of two
/// numbers that are both noise. The mapping spec says omit rather than emit a
/// direction the aircraft is not travelling in.
constexpr double kCourseSpeedThresholdMps = 0.5;

/// Wrap to [0, 360), the range Items 5/18/20 and 112 use.
double wrap_360(double degrees) {
    double wrapped = std::fmod(degrees, 360.0);
    if (wrapped < 0.0) wrapped += 360.0;
    return wrapped;
}

/// Conjugate — the inverse rotation, since Parrot quaternions are unit norm.
Quaternion conjugate(const Quaternion& q) {
    return Quaternion{q.w, -q.x, -q.y, -q.z};
}

/// Hamilton product.
Quaternion multiply(const Quaternion& a, const Quaternion& b) {
    return Quaternion{
        a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
        a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
        a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
        a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
    };
}

std::string render(double value) {
    std::ostringstream out;
    out.setf(std::ios::fixed);
    out.precision(3);
    out << value;
    return out.str();
}

/// ST 0601.19 Table 11, from FlyingState × piloting mode.
uint64_t platform_status(FlyingState state, CommonPilotingMode mode) {
    switch (state) {
        case FlyingState::Landed: return 12;         // Landed-Parked
        case FlyingState::MotorRamping: return 3;    // Run-up
        case FlyingState::TakingOff:
        case FlyingState::UserTakeoff: return 4;     // Take-off
        case FlyingState::Landing:
        case FlyingState::EmergencyLanding: return 10;  // Landing
        // Table 11 has no emergency value; 0 (Active) is the least wrong.
        case FlyingState::Emergency: return 0;
        case FlyingState::Hovering:
        case FlyingState::Flying: break;
    }
    switch (mode) {
        case CommonPilotingMode::Tracking: return 7;      // Automated-orbit
        case CommonPilotingMode::FlightPlan:
        case CommonPilotingMode::MoveTo: return 8;        // Transitioning
        case CommonPilotingMode::ReturnHome: return 9;    // Egress
        case CommonPilotingMode::Manual:
        case CommonPilotingMode::MagicCarpet:
        case CommonPilotingMode::Unknown: break;
    }
    return 6;  // Manual operation
}

/// ST 0601.19 Table 12.
uint64_t sensor_control_mode(FlyingState state, CommonPilotingMode mode, bool tracking) {
    if (mode == CommonPilotingMode::Tracking && tracking) return 6;  // Auto-Tracking
    if (mode == CommonPilotingMode::Manual) return 3;                // Manual Control
    if (state == FlyingState::Landed) return 1;                      // Home Position
    return 2;                                                        // Uncontrolled
}

/// Staging helper: encodes through the registry codec and records the outcome
/// in the coverage report, so no item can be emitted without being reported.
class Staging {
public:
    Staging(misbklv::LocalSetBuilder& builder, CoverageReport& coverage)
        : builder_(builder), coverage_(coverage) {}

    void put(uint16_t tag, double value, const std::string& rendered) {
        auto result = builder_.set(tag, misbklv::Value{value});
        if (result) {
            coverage_.emitted[tag] = rendered;
        } else {
            // Out of the item's declared range. Reported rather than clamped:
            // a clamped value is a confident lie.
            coverage_.omitted[tag] = OmissionReason::SourceAbsent;
        }
    }

    void put(uint16_t tag, double value) { put(tag, value, render(value)); }

    void put_uint(uint16_t tag, uint64_t value) {
        auto result = builder_.set(tag, misbklv::Value{value});
        if (result) {
            coverage_.emitted[tag] = std::to_string(value);
        } else {
            coverage_.omitted[tag] = OmissionReason::SourceAbsent;
        }
    }

    void put_text(uint16_t tag, std::string_view value) {
        auto result = builder_.set(tag, misbklv::Value{value});
        if (result) coverage_.emitted[tag] = std::string(value);
    }

    /// Stage pre-encoded bytes — used for the out-of-range special values,
    /// which the typed codec will not produce from a double.
    void put_special(uint16_t tag, const std::vector<uint8_t>& bytes,
                     const std::string& rendered) {
        std::vector<std::byte> raw;
        raw.reserve(bytes.size());
        for (uint8_t b : bytes) raw.push_back(static_cast<std::byte>(b));
        builder_.append_raw(tag, std::span<const std::byte>{raw});
        coverage_.emitted[tag] = rendered;
    }

private:
    misbklv::LocalSetBuilder& builder_;
    CoverageReport& coverage_;
};

/// Items 6/7 are ±20° and ±50°, which a quadcopter exceeds in ordinary flight.
/// Out of range must be signalled with 0x8000, not clamped — and 90/91 carry
/// the trustworthy value regardless. `clamp` overrides this for consumers that
/// can't handle 0x8000 gracefully.
///
/// There is deliberately no third option emitting the true angle. ST 0601 maps
/// ±limit onto the whole int16 range, so a value past the limit has no encoding
/// here at all; a flag that claimed otherwise shipped briefly and only ever
/// produced the clamped value (ADR 0008). Fidelity lives in Items 90/91.
void put_narrow_angle(Staging& staging, uint16_t tag, double degrees, double limit,
                      bool clamp) {
    if (std::abs(degrees) <= limit) {
        staging.put(tag, degrees);
    } else if (clamp) {
        const double clamped = std::clamp(degrees, -limit, limit);
        staging.put(tag, clamped, render(clamped) + " (clamped)");
    } else {
        staging.put_special(tag, {0x80, 0x00}, "out-of-range (0x8000)");
    }
}

}  // namespace

std::string to_string(OmissionReason reason) {
    switch (reason) {
        case OmissionReason::NoSourceInFormat: return "format does not carry it";
        case OmissionReason::SourceAbsent: return "not present in this frame";
        case OmissionReason::NeedsDerivation: return "needs terrain intersection (not implemented)";
        case OmissionReason::NeedsSessionData: return "needs MP4 session metadata (not parsed)";
        case OmissionReason::Unresolved: return "mapping is an open question";
    }
    return "unknown";
}

Euler to_euler(const Quaternion& q) {
    Euler e;
    e.yaw_deg = wrap_360(
        std::atan2(2.0 * (q.w * q.z + q.x * q.y), 1.0 - 2.0 * (q.y * q.y + q.z * q.z)) *
        kRadToDeg);
    // Clamp before asin: a quaternion that is barely non-unit (fixed-point
    // rounding in v3) can push the argument just past 1 and yield NaN.
    const double sin_pitch = std::clamp(2.0 * (q.w * q.y - q.z * q.x), -1.0, 1.0);
    e.pitch_deg = std::asin(sin_pitch) * kRadToDeg;
    e.roll_deg = std::atan2(2.0 * (q.w * q.x + q.y * q.z),
                            1.0 - 2.0 * (q.x * q.x + q.y * q.y)) *
                 kRadToDeg;
    return e;
}

Euler relative_euler(const Quaternion& drone, const Quaternion& frame) {
    return to_euler(multiply(conjugate(drone), frame));
}

std::string CoverageReport::to_text(const Disclosure& disclosure) const {
    // Sensor and target coordinates. Withheld by default: a coverage report is
    // exactly the kind of output someone pastes into an issue, and telemetry
    // from a real flight carries the operator's position.
    const auto is_location = [](uint16_t tag) {
        return tag == 13 || tag == 14 || tag == 40 || tag == 41;
    };
    // Item 4 carries the airframe serial — device identity, which the
    // conformance reports exclude by construction for the same reason.
    const auto is_identity = [](uint16_t tag) { return tag == 4; };
    std::ostringstream out;
    out << "  emitted " << emitted.size() << " item(s):";
    for (const auto& [tag, value] : emitted) {
        const bool hide = (is_location(tag) && !disclosure.location) ||
                          (is_identity(tag) && !disclosure.identity);
        out << " " << tag << "=" << (hide ? "withheld" : value);
    }
    out << "\n";
    if (!omitted.empty()) {
        out << "  omitted " << omitted.size() << " item(s):\n";
        for (const auto& [tag, reason] : omitted) {
            out << "    item " << tag << " — " << to_string(reason) << "\n";
        }
    }
    if (!dropped_fields.empty()) {
        out << "  dropped " << dropped_fields.size()
            << " Parrot field(s) with no ST 0601 item:\n   ";
        for (const auto& field : dropped_fields) out << " " << field;
        out << "\n";
    }
    return out.str();
}

void anchor_to_session(TelemetrySnapshot& snapshot, const SessionMetadata& session,
                       uint64_t first_monotonic_us) {
    if (snapshot.utc_timestamp_us) return;  // already has a real one
    if (!session.media_date_epoch_s) return;
    // The monotonic clock can run backwards relative to the first sample only
    // if the caller passed a later frame as the origin; guard rather than wrap.
    const uint64_t elapsed_us =
        snapshot.monotonic_us > first_monotonic_us ? snapshot.monotonic_us - first_monotonic_us : 0;
    snapshot.utc_timestamp_us =
        static_cast<uint64_t>(*session.media_date_epoch_s) * 1000000ULL + elapsed_us;
}

Klv0601Frame to_0601(const TelemetrySnapshot& s, const SessionMetadata* session,
                     bool clamp_attitude) {
    Klv0601Frame result;
    CoverageReport& coverage = result.coverage;
    misbklv::LocalSetBuilder builder(misbklv::gen::uas_0601);
    Staging staging(builder, coverage);

    // Carried through from normalization, where these were discarded (ADR 0002).
    coverage.dropped_fields = s.dropped_fields;

    // --- Item 2, Precision Time Stamp (mandatory in 0601) ---
    if (s.utc_timestamp_us) {
        staging.put_uint(2, *s.utc_timestamp_us);
    } else {
        // v3 has only a monotonic clock, and proto says "invalid" through a zero
        // accuracy. Anchoring needs session media_date, which nothing parses.
        coverage.omitted[2] = OmissionReason::NeedsSessionData;
    }

    // --- platform attitude (Items 5/6/7, and the trustworthy 90/91) ---
    const Euler platform = to_euler(s.drone_quat);
    staging.put(5, platform.yaw_deg);
    put_narrow_angle(staging, 6, platform.pitch_deg, 20.0, clamp_attitude);
    put_narrow_angle(staging, 7, platform.roll_deg, 50.0, clamp_attitude);
    staging.put(90, platform.pitch_deg);
    staging.put(91, platform.roll_deg);

    // --- Item 8, air speed: v3 only, and only when the drone reported it ---
    if (s.air_speed_mps) {
        staging.put_uint(8, static_cast<uint64_t>(std::lround(std::max(0.0, *s.air_speed_mps))));
    } else {
        coverage.omitted[8] = s.source_format == ParrotFormat::Proto
                                  ? OmissionReason::NoSourceInFormat
                                  : OmissionReason::SourceAbsent;
    }

    // --- Items 12/65, constants ---
    staging.put_text(12, "Geodetic WGS84");
    staging.put_uint(65, 19);

    // --- sensor position (Items 13/14/15/75) ---
    if (s.latitude_deg) staging.put(13, *s.latitude_deg);
    else coverage.omitted[13] = OmissionReason::SourceAbsent;
    if (s.longitude_deg) staging.put(14, *s.longitude_deg);
    else coverage.omitted[14] = OmissionReason::SourceAbsent;
    if (s.altitude_amsl_m) staging.put(15, *s.altitude_amsl_m);
    else coverage.omitted[15] = OmissionReason::SourceAbsent;
    if (s.altitude_wgs84_m) {
        staging.put(75, *s.altitude_wgs84_m);
    } else {
        // v3 carries AMSL only; deriving the ellipsoid height needs an EGM96
        // lookup this project does not have.
        coverage.omitted[75] = s.source_format == ParrotFormat::V3
                                   ? OmissionReason::NoSourceInFormat
                                   : OmissionReason::SourceAbsent;
    }

    // --- field of view (Items 16/17) ---
    if (s.hfov_deg) staging.put(16, *s.hfov_deg);
    else coverage.omitted[16] = OmissionReason::SourceAbsent;
    if (s.vfov_deg) staging.put(17, *s.vfov_deg);
    else coverage.omitted[17] = OmissionReason::SourceAbsent;

    // --- gimbal orientation relative to the airframe (Items 18/19/20) ---
    const Euler relative = relative_euler(s.drone_quat, s.frame_quat);
    staging.put(18, relative.yaw_deg);
    staging.put(19, relative.pitch_deg);
    staging.put(20, wrap_360(relative.roll_deg));

    // --- velocity (Items 51/56/79/80/112) ---
    // 0601 §8.51 measures vertical speed toward zenith; Parrot NED is positive
    // down. The sign flip lives here and only here.
    staging.put(51, -s.down_mps);
    const double ground_speed = std::hypot(s.north_mps, s.east_mps);
    staging.put_uint(56, static_cast<uint64_t>(std::min(255.0, std::round(ground_speed))));
    staging.put(79, s.north_mps);
    staging.put(80, s.east_mps);
    if (ground_speed >= kCourseSpeedThresholdMps) {
        staging.put(112, wrap_360(std::atan2(s.east_mps, s.north_mps) * kRadToDeg));
    } else {
        // Course over ground is meaningless when the aircraft is not moving.
        coverage.omitted[112] = OmissionReason::SourceAbsent;
    }

    // --- Item 113, AGL ---
    staging.put(113, s.ground_distance_m);

    // --- Items 123/124, navigation satellites ---
    if (s.sv_count) {
        staging.put_uint(123, std::min<uint64_t>(255, *s.sv_count));
        // Table 10 bit 1 is NAVSAT-GPS. Parrot does not say which constellation
        // the fix came from, so only that bit is claimed.
        staging.put_uint(124, *s.sv_count > 0 ? 0x02 : 0x00);
    } else {
        coverage.omitted[123] = OmissionReason::SourceAbsent;
        coverage.omitted[124] = OmissionReason::SourceAbsent;
    }

    // --- Items 125/126, status enums ---
    staging.put_uint(125, platform_status(s.flying_state, s.piloting_mode));
    staging.put_uint(126, sensor_control_mode(s.flying_state, s.piloting_mode, s.tracking_active));

    // --- Items 40/41/42/45/46, target ---
    if (s.target) {
        staging.put(40, s.target->latitude_deg);
        staging.put(41, s.target->longitude_deg);
        staging.put(42, s.target->altitude_amsl_m);
        // Parrot's radial/tangential horizontal accuracies are not literally
        // CE90/LE90. Reporting them as such is an open question in the mapping,
        // so they are withheld rather than relabelled.
        coverage.omitted[45] = OmissionReason::Unresolved;
        coverage.omitted[46] = OmissionReason::Unresolved;
    } else {
        for (uint16_t tag : {40, 41, 42, 45, 46}) {
            coverage.omitted[tag] = OmissionReason::SourceAbsent;
        }
    }

    // --- Item 134, zoom ---
    if (s.zoom_level) {
        // Parrot's zoom_level is a multiplier; 0601 wants 0..100 %. No defined
        // correspondence exists, so inventing one would be a fabricated number.
        coverage.omitted[134] = OmissionReason::Unresolved;
    } else {
        coverage.omitted[134] = OmissionReason::NoSourceInFormat;
    }

    // --- Items 4/10/11, session-level identity ---
    // Item 4 is the tail number; Parrot's nearest equivalent is the airframe
    // serial. Item 10 is the platform designation, i.e. the model.
    if (session && session->serial_number) {
        staging.put_text(4, *session->serial_number);
    } else {
        coverage.omitted[4] = OmissionReason::NeedsSessionData;
    }
    if (session && session->model) {
        staging.put_text(10, *session->model);
    } else {
        coverage.omitted[10] = OmissionReason::NeedsSessionData;
    }
    // Item 11 names the active sensor. Proto's `camera.spectrum` is the only
    // source; v3 carries nothing equivalent, and an unknown spectrum is left
    // unnamed rather than assumed to be EO.
    if (auto sensor = image_source_sensor(s.spectrum, s.subtype)) {
        staging.put_text(11, *sensor);
    } else {
        coverage.omitted[11] = s.source_format == ParrotFormat::V3
                                   ? OmissionReason::NoSourceInFormat
                                   : OmissionReason::SourceAbsent;
    }

    // --- Tier 2, derived geometry (open fork: flat-earth vs DTED) ---
    for (uint16_t tag : {21, 22, 23, 24, 25, 57, 78, 82, 83, 84, 85, 86, 87, 88, 89}) {
        coverage.omitted[tag] = OmissionReason::NeedsDerivation;
    }

    // Item 2 is mandatory; when it is absent we assemble anyway rather than
    // failing the conversion, and the omission stands in the report.
    const bool enforce_mandatory = s.utc_timestamp_us.has_value();
    auto packet = std::move(builder).finalize(std::span{misbklv::kUas0601Key}, enforce_mandatory);
    if (packet) {
        result.packet.assign(reinterpret_cast<const uint8_t*>(packet->data()),
                             reinterpret_cast<const uint8_t*>(packet->data()) + packet->size());
    }
    return result;
}

}  // namespace parrot_to_klv
