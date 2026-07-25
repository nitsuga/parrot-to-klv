// Copyright 2026 Eric Schneider
// Licensed under the Apache License, Version 2.0
//
// Decodes the v3 samples in the synthetic fixtures and checks them field by
// field against the values test/make_fixture.py encodes. Keep the two in step:
// the expectations below are the generator's inputs, not the parser's output.

#include "mp4_metadata_extractor.hpp"
#include "parrot_v3.hpp"

#include <cmath>
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

/// Fixed-point round-trips are exact only to the encoding's resolution, so
/// compare within it rather than demanding equality.
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

std::string fixture(const char* dir, const char* name) {
    return std::string(dir) + "/" + name;
}

std::vector<MetadataSample> samples_of(const char* dir, const char* name) {
    auto extractor = Mp4MetadataExtractor::open(fixture(dir, name));
    if (!extractor) {
        std::cerr << "FAIL: could not open " << name << "\n";
        ++g_failures;
        return {};
    }
    return extractor->extract_all_samples();
}

// Resolutions of the fixed-point encodings the base structure uses.
constexpr double kTolQ8_8 = 1.0 / 256;
constexpr double kTolQ2_14 = 1.0 / 16384;
constexpr double kTolQ10_22 = 1.0 / 4194304;
constexpr double kTolQ16_8 = 1.0 / 256;
constexpr double kTolQ16_16 = 1.0 / 65536;
constexpr double kTolQ11_5 = 1.0 / 32;

void test_base_fields(const char* dir) {
    const auto samples = samples_of(dir, "parrot_v3.mp4");
    check_eq(samples.size(), size_t{8}, "v3 fixture sample count");
    if (samples.empty()) return;

    ParrotV3Error err{};
    auto frame = parse_v3(samples[0].data, &err);
    check(frame.has_value(), "sample 0 parses");
    if (!frame) {
        std::cerr << "  (" << to_string(err) << ")\n";
        return;
    }

    check_near(frame->ground_distance_m, 12.5, kTolQ16_16, "ground_distance");
    check_near(frame->latitude_deg, 48.858370, kTolQ10_22, "latitude");
    check_near(frame->longitude_deg, 2.294481, kTolQ10_22, "longitude");
    check_near(frame->altitude_amsl_m, 135.25, kTolQ16_8, "altitude AMSL");
    check_eq(int{frame->sv_count}, 11, "sv_count");

    check_near(frame->north_speed_mps, 3.5, kTolQ8_8, "north speed");
    check_near(frame->east_speed_mps, -1.25, kTolQ8_8, "east speed (negative)");
    check_near(frame->down_speed_mps, 0.75, kTolQ8_8, "down speed");
    check(frame->air_speed_mps.has_value(), "air speed present");
    if (frame->air_speed_mps) {
        check_near(*frame->air_speed_mps, 4.0, kTolQ8_8, "air speed");
    }

    // 60 degrees about Z, encoded Q2.14.
    check_near(frame->drone_quat.w, 0.86602540, kTolQ2_14, "drone quat w");
    check_near(frame->drone_quat.z, 0.5, kTolQ2_14, "drone quat z");
    check_near(frame->drone_quat.norm(), 1.0, 1e-3, "drone quat is unit-norm");
    check_near(frame->frame_base_quat.w, 1.0, kTolQ2_14, "frame_base quat is identity");
    check_near(frame->frame_quat.norm(), 1.0, 1e-3, "frame quat is unit-norm");

    check_near(frame->exposure_time_ms, 8.25, kTolQ8_8, "exposure time");
    check_eq(int{frame->gain}, 200, "gain");
    check_near(frame->awb_r_gain, 1.5, kTolQ2_14, "awb r gain");
    check_near(frame->awb_b_gain, 1.25, kTolQ2_14, "awb b gain");
    check_near(frame->picture_hfov_deg, 69.5, kTolQ8_8, "hfov (already degrees)");
    check_near(frame->picture_vfov_deg, 43.75, kTolQ8_8, "vfov");

    check_eq(frame->link_goodput_kbps, uint32_t{25000}, "link goodput");
    check_eq(int{frame->link_quality}, 4, "link quality");
    check_eq(int{frame->wifi_rssi_dbm}, -47, "wifi rssi (signed)");
    check_eq(int{frame->battery_percentage}, 77, "battery percentage");

    // state/mode pack a flag into the top bit of each byte.
    check_eq(to_string(frame->flying_state), std::string("flying"), "flying state");
    check(frame->binning, "binning flag");
    check_eq(to_string(frame->piloting_mode), std::string("flight-plan"), "piloting mode");
    check(frame->animation_in_progress, "animation flag");
}

