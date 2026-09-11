// Copyright 2026 Eric Schneider
// Licensed under the Apache License, Version 2.0
//
// A conformance report is meant to be committed while the recording it describes
// is not. The central test is therefore a *negative* one: nothing that could
// locate the flight may appear in the report text, in any encoding.

#include "conformance.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
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

void test_report_records_decode_failures() {
    const auto report =
        run_conformance(fixture("parrot_v3_video_undecodable_metadata.mp4"), "undecodable");
    check(report.ok, "conformance run completes on partially or fully failing file");
    check_eq(report.sample_count, uint32_t{8}, "sample count");
    check_eq(report.decoded, uint32_t{0}, "no samples decoded");
    check_eq(report.failed, uint32_t{8}, "all 8 samples failed to decode");
    check(!report.decode_errors.empty(), "decode errors recorded");
    check_eq(report.decode_errors.size(), size_t{1}, "one error type recorded");
    const std::string json = to_json(report);
    check(json.find("\"failed\": 8") != std::string::npos, "JSON output includes failed count");
    check(json.find("\"decode_errors\"") != std::string::npos, "JSON output includes decode_errors");
}

void test_timestamp_delta_min_preserves_zero() {
    std::ifstream in(fixture("parrot_v3.mp4"), std::ios::binary);
    check(in.good(), "can open parrot_v3.mp4 fixture");
    if (!in.good()) return;

    const std::vector<uint8_t> valid_bytes((std::istreambuf_iterator<char>(in)),
                                          std::istreambuf_iterator<char>());
    in.close();

    std::error_code ec;
    const std::filesystem::path system_temp = std::filesystem::temp_directory_path(ec);
    check(!ec, "resolved system temporary directory");
    if (ec) return;

    std::string dir_template = (system_temp / "conformance_zero_delta_XXXXXX").string();
    char* created_dir = mkdtemp(dir_template.data());
    check(created_dir != nullptr, "created unique temporary directory");
    if (!created_dir) return;

    const std::filesystem::path temp_dir(created_dir);
    const std::filesystem::path temp_file = temp_dir / "conformance_zero_delta_test.mp4";

    struct Cleanup {
        std::filesystem::path dir;
        ~Cleanup() {
            std::error_code ec_clean;
            std::filesystem::remove_all(dir, ec_clean);
        }
    } cleanup{temp_dir};

    auto write_temp = [&](const std::vector<uint8_t>& data) {
        std::ofstream out(temp_file, std::ios::binary | std::ios::trunc);
        check(out.is_open(), "opened temporary test file for writing");
        out.write(reinterpret_cast<const char*>(data.data()),
                  static_cast<std::streamsize>(data.size()));
        out.flush();
        check(out.good(), "wrote temporary test file successfully");
    };

    // Find all 0x4531 timestamp extensions.
    const std::vector<uint8_t> needle = {0x45, 0x31, 0x00, 0x02};
    std::vector<size_t> ts_offsets;
    for (size_t i = 0; i + needle.size() + 8 <= valid_bytes.size(); ++i) {
        if (std::equal(needle.begin(), needle.end(), valid_bytes.begin() + i)) {
            ts_offsets.push_back(i + needle.size());
        }
    }
    check(ts_offsets.size() >= 4, "found at least 4 timestamp extensions in parrot_v3.mp4");
    if (ts_offsets.size() < 4) return;

    // Case 1: Intermediate zero delta.
    // Duplicate sample 1 timestamp onto sample 2:
    // sample 0: 0, sample 1: 33333 (delta=33333), sample 2: 33333 (delta=0),
    // sample 3: 99999 (delta=66666), ...
    // Before fix, sample 3 saw min==0 and re-seeded min to 66666, ending at 33333.
    {
        auto bytes = valid_bytes;
        std::copy_n(bytes.data() + ts_offsets[1], 8, bytes.data() + ts_offsets[2]);
        write_temp(bytes);

        const auto report = run_conformance(temp_file.string(), "zero_delta_intermediate");
        check(report.ok, "conformance run on intermediate zero-delta fixture succeeds");
        check_eq(report.checks.at("timestamps_strictly_increasing"), false,
                 "intermediate duplicate timestamp marks timestamps_strictly_increasing false");
        check_eq(report.timestamp_delta_min, uint64_t{0},
                 "timestamp_delta_min remains 0 after intermediate zero delta");
        check_eq(report.timestamp_delta_max, uint64_t{66666},
                 "timestamp_delta_max is 66666");
        const std::string json = to_json(report);
        check(json.find("\"timestamp_delta_us\": {\"min\": 0, \"max\": 66666}") != std::string::npos,
              "JSON output contains min: 0, max: 66666");
    }

    // Case 2: Initial zero delta.
    // Duplicate sample 0 timestamp onto sample 1:
    // sample 0: 0, sample 1: 0 (delta=0), sample 2: 66666 (delta=66666), ...
    // Before fix, sample 2 saw min==0 and re-seeded min to 66666, ending at 33333.
    {
        auto bytes = valid_bytes;
        std::copy_n(bytes.data() + ts_offsets[0], 8, bytes.data() + ts_offsets[1]);
        write_temp(bytes);

        const auto report = run_conformance(temp_file.string(), "zero_delta_initial");
        check(report.ok, "conformance run on initial zero-delta fixture succeeds");
        check_eq(report.checks.at("timestamps_strictly_increasing"), false,
                 "initial duplicate timestamp marks timestamps_strictly_increasing false");
        check_eq(report.timestamp_delta_min, uint64_t{0},
                 "timestamp_delta_min remains 0 after initial zero delta");
        check_eq(report.timestamp_delta_max, uint64_t{66666},
                 "timestamp_delta_max is 66666 for initial duplicate");
    }
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
    test_report_records_decode_failures();
    test_timestamp_delta_min_preserves_zero();

    if (g_failures > 0) {
        std::cerr << "\n" << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "\nAll checks passed\n";
    return 0;
}
