// Copyright 2026 Eric Schneider
// Licensed under the Apache License, Version 2.0
//
// Decodes the proto samples in the synthetic fixtures and checks them field by
// field against the values test/make_fixture.py encodes, then exercises the
// wire format's edges directly with hand-built buffers. Keep the fixture
// expectations in step with the generator: they are its inputs, not the
// parser's output.
//
// Both encoder and decoder are ours (ADR 0003 § Validation strategy), so these
// checks catch an asymmetric mistake — a field number or wire type wrong on one
// side — but not a misreading of proto3 shared by both.

#include "mp4_metadata_extractor.hpp"
#include "parrot_proto.hpp"

#include <cmath>
#include <cstdint>
#include <iostream>
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

template <typename T>
void check_eq(const T& actual, const T& expected, const std::string& what) {
    if (!(actual == expected)) {
        std::cerr << "FAIL: " << what << " — expected " << expected << ", got " << actual << "\n";
        ++g_failures;
    }
}

// --- little wire-format builder, mirroring make_fixture.py ---

void put_varint(std::vector<uint8_t>& out, uint64_t n) {
    do {
        uint8_t byte = n & 0x7F;
        n >>= 7;
        out.push_back(byte | (n ? 0x80 : 0));
    } while (n);
}

void put_tag(std::vector<uint8_t>& out, uint32_t field, uint32_t wire) {
    put_varint(out, (static_cast<uint64_t>(field) << 3) | wire);
}

