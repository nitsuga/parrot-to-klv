// Copyright 2026 Eric Schneider
// Licensed under the Apache License, Version 2.0
//
// Checks the Parrot -> ST 0601 transform: the quaternion/Euler conversions, the
// normalization boundary (ADR 0004), and the mapping itself. Emitted packets are
// parsed back through libmisbklv and decoded item by item, so a value is checked
// as a 0601 consumer would see it rather than as we staged it.

#include "misbklv/codec.hpp"
#include "misbklv/packet.hpp"
#include "misbklv/registries.hpp"
#include "telemetry.hpp"
#include "transform.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

using namespace parrot_to_klv;

namespace {

int g_failures = 0;

void check(bool condition, const std::string& what) {
    if (!condition) {
        std::cerr << "FAIL: " << what << "\n";
        ++g_failures;
    }
}

void check_near(double actual, double expected, double tolerance, const std::string& what) {
    if (!(std::abs(actual - expected) <= tolerance)) {
        std::cerr << "FAIL: " << what << " — expected " << expected << " ±" << tolerance
                  << ", got " << actual << "\n";
        ++g_failures;
    }
}

/// Decode an emitted packet back into tag -> value, the way a consumer would.
struct Decoded {
    std::map<uint16_t, double> reals;
    std::map<uint16_t, uint64_t> uints;
    std::map<uint16_t, std::string> texts;
    std::map<uint16_t, size_t> raw_len;
    bool parsed = false;
};

Decoded decode_packet(const std::vector<uint8_t>& packet) {
    Decoded out;
    if (packet.empty()) return out;
    auto bytes = std::span<const std::byte>(reinterpret_cast<const std::byte*>(packet.data()),
                                            packet.size());
    auto parsed = misbklv::parse_packet(bytes);
    if (!parsed) return out;
    out.parsed = true;
    for (const auto& item : parsed->items) {
        const auto* descriptor = misbklv::gen::uas_0601.find(item.tag);
        out.raw_len[item.tag] = item.value.size();
        if (!descriptor) continue;
        auto value = misbklv::codec::decode(*descriptor, item.value);
        if (!value) continue;
        if (std::holds_alternative<double>(*value)) {
            out.reals[item.tag] = std::get<double>(*value);
        } else if (std::holds_alternative<std::uint64_t>(*value)) {
            out.uints[item.tag] = std::get<std::uint64_t>(*value);
        } else if (std::holds_alternative<std::string_view>(*value)) {
            out.texts[item.tag] = std::string(std::get<std::string_view>(*value));
        }
    }
    return out;
}

/// A level, north-facing aircraft: the identity quaternion.
Quaternion identity() { return Quaternion{1.0, 0.0, 0.0, 0.0}; }

/// Rotation of `degrees` about Z (yaw).
Quaternion yaw_quat(double degrees) {
    const double half = degrees * 3.14159265358979323846 / 360.0;
    return Quaternion{std::cos(half), 0.0, 0.0, std::sin(half)};
}

/// Rotation of `degrees` about Y (pitch).
Quaternion pitch_quat(double degrees) {
    const double half = degrees * 3.14159265358979323846 / 360.0;
    return Quaternion{std::cos(half), 0.0, std::sin(half), 0.0};
}

/// Rotation of `degrees` about X (roll).
Quaternion roll_quat(double degrees) {
    const double half = degrees * 3.14159265358979323846 / 360.0;
    return Quaternion{std::cos(half), std::sin(half), 0.0, 0.0};
}

void check_euler() {
    const Euler level = to_euler(identity());
    check_near(level.yaw_deg, 0.0, 1e-9, "identity quaternion has zero yaw");
    check_near(level.pitch_deg, 0.0, 1e-9, "identity quaternion has zero pitch");
    check_near(level.roll_deg, 0.0, 1e-9, "identity quaternion has zero roll");

    check_near(to_euler(yaw_quat(90.0)).yaw_deg, 90.0, 1e-6, "90 degree yaw");
    // Yaw wraps into [0, 360) rather than reporting -90.
    check_near(to_euler(yaw_quat(-90.0)).yaw_deg, 270.0, 1e-6, "negative yaw wraps to [0,360)");
    check_near(to_euler(pitch_quat(30.0)).pitch_deg, 30.0, 1e-6, "30 degree pitch");

    // A gimbal pointing exactly along the airframe has zero relative angles,
    // whatever the airframe is doing.
    const Quaternion drone = yaw_quat(137.0);
    const Euler same = relative_euler(drone, drone);
    check_near(same.yaw_deg, 0.0, 1e-6, "gimbal aligned with airframe has zero relative azimuth");
    check_near(same.pitch_deg, 0.0, 1e-6, "…and zero relative elevation");

    // Airframe at 100 degrees, gimbal at 130 -> 30 degrees relative.
    const Euler offset = relative_euler(yaw_quat(100.0), yaw_quat(130.0));
    check_near(offset.yaw_deg, 30.0, 1e-6, "relative azimuth is the difference");

    // A quaternion slightly off unit norm (v3 fixed-point rounding) must not
    // produce NaN through asin.
    const Euler nudged = to_euler(Quaternion{1.0001, 0.0, 0.7072, 0.0});
    check(!std::isnan(nudged.pitch_deg), "non-unit quaternion does not yield NaN pitch");
}

TelemetrySnapshot base_v3_snapshot() {
    ParrotV3Frame frame;
    frame.latitude_deg = 45.5;
    frame.longitude_deg = -73.5;
    frame.altitude_amsl_m = 250.0;
    frame.sv_count = 14;
    frame.ground_distance_m = 120.0;
    frame.north_speed_mps = 6.0;
    frame.east_speed_mps = 8.0;
    frame.down_speed_mps = 2.0;  // descending
    frame.drone_quat = identity();
    frame.frame_quat = identity();
    frame.picture_hfov_deg = 69.0;
    frame.picture_vfov_deg = 42.0;
    frame.flying_state = FlyingState::Flying;
    frame.piloting_mode = PilotingMode::Manual;
    frame.frame_timestamp_us = 33333;
    return from_v3(frame);
}

void check_v3_mapping() {
    const auto snapshot = base_v3_snapshot();
    check(snapshot.source_format == ParrotFormat::V3, "snapshot records its source format");
    check(!snapshot.utc_timestamp_us.has_value(), "v3 supplies no absolute time");
    check(!snapshot.altitude_wgs84_m.has_value(), "v3 supplies no ellipsoid height");
    check_near(*snapshot.hfov_deg, 69.0, 1e-9, "v3 FOV passes through as degrees");

    const auto frame = to_0601(snapshot);
    check(!frame.packet.empty(), "v3 snapshot produces a packet");
    const auto decoded = decode_packet(frame.packet);
    check(decoded.parsed, "emitted packet parses as a 0601 packet");

    check_near(decoded.reals.at(13), 45.5, 1e-6, "Item 13 sensor latitude");
    check_near(decoded.reals.at(14), -73.5, 1e-6, "Item 14 sensor longitude");
    check_near(decoded.reals.at(15), 250.0, 0.5, "Item 15 sensor true altitude");
    check_near(decoded.reals.at(16), 69.0, 0.01, "Item 16 horizontal FOV");
    check_near(decoded.reals.at(113), 120.0, 0.1, "Item 113 altitude AGL");

    // The sign flip: Parrot NED is positive down, 0601 Item 51 is positive up.
    check_near(decoded.reals.at(51), -2.0, 0.01, "Item 51 vertical speed is negated");
    check_near(decoded.reals.at(79), 6.0, 0.05, "Item 79 north velocity");
    check_near(decoded.reals.at(80), 8.0, 0.05, "Item 80 east velocity");
    check(decoded.uints.at(56) == 10, "Item 56 ground speed is hypot(6,8) rounded");
    check_near(decoded.reals.at(112), 53.13, 0.05, "Item 112 course angle from NE velocity");

    check(decoded.uints.at(65) == 19, "Item 65 LS version is 19");
    check(decoded.texts.at(12) == "Geodetic WGS84", "Item 12 coordinate system");
    check(decoded.uints.at(123) == 14, "Item 123 NAVSAT count");
    check(decoded.uints.at(124) == 0x02, "Item 124 claims GPS only (Table 10 bit 1)");
    check(decoded.uints.at(125) == 6, "Item 125 flying + manual is Manual operation");
    check(decoded.uints.at(126) == 3, "Item 126 manual mode is Manual Control");

    // Omissions must be reported, not silent.
    check(frame.coverage.omitted.count(2) == 1, "Item 2 omitted for v3");
    check(frame.coverage.omitted.at(2) == OmissionReason::NeedsSessionData,
          "Item 2 omission blames the missing session metadata");
    check(frame.coverage.omitted.at(75) == OmissionReason::NoSourceInFormat,
          "Item 75 omission blames the format");
    check(frame.coverage.omitted.at(23) == OmissionReason::NeedsDerivation,
          "Tier 2 frame centre is reported as needing derivation");
    check(frame.coverage.omitted.count(134) == 1, "Item 134 zoom omitted for v3");
    check(frame.coverage.emitted.count(13) == 1, "coverage lists Item 13 as emitted");
    check(!frame.coverage.to_text().empty(), "coverage renders as text");

    // ADR 0002's obligation: the fields 0601 cannot carry are named, not
    // silently discarded.
    const auto& dropped = frame.coverage.dropped_fields;
    check(!dropped.empty(), "coverage names the dropped Parrot fields");
    check(std::find(dropped.begin(), dropped.end(), "battery_percentage") != dropped.end(),
          "battery is reported as dropped");
    check(frame.coverage.to_text().find("battery_percentage") != std::string::npos,
          "the rendered report mentions the dropped battery field");

    // A coverage report is the kind of thing that gets pasted into an issue, so
    // it must not leak the flight's position unless asked.
    const std::string safe = frame.coverage.to_text();
    check(safe.find("45.5") == std::string::npos, "latitude is withheld by default");
    check(safe.find("-73.5") == std::string::npos, "longitude is withheld by default");
    check(safe.find("13=withheld") != std::string::npos, "…and marked as withheld");
    const std::string revealed = frame.coverage.to_text(Disclosure{.location = true});
    check(revealed.find("45.5") != std::string::npos, "--with-location reveals latitude");
}

void check_proto_mapping() {
    ParrotProtoFrame frame;
    frame.drone_quat = identity();
    frame.frame_quat = identity();
    frame.camera_location.latitude_deg = 45.5;
    frame.camera_location.longitude_deg = -73.5;
    frame.camera_location.altitude_egm96_m = 250.0;
    frame.camera_location.altitude_wgs84_m = 285.0;
    frame.camera_location.sv_count = 14;
    frame.ground_distance_m = 120.0;
    frame.speed = {6.0, 8.0, 2.0};
    frame.picture_hfov_rad = 1.2043;  // ~69 degrees
    frame.picture_vfov_rad = 0.7330;  // ~42 degrees
    frame.utc_timestamp_us = 1770000000000000;
    frame.utc_timestamp_accuracy = 50;
    frame.flying_state = FlyingState::Flying;
    frame.piloting_mode = ProtoPilotingMode::Manual;
    frame.zoom_level = 2.0;
    frame.spectrum = CameraSpectrum::Visible;
    frame.subtype = CameraSubtype::Wide;
    ProtoLfic target;
    target.type = LficType::Cot;
    target.location.latitude_deg = 45.6;
    target.location.longitude_deg = -73.6;
    target.location.altitude_egm96_m = 100.0;
    target.horizontal_radial_accuracy_m = 4.0;
    frame.lfic.push_back(target);

    const auto snapshot = from_proto(frame);
    // Radians on the wire become degrees at the boundary, so the mapping never
    // has to know which format it came from.
    check_near(*snapshot.hfov_deg, 69.0, 0.05, "proto FOV is converted to degrees");
    check(snapshot.utc_timestamp_us.has_value(), "valid UTC accuracy yields a timestamp");
    check(snapshot.altitude_wgs84_m.has_value(), "proto supplies the ellipsoid height");
    // PM_MANUAL is 1 in protobuf; mapping by number would give ReturnHome.
    check(snapshot.piloting_mode == CommonPilotingMode::Manual,
          "proto piloting mode normalizes by name");

    const auto klv = to_0601(snapshot);
    const auto decoded = decode_packet(klv.packet);
    check(decoded.parsed, "proto packet parses");
    check(decoded.uints.at(2) == 1770000000000000, "Item 2 carries the UTC timestamp");
    check_near(decoded.reals.at(75), 285.0, 0.5, "Item 75 ellipsoid height");
    check_near(decoded.reals.at(16), 69.0, 0.05, "Item 16 from converted radians");
    check_near(decoded.reals.at(40), 45.6, 1e-6, "Item 40 target latitude from LFIC");
    check_near(decoded.reals.at(41), -73.6, 1e-6, "Item 41 target longitude from LFIC");

    // Accuracies exist but are not honestly CE90/LE90, so they stay withheld.
    check(klv.coverage.omitted.at(45) == OmissionReason::Unresolved,
          "Item 45 withheld as an unresolved mapping");
    check(klv.coverage.omitted.at(134) == OmissionReason::Unresolved,
          "Item 134 withheld — zoom multiplier has no defined percentage");
    check(klv.coverage.omitted.count(8) == 1, "Item 8 omitted: proto has no air speed");
    // Item 11 now has a source: the camera spectrum.
    check(decoded.texts.at(11) == "EO Wide", "Item 11 names the wide visible sensor");
    check(klv.coverage.omitted.at(8) == OmissionReason::NoSourceInFormat,
          "Item 8 omission blames the format, not the frame");
}

void check_image_source_sensor() {
    using Spectrum = ImageSpectrum;
    using Sub = ImageSubtype;
    // The spectrum is the stem; the subtype qualifies it, matching the
    // standard's own "EO Narrow" / "EO Wide" example configuration.
    check(*image_source_sensor(Spectrum::Visible, Sub::Unknown) == "EO", "visible alone is EO");
    check(*image_source_sensor(Spectrum::Visible, Sub::Wide) == "EO Wide", "visible + wide");
    check(*image_source_sensor(Spectrum::Visible, Sub::Tele) == "EO Tele", "visible + tele");
    check(*image_source_sensor(Spectrum::Thermal, Sub::Unknown) == "IR", "thermal alone is IR");
    check(*image_source_sensor(Spectrum::Thermal, Sub::Wide) == "IR Wide", "thermal + wide");
    // Item 11 is free-form utf8, so a fused image is named rather than being
    // mislabelled as one of its halves or dropped.
    check(*image_source_sensor(Spectrum::Blended, Sub::Unknown) == "EO/IR Blended",
          "blended is named");
    check(*image_source_sensor(Spectrum::Blended, Sub::Tele) == "EO/IR Blended Tele",
          "blended is still qualified by the subtype");
    check(!image_source_sensor(Spectrum::Unknown, Sub::Unknown).has_value(),
          "an unknown spectrum names no sensor");
    // A subtype without a spectrum is not a usable sensor name: "Wide" does not
    // say whether the imagery is visible or thermal.
    check(!image_source_sensor(Spectrum::Unknown, Sub::Wide).has_value(),
          "a subtype alone does not name a sensor");

    // Thermal footage must not be reported as EO.
    ParrotProtoFrame thermal;
    thermal.drone_quat = Quaternion{1.0, 0.0, 0.0, 0.0};
    thermal.frame_quat = thermal.drone_quat;
    thermal.spectrum = CameraSpectrum::Thermal;
    thermal.subtype = CameraSubtype::Tele;
    const auto klv = to_0601(from_proto(thermal));
    check(decode_packet(klv.packet).texts.at(11) == "IR Tele", "thermal tele camera emits IR Tele");

    // v3 carries no spectrum at all, and that is a property of the format.
    ParrotV3Frame v3;
    v3.drone_quat = Quaternion{1.0, 0.0, 0.0, 0.0};
    v3.frame_quat = v3.drone_quat;
    const auto from_binary = to_0601(from_v3(v3));
    check(from_binary.coverage.omitted.at(11) == OmissionReason::NoSourceInFormat,
          "v3 reports Item 11 as absent from the format, not from the frame");

    // A proto frame that simply did not report one is a different case.
    ParrotProtoFrame silent;
    silent.drone_quat = Quaternion{1.0, 0.0, 0.0, 0.0};
    silent.frame_quat = silent.drone_quat;
    const auto unspecified = to_0601(from_proto(silent));
    check(unspecified.coverage.omitted.at(11) == OmissionReason::SourceAbsent,
          "proto without a spectrum reports the frame, not the format");
}

void check_unknown_utc_is_not_invented() {
    ParrotProtoFrame frame;
    frame.drone_quat = identity();
    frame.frame_quat = identity();
    frame.utc_timestamp_us = 1770000000000000;
    frame.utc_timestamp_accuracy = 0;  // the schema's "invalid" signal
    const auto snapshot = from_proto(frame);
    check(!snapshot.utc_timestamp_us.has_value(),
          "zero UTC accuracy means the timestamp is not used");

    // Zero altitude is proto's in-band "unknown"; treating it as sea level
    // would put the aircraft 250 m below where it is.
    check(!snapshot.altitude_amsl_m.has_value(), "zero altitude is unknown, not sea level");

    const auto klv = to_0601(snapshot);
    check(klv.coverage.omitted.count(2) == 1, "Item 2 omitted rather than invented");
    check(!klv.packet.empty(), "packet is still produced without the mandatory Item 2");
}

void check_narrow_angle_specials() {
    // Item 6 is ±20 degrees; a quadcopter exceeds that in ordinary flight, and
    // 0601 says signal it with 0x8000 rather than clamping.
    auto snapshot = base_v3_snapshot();
    ParrotV3Frame steep;
    steep.drone_quat = pitch_quat(45.0);
    steep.frame_quat = pitch_quat(45.0);
    snapshot.drone_quat = steep.drone_quat;
    snapshot.frame_quat = steep.frame_quat;

    const auto klv = to_0601(snapshot);
    const auto decoded = decode_packet(klv.packet);
    check(decoded.parsed, "steep-pitch packet parses");
    check(decoded.raw_len.count(6) == 1, "Item 6 is present");
    check(klv.coverage.emitted.at(6).find("out-of-range") != std::string::npos,
          "Item 6 reports the out-of-range special rather than a value");
    // Item 90 is the one to trust: ±90 degrees, so 45 fits.
    check_near(decoded.reals.at(90), 45.0, 0.01, "Item 90 carries the true pitch");
}

/// A snapshot pitched and rolled past both Item 6 and Item 7 spec ranges.
///
/// Pitch 45 exceeds Item 6's ±20; roll 70 exceeds Item 7's ±50. Both stay inside
/// Items 90/91's ±90, which is the point — the trustworthy pair must survive
/// whatever the narrow pair is made to do.
TelemetrySnapshot steep_snapshot(double pitch_deg, double roll_deg) {
    auto snapshot = base_v3_snapshot();
    const auto q = pitch_deg != 0.0 ? pitch_quat(pitch_deg) : roll_quat(roll_deg);
    snapshot.drone_quat = q;
    snapshot.frame_quat = q;
    return snapshot;
}

void check_attitude_clamped() {
    // --clamp-attitude: emit the spec limit instead of the 0x8000 sentinel, for
    // consumers that treat a missing key as a parse failure (ADR 0005).
    auto snapshot = steep_snapshot(45.0, 0.0);
    const auto klv = to_0601(snapshot, nullptr, /*clamp=*/true, /*force_emit=*/false);
    const auto decoded = decode_packet(klv.packet);
    check(decoded.parsed, "clamped packet parses");

    check_near(decoded.reals.at(6), 20.0, 0.01, "Item 6 clamps to its +20 limit");
    check(klv.coverage.emitted.at(6).find("clamped") != std::string::npos,
          "coverage labels Item 6 as clamped rather than reporting it as measured");
    check(klv.coverage.emitted.at(6).find("out-of-range") == std::string::npos,
          "a clamped item does not also claim to be out of range");
    // The whole justification for allowing the lie: 90 still tells the truth.
    check_near(decoded.reals.at(90), 45.0, 0.01, "Item 90 keeps the true pitch when 6 is clamped");

    // Negative side, and Item 7's wider limit.
    auto rolled = steep_snapshot(0.0, -70.0);
    const auto neg = decode_packet(
        to_0601(rolled, nullptr, /*clamp=*/true, /*force_emit=*/false).packet);
    check_near(neg.reals.at(7), -50.0, 0.01, "Item 7 clamps to its -50 limit");
    check_near(neg.reals.at(91), -70.0, 0.01, "Item 91 keeps the true roll");
}

void check_attitude_in_range_is_untouched() {
    // Neither flag may alter a value that was always emittable — a flag that
    // changes in-range behaviour is a flag that changes every conversion.
    auto snapshot = steep_snapshot(10.0, 0.0);
    const auto plain = decode_packet(to_0601(snapshot).packet);
    const auto clamped = decode_packet(
        to_0601(snapshot, nullptr, /*clamp=*/true, /*force_emit=*/false).packet);
    const auto forced = decode_packet(
        to_0601(snapshot, nullptr, /*clamp=*/false, /*force_emit=*/true).packet);

    check_near(plain.reals.at(6), 10.0, 0.01, "in-range pitch is emitted as measured");
    check_near(clamped.reals.at(6), plain.reals.at(6), 1e-9,
               "--clamp-attitude leaves an in-range Item 6 alone");
    check_near(forced.reals.at(6), plain.reals.at(6), 1e-9,
               "--force-emit-attitude leaves an in-range Item 6 alone");

    const auto klv = to_0601(snapshot, nullptr, /*clamp=*/true, /*force_emit=*/false);
    check(klv.coverage.emitted.at(6).find("clamped") == std::string::npos,
          "an in-range item is not labelled clamped");
}

void check_attitude_force_emitted() {
    // --force-emit-attitude: deliberately non-compliant, encoding an angle the
    // item's range cannot express (ADR 0005).
    auto snapshot = steep_snapshot(45.0, 0.0);
    const auto klv = to_0601(snapshot, nullptr, /*clamp=*/false, /*force_emit=*/true);
    const auto decoded = decode_packet(klv.packet);
    check(decoded.parsed, "force-emitted packet parses");

    check(klv.coverage.emitted.at(6).find("out-of-spec") != std::string::npos,
          "coverage labels Item 6 as out-of-spec");
    check(decoded.raw_len.at(6) == 2, "Item 6 is still a 2-byte item");
    check_near(decoded.reals.at(90), 45.0, 0.01, "Item 90 is unaffected by force-emit");

    // What the encoding actually carries — and it is not the true pitch.
    //
    // **This asserts a known defect** (fork 8,
    // `0008-force-emit-attitude-cannot-work.md`). ST 0601 already spends the
    // whole int16 range on ±limit, so `encode_out_of_spec_angle`'s scale of
    // 0x7FFF/limit leaves no headroom: it is only ever called when
    // |degrees| > limit, so the result always saturates to 0x7FFF. Force-emit
    // therefore produces *exactly the clamped value*, byte for byte, while
    // labelling it "out-of-spec".
    //
    // If you are here because this failed, you have probably fixed the flag —
    // update this expectation and close fork 8.
    const auto clamped = decode_packet(
        to_0601(snapshot, nullptr, /*clamp=*/true, /*force_emit=*/false).packet);
    check_near(decoded.reals.at(6), 20.0, 0.01,
               "force-emit saturates at the spec limit rather than carrying 45");
    check_near(decoded.reals.at(6), clamped.reals.at(6), 1e-9,
               "force-emit and clamp emit the same value — only the label differs");
}

void check_status_enums() {
    auto snapshot = base_v3_snapshot();

    snapshot.flying_state = FlyingState::Landed;
    check(decode_packet(to_0601(snapshot).packet).uints.at(125) == 12, "landed is Landed-Parked");
    check(decode_packet(to_0601(snapshot).packet).uints.at(126) == 3,
          "manual mode outranks landed for sensor control");

    snapshot.flying_state = FlyingState::TakingOff;
    check(decode_packet(to_0601(snapshot).packet).uints.at(125) == 4, "taking off is Take-off");

    snapshot.flying_state = FlyingState::Flying;
    snapshot.piloting_mode = CommonPilotingMode::ReturnHome;
    check(decode_packet(to_0601(snapshot).packet).uints.at(125) == 9, "return home is Egress");

    snapshot.piloting_mode = CommonPilotingMode::Tracking;
    snapshot.tracking_active = true;
    const auto tracking = decode_packet(to_0601(snapshot).packet);
    check(tracking.uints.at(125) == 7, "tracking is Automated-orbit");
    check(tracking.uints.at(126) == 6, "active tracking is Auto-Tracking");

    snapshot.flying_state = FlyingState::Emergency;
    check(decode_packet(to_0601(snapshot).packet).uints.at(125) == 0,
          "emergency falls back to Active — Table 11 has no emergency value");
}

void check_course_angle_omitted_when_stationary() {
    auto snapshot = base_v3_snapshot();
    snapshot.north_mps = 0.0;
    snapshot.east_mps = 0.0;
    const auto klv = to_0601(snapshot);
    check(klv.coverage.omitted.count(112) == 1,
          "course angle omitted when the aircraft is not moving");
    check(klv.coverage.emitted.count(112) == 0, "…and not emitted as a spurious heading");
    check(decode_packet(klv.packet).uints.at(56) == 0, "ground speed is zero");
}

}  // namespace

int main() {
    check_euler();
    check_v3_mapping();
    check_proto_mapping();
    check_image_source_sensor();
    check_unknown_utc_is_not_invented();
    check_narrow_angle_specials();
    check_attitude_clamped();
    check_attitude_in_range_is_untouched();
    check_attitude_force_emitted();
    check_status_enums();
    check_course_angle_omitted_when_stationary();

    if (g_failures > 0) {
        std::cerr << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "all transform checks passed\n";
    return 0;
}