void test_timestamp_extension(const char* dir) {
    const auto samples = samples_of(dir, "parrot_v3.mp4");
    if (samples.size() < 4) return;

    for (size_t i = 0; i < samples.size(); ++i) {
        auto frame = parse_v3(samples[i].data);
        check(frame.has_value(), "sample parses");
        if (!frame) return;
        check(frame->frame_timestamp_us.has_value(), "timestamp extension present");
        if (frame->frame_timestamp_us) {
            check_eq(*frame->frame_timestamp_us, uint64_t{33333} * i, "frame timestamp");
        }
        check(frame->skipped_extension_ids.empty(), "no unknown extensions");
        check(!frame->automation.has_value(), "no automation extension in this fixture");
    }
}

void test_all_extensions(const char* dir) {
    const auto samples = samples_of(dir, "parrot_v3_extensions.mp4");
    check_eq(samples.size(), size_t{8}, "extensions fixture sample count");
    if (samples.empty()) return;

    auto frame = parse_v3(samples[0].data);
    check(frame.has_value(), "sample with every extension parses");
    if (!frame) return;

    check(frame->frame_timestamp_us.has_value(), "timestamp extension decoded");

    check(frame->automation.has_value(), "automation extension decoded");
    if (frame->automation) {
        const auto& a = *frame->automation;
        check_near(a.framing_target_latitude_deg, 48.86, kTolQ10_22, "framing target lat");
        check_near(a.framing_target_altitude_m, 140.0, kTolQ16_16, "framing target alt");
        check_near(a.flight_destination_longitude_deg, 2.31, kTolQ10_22, "destination lon");
        check_eq(int{a.animation}, 5, "automation animation");
        check(a.follow_me, "follow-me flag");
        check(!a.look_at_me, "look-at-me flag clear");
        check(a.angle_locked, "angle-locked flag");
    }

    check(frame->thermal.has_value(), "thermal extension decoded");
    if (frame->thermal) {
        const auto& t = *frame->thermal;
        check_near(t.min.temperature_k, 283.15, kTolQ11_5, "thermal min temp");
        check_near(t.max.temperature_k, 312.65, kTolQ11_5, "thermal max temp");
        check_near(t.max.x, 0.75, kTolQ11_5, "thermal max x");
        check(t.min.valid, "thermal min valid");
        check(t.max.valid, "thermal max valid");
        check(!t.probe.valid, "thermal probe marked invalid");
        check_eq(int{t.calibration_state}, 2, "thermal calibration state");
    }

    // An unknown extension must be skipped and recorded, never fatal.
    check_eq(frame->skipped_extension_ids.size(), size_t{1}, "one unknown extension skipped");
    if (frame->skipped_extension_ids.size() == 1) {
        check_eq(frame->skipped_extension_ids[0], uint16_t{0x4599}, "unknown extension id");
    }
}

void test_malformed() {
    ParrotV3Error err{};

    std::vector<uint8_t> empty;
    check(!parse_v3(empty, &err).has_value(), "empty sample rejected");
    check_eq(to_string(err), to_string(ParrotV3Error::TooShort), "empty sample error");

    // Right size, wrong identifier — this is what a v2 or proto sample looks
    // like to the v3 parser, and it must not be decoded as v3.
    std::vector<uint8_t> wrong_id(kParrotV3BaseSize, 0);
    wrong_id[0] = 0x50;
    wrong_id[1] = 0x32;  // 'P2'
    check(!parse_v3(wrong_id, &err).has_value(), "v2 identifier rejected");
    check_eq(to_string(err), to_string(ParrotV3Error::BadIdentifier), "wrong id error");

    // Declared length runs past the buffer.
    std::vector<uint8_t> overlong(kParrotV3BaseSize, 0);
    overlong[0] = 0x50;
    overlong[1] = 0x33;
    overlong[2] = 0x00;
    overlong[3] = 0xFF;  // 255 words = 1020 bytes, far beyond the buffer
    check(!parse_v3(overlong, &err).has_value(), "over-long declared length rejected");
    check_eq(to_string(err), to_string(ParrotV3Error::LengthMismatch), "length mismatch error");

    // A base structure one byte short.
    std::vector<uint8_t> truncated(kParrotV3BaseSize - 1, 0);
    truncated[0] = 0x50;
    truncated[1] = 0x33;
    check(!parse_v3(truncated, &err).has_value(), "truncated base rejected");
}

}  // namespace

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <fixture-dir>\n";
        return 2;
    }
    const char* dir = argv[1];

    test_base_fields(dir);
    test_timestamp_extension(dir);
    test_all_extensions(dir);
    test_malformed();

    if (g_failures > 0) {
        std::cerr << "\n" << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "\nAll checks passed\n";
    return 0;
}