std::vector<uint8_t> msg(uint32_t field, const std::vector<uint8_t>& payload) {
    std::vector<uint8_t> out;
    put_tag(out, field, 2);
    put_varint(out, payload.size());
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

void check_fixture_samples(const std::string& fixture_dir) {
    auto extractor = Mp4MetadataExtractor::open(fixture_dir + "/parrot_proto.mp4");
    if (!extractor) {
        std::cerr << "FAIL: cannot open parrot_proto.mp4\n";
        ++g_failures;
        return;
    }
    check_eq(to_string(extractor->track_info().format), std::string("proto"),
             "fixture is identified as the proto format");
    check_eq(extractor->sample_count(), uint32_t{8}, "eight proto samples");

    auto sample = extractor->extract_sample(0);
    if (!sample) {
        std::cerr << "FAIL: cannot extract proto sample 0\n";
        ++g_failures;
        return;
    }
    ParrotProtoError err{};
    auto frame = parse_proto(sample->data, &err);
    if (!frame) {
        std::cerr << "FAIL: sample 0 did not decode — " << to_string(err) << "\n";
        ++g_failures;
        return;
    }

    // Drone.
    check_near(frame->drone_quat.norm(), 1.0, 1e-6, "drone quaternion is unit norm");
    check_near(frame->drone_location.latitude_deg, 45.5, 1e-9, "latitude");
    check_near(frame->drone_location.longitude_deg, -73.5, 1e-9, "longitude");
    // The whole reason proto is parsed separately: both datums, no geoid model.
    check_near(frame->drone_location.altitude_wgs84_m, 120.0, 1e-9, "WGS84 altitude");
    check_near(frame->drone_location.altitude_egm96_m, 100.0, 1e-9, "EGM96 altitude (field 7)");
    check_eq(frame->drone_location.sv_count, uint32_t{12}, "sv_count (field 4)");
    check_near(frame->ground_distance_m, 25.0, 1e-9, "ground distance");
    check_near(frame->altitude_ato_m, 30.0, 1e-9, "altitude above take-off");
    check_near(frame->speed.north_mps, 1.5, 1e-6, "north speed");
    check_near(frame->speed.east_mps, -2.5, 1e-6, "east speed");
    check_near(frame->speed.down_mps, 0.5, 1e-6, "down speed");
    check_eq(frame->battery_percentage, 77, "battery percentage");
    check(frame->flying_state == FlyingState::Flying, "flying state is Flying");
    // PM_MANUAL is 1 in protobuf and 0 in the binary formats. Reading this as
    // the binary enum would report ReturnHome.
    check(frame->piloting_mode == ProtoPilotingMode::Manual, "piloting mode is Manual (proto=1)");
    check(frame->animation_in_progress, "animation in progress");

    // Camera.
    check_eq(frame->camera_timestamp_us, uint64_t{0}, "camera timestamp of sample 0");
    check_eq(frame->utc_timestamp_accuracy, uint32_t{50}, "utc accuracy non-zero means valid");
    check_eq(frame->utc_timestamp_us, uint64_t{1770000000000000}, "utc timestamp");
    check_near(frame->picture_hfov_rad, 1.2043, 1e-5, "hfov in RADIANS, not degrees");
    check_near(frame->picture_vfov_rad, 0.7376, 1e-5, "vfov in RADIANS, not degrees");
    check_near(frame->zoom_level, 2.0, 1e-6, "zoom level");
    check_near(frame->exposure_time_ms, 2.5, 1e-6, "exposure time");
    check_eq(frame->iso_gain, uint32_t{100}, "ISO gain");
    check_near(frame->frame_quat.norm(), 1.0, 1e-4, "frame quaternion is unit norm");
    check_near(frame->camera_location.altitude_egm96_m, 100.0, 1e-9, "camera EGM96 altitude");
    check_near(frame->principal_point_x, 0.5, 1e-6, "principal point x");

    // Links, LFIC, tracking.
    check_eq(frame->wifi_links.size(), size_t{1}, "one Wi-Fi link");
    if (!frame->wifi_links.empty()) {
        check_eq(frame->wifi_links[0].goodput_kbps, uint32_t{15000}, "link goodput");
        check_eq(frame->wifi_links[0].quality, uint32_t{4}, "link quality");
        // sint32/ZigZag: a plain varint read would give 109.
        check_eq(frame->wifi_links[0].rssi_dbm, -55, "RSSI is negative (ZigZag decoded)");
    }
    check_eq(frame->lfic.size(), size_t{1}, "one LFIC entry");
    if (!frame->lfic.empty()) {
        check(frame->lfic[0].type == LficType::Cot, "LFIC entry is cursor-on-target");
        check_near(frame->lfic[0].location.latitude_deg, 45.6, 1e-9, "LFIC latitude");
        check_near(frame->lfic[0].horizontal_radial_accuracy_m, 4.0, 1e-6, "LFIC radial accuracy");
    }
    check(frame->tracking.has_value(), "tracking present");
    if (frame->tracking && frame->tracking->target) {
        check_near(frame->tracking->target->width, 0.5, 1e-6, "tracking box width");
        check_eq(frame->tracking->target->uid, uint32_t{42}, "tracking target uid");
    }

    // Thermal (field 7) is dropped by design — it must be recorded as skipped
    // rather than silently vanishing or failing the parse.
    check_eq(frame->skipped_field_numbers.size(), size_t{1}, "one skipped top-level field");
    if (!frame->skipped_field_numbers.empty()) {
        check_eq(frame->skipped_field_numbers[0], uint32_t{7}, "the skipped field is thermal (7)");
    }

    // Every sample must decode, and the monotonic clock must advance.
    uint64_t previous = 0;
    for (uint32_t i = 0; i < extractor->sample_count(); ++i) {
        auto s = extractor->extract_sample(i);
        if (!s) {
            check(false, "sample " + std::to_string(i) + " extracts");
            continue;
        }
        auto f = parse_proto(s->data, &err);
        if (!f) {
            check(false, "sample " + std::to_string(i) + " decodes: " + to_string(err));
            continue;
        }
        if (i > 0) check(f->camera_timestamp_us > previous, "timestamps strictly increase");
        previous = f->camera_timestamp_us;
    }
}

void check_wire_edges() {
    ParrotProtoError err{};

    // An empty message is valid proto3: every field defaults. It must not be an
    // error, because a frame with nothing set is indistinguishable from absent.
    auto empty = parse_proto({}, &err);
    check(empty.has_value(), "empty sample decodes to a default frame");
    if (empty) {
        check_eq(empty->battery_percentage, 0, "default frame has zeroed fields");
        check(empty->skipped_field_numbers.empty(), "default frame skipped nothing");
    }

    // Unknown fields are the point of the format: a newer SDK's additions must
    // skip, not fail. One of each wire type.
    {
        std::vector<uint8_t> buf;
        put_tag(buf, 900, 0);
        put_varint(buf, 300);                     // varint
        put_tag(buf, 901, 5);
        buf.insert(buf.end(), {1, 2, 3, 4});      // fixed32
        put_tag(buf, 902, 1);
        buf.insert(buf.end(), {1, 2, 3, 4, 5, 6, 7, 8});  // fixed64
        auto tail = msg(903, {9, 9, 9});                  // length-delimited
        buf.insert(buf.end(), tail.begin(), tail.end());
        auto frame = parse_proto(buf, &err);
        check(frame.has_value(), "unknown fields of every wire type are skipped");
        if (frame) {
            check_eq(frame->skipped_field_numbers.size(), size_t{4}, "all four recorded as skipped");
        }
    }

    // A length that runs past the end must be rejected, not clamped.
    {
        std::vector<uint8_t> buf;
        put_tag(buf, 1, 2);
        put_varint(buf, 50);  // claims 50 bytes
        buf.insert(buf.end(), {1, 2, 3});
        auto frame = parse_proto(buf, &err);
        check(!frame.has_value(), "over-long length is rejected");
        check(err == ParrotProtoError::Truncated, "over-long length reports Truncated");
    }

    // A varint cut off by the end of the sample. Field 900 is unknown, so this
    // goes through the skip path — field 1 would be rejected earlier, for the
    // wire type, and never reach the varint reader.
    {
        std::vector<uint8_t> buf;
        put_tag(buf, 900, 0);
        buf.push_back(0x80);  // continuation bit set, nothing follows
        auto frame = parse_proto(buf, &err);
        check(!frame.has_value(), "truncated varint is rejected");
        check(err == ParrotProtoError::Truncated, "truncated varint reports Truncated");
    }

    // A message field carrying a non-message wire type must be rejected rather
    // than reinterpreted. Without the check, wire 0 here would read the next
    // byte as a submessage length.
    {
        std::vector<uint8_t> buf;
        put_tag(buf, 1, 0);  // field 1 is DroneMetadata, a message
        put_varint(buf, 0);
        auto frame = parse_proto(buf, &err);
        check(!frame.has_value(), "varint on a message field is rejected");
        check(err == ParrotProtoError::BadWireType, "message-field wire mismatch reports BadWireType");
    }

    // Wire types 3/4 are proto2 groups and 6/7 are undefined; either means we
    // have lost sync, and guessing a length would produce silent garbage.
    for (uint32_t wire : {3U, 4U, 6U, 7U}) {
        std::vector<uint8_t> buf;
        put_tag(buf, 1, wire);
        buf.push_back(0);
        auto frame = parse_proto(buf, &err);
        check(!frame.has_value(), "wire type " + std::to_string(wire) + " is rejected");
        check(err == ParrotProtoError::BadWireType,
              "wire type " + std::to_string(wire) + " reports BadWireType");
    }

    // Eleven continuation bytes is not a number we should try to interpret.
    {
        std::vector<uint8_t> buf;
        put_tag(buf, 900, 0);
        for (int i = 0; i < 11; ++i) buf.push_back(0x80);
        buf.push_back(0x01);
        auto frame = parse_proto(buf, &err);
        check(!frame.has_value(), "over-long varint is rejected");
        check(err == ParrotProtoError::VarintTooLong, "over-long varint reports VarintTooLong");
    }

    // Field number 0 does not exist; a zero tag usually means padding read as
    // a message.
    {
        std::vector<uint8_t> buf{0x00};
        auto frame = parse_proto(buf, &err);
        check(!frame.has_value(), "field number 0 is rejected");
    }

    // ZigZag at the boundary: -1 encodes as 1, and a plain varint reading would
    // report 1 rather than -1.
    {
        std::vector<uint8_t> drone;
        put_tag(drone, 5, 0);
        put_varint(drone, 1);  // ZigZag(-1)
        auto frame = parse_proto(msg(1, drone), &err);
        check(frame.has_value(), "ZigZag battery decodes");
        if (frame) check_eq(frame->battery_percentage, -1, "ZigZag(1) is -1, not 1");
    }

    // A truncated nested message must fail the whole parse rather than yielding
    // a half-populated frame.
    {
        std::vector<uint8_t> buf;
        put_tag(buf, 1, 2);
        put_varint(buf, 4);
        put_tag(buf, 3, 1);   // a double, but only two bytes follow
        buf.insert(buf.end(), {1, 2, 3});
        auto frame = parse_proto(buf, &err);
        check(!frame.has_value(), "truncated nested message fails the parse");
    }

    // A oneof carrying starfish rather than wifi must not be counted as a
    // Wi-Fi link.
    {
        std::vector<uint8_t> link;
        auto starfish = msg(2, {});
        link.insert(link.end(), starfish.begin(), starfish.end());
        auto frame = parse_proto(msg(3, link), &err);
        check(frame.has_value(), "starfish link decodes");
        if (frame) check(frame->wifi_links.empty(), "starfish is not counted as a Wi-Fi link");
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: " << argv[0] << " <fixture-dir>\n";
        return 2;
    }
    check_fixture_samples(argv[1]);
    check_wire_edges();

    if (g_failures > 0) {
        std::cerr << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "all proto checks passed\n";
    return 0;
}
