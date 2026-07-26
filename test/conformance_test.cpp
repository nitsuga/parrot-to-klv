// Copyright 2026 Eric Schneider
// Licensed under the Apache License, Version 2.0
//
// A conformance report is meant to be committed while the recording it describes
// is not. The central test is therefore a *negative* one: nothing that could
// locate the flight may appear in the report text, in any encoding.

#include "conformance.hpp"

#include <cmath>
#include <cstdio>
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

template <typename T>
void check_eq(const T& actual, const T& expected, const std::string& what) {
    if (!(actual == expected)) {
        std::cerr << "FAIL: " << what << " — expected " << expected << ", got " << actual << "\n";
        ++g_failures;
    }
}

std::string g_dir;
std::string fixture(const char* name) { return g_dir + "/" + name; }

// What make_fixture.py encodes. None of it may reach a report.
constexpr double kLat = 48.858370;
constexpr double kLon = 2.294481;
constexpr double kAltAmsl = 135.25;

void test_report_holds_no_location() {
    const auto report = run_conformance(fixture("parrot_v3_extensions.mp4"), "fixture");
    check(report.ok, "conformance run succeeds");
    if (!report.ok) {
        std::cerr << "  (" << report.error << ")\n";
        return;
    }
    const std::string text = to_json(report);

    // Decimal renderings at every plausible precision, plus the raw fixed-point
    // integers. A report that leaked position would almost certainly show one.
    std::vector<std::string> forbidden;
    for (int precision = 2; precision <= 7; ++precision) {
        for (double v : {kLat, kLon, kAltAmsl}) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%.*f", precision, v);
            forbidden.emplace_back(buf);
        }
    }
    for (double v : {kLat, kLon}) {
        forbidden.push_back(std::to_string(std::lround(v * 4194304.0)));  // Q10.22
    }
    forbidden.push_back(std::to_string(std::lround(kAltAmsl * 256.0)));  // Q16.8

    for (const auto& needle : forbidden) {
        if (text.find(needle) != std::string::npos) {
            std::cerr << "FAIL: report contains \"" << needle << "\" — possible location leak\n";
            ++g_failures;
        }
    }

    // The range checks must still be present and passing: the report says the
    // coordinates were valid without saying what they were.
    check(report.checks.at("latitude_within_range"), "latitude range check passed");
    check(report.checks.at("longitude_within_range"), "longitude range check passed");
    check(report.checks.at("altitude_within_documented_range"), "altitude range check passed");
    check(text.find("latitude_within_range") != std::string::npos,
          "report records the latitude check by name");
}

void test_report_captures_what_matters() {
    const auto report = run_conformance(fixture("parrot_v3_extensions.mp4"), "fixture");
    check(report.ok, "conformance run succeeds");
    if (!report.ok) return;

    check_eq(report.format, std::string("v3"), "format recorded");
    check_eq(report.sample_count, uint32_t{8}, "sample count");
    check_eq(report.decoded, uint32_t{8}, "all samples decoded");
    check_eq(report.failed, uint32_t{0}, "no failures");
    check(report.decode_errors.empty(), "no decode errors");

    // The undocumented-extension shape is the whole reason to run this against
    // real footage, so it must be captured: id, how often, and payload size.
    check(report.extension_lengths.count(0x4531) == 1, "timestamp extension recorded");
    check(report.extension_lengths.count(0x4599) == 1, "unknown extension recorded");
    if (report.extension_lengths.count(0x4599)) {
        check_eq(report.extension_counts.at(0x4599), uint32_t{8}, "unknown ext seen every sample");
        check_eq(*report.extension_lengths.at(0x4599).begin(), uint16_t{1},
                 "unknown ext length in words");
    }

    check(report.checks.at("quaternions_are_unit_norm"), "quaternion norm check passed");
    check(report.checks.at("declared_length_matches_sample_size"), "declared length check passed");
    check(report.checks.at("timestamp_extension_always_present"), "timestamp always present");
    check(report.checks.at("timestamps_strictly_increasing"), "timestamps increase");

    check_eq(report.sv_count_min, 11, "sv count min");
    check_eq(report.battery_max, 77, "battery max");
    check_eq(report.rssi_min, -47, "rssi min");
    check(report.flying_states_seen.count("flying") == 1, "flying state recorded");
    check_eq(report.timestamp_delta_min, uint64_t{33333}, "timestamp delta");

    check(!report.input_hash.empty(), "input hashed");
    check_eq(report.input_hash.size(), size_t{32}, "xxhash is 32 hex chars (128 bits)");
}

void test_report_is_deterministic() {
    // --check compares text, so two runs over one input must agree exactly.
    const auto a = to_json(run_conformance(fixture("parrot_v3.mp4"), "x"));
    const auto b = to_json(run_conformance(fixture("parrot_v3.mp4"), "x"));
    check(a == b, "report generation is deterministic");

    // A different input must produce a different hash, or --check is worthless.
    const auto c = run_conformance(fixture("parrot_v3_extensions.mp4"), "x");
    const auto d = run_conformance(fixture("parrot_v3.mp4"), "x");
    check(c.input_hash != d.input_hash, "distinct inputs hash differently");
}

void test_non_v3_is_reported_not_guessed() {
    const auto proto = run_conformance(fixture("parrot_proto.mp4"), "proto");
    check(!proto.ok, "proto file is not silently conformance-checked");
    check(proto.format == "proto", "format still identified");

    const auto foreign = run_conformance(fixture("foreign_meta.mp4"), "foreign");
    check(!foreign.ok, "non-Parrot file reports an error");
}

}  // namespace

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <fixture-dir>\n";
        return 2;
    }
    g_dir = argv[1];

    test_report_holds_no_location();
    test_report_captures_what_matters();
    test_report_is_deterministic();
    test_non_v3_is_reported_not_guessed();

    if (g_failures > 0) {
        std::cerr << "\n" << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "\nAll checks passed\n";
    return 0;
}
