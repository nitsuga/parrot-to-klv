// Copyright 2026 Eric Schneider
// Licensed under the Apache License, Version 2.0
//
// The scrubber's job is to make a real recording safe to share. A bug here
// leaks the operator's location, so the checks below are about what must be
// *gone* as much as what must survive.

#include "mp4_metadata_extractor.hpp"
#include "parrot_scrub.hpp"
#include "parrot_v3.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <utility>
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

void check_near(double actual, double expected, double tol, const std::string& what) {
    if (!(std::abs(actual - expected) <= tol)) {
        std::cerr << "FAIL: " << what << " — expected " << expected << " ±" << tol << ", got "
                  << actual << "\n";
        ++g_failures;
    }
}

std::vector<uint8_t> read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

std::vector<uint8_t> read_file(const std::filesystem::path& path) {
    return read_file(path.string());
}

bool refusal_names_the_alias(const std::string& error) {
    return error.find("input") != std::string::npos &&
           error.find("output") != std::string::npos &&
           (error.find("same") != std::string::npos ||
            error.find("distinct") != std::string::npos);
}

std::string g_dir;
std::string g_tmp;

std::string fixture(const char* name) { return g_dir + "/" + name; }
std::string temp(const char* name) { return g_tmp + "/" + name; }

/// `scrub_file` must refuse aliases before it can overwrite the recording.
/// All inputs here are disposable copies: a regression must not modify a
/// committed fixture while demonstrating the safety property.
void test_refuses_input_output_aliases() {
    namespace fs = std::filesystem;
    const fs::path root = fs::path(g_tmp) / "scrub_input_output_aliases";
    std::error_code ec;
    fs::remove_all(root, ec);
    check(!ec, "scrub alias test setup removes its old disposable directory");
    if (ec) return;
    fs::create_directories(root / "lexical", ec);
    check(!ec, "scrub alias test setup creates its disposable directory");
    if (ec) return;

    const fs::path source = fixture("parrot_v3_extensions.mp4");
    const auto exercise = [&](const char* label, const fs::path& output) {
        const fs::path input = root / (std::string(label) + ".mp4");
        ec.clear();
        fs::copy_file(source, input, fs::copy_options::overwrite_existing, ec);
        check(!ec, std::string("scrub ") + label + ": creates disposable input copy");
        if (ec) return;

        const auto before = read_file(input);
        const auto report = scrub_file(input.string(), output.string(), ScrubOptions{});
        check(!report.ok, std::string("scrub ") + label + ": refuses aliased output");
        check(refusal_names_the_alias(report.error),
              std::string("scrub ") + label + ": refusal explains input/output aliasing");
        check(read_file(input) == before,
              std::string("scrub ") + label + ": input bytes are unchanged after refusal");
    };

    const fs::path exact = root / "exact.mp4";
    exercise("exact", exact);
    exercise("lexical", root / "lexical" / ".." / "lexical.mp4");

    const fs::path symlink_input = root / "symlink.mp4";
    fs::copy_file(source, symlink_input, fs::copy_options::overwrite_existing, ec);
    check(!ec, "scrub symlink: creates disposable input copy");
    if (!ec) {
        const fs::path symlink_output = root / "symlink-output.mp4";
        fs::create_symlink(symlink_input.filename(), symlink_output, ec);
#ifdef _WIN32
        if (ec) {
            std::cout << "SKIP: scrub symlink alias test unavailable: " << ec.message() << "\n";
        } else
#endif
        {
            check(!ec, "scrub symlink: creates alias (required on this platform)");
            if (!ec) {
                const auto before = read_file(symlink_input);
                const auto report = scrub_file(symlink_input.string(), symlink_output.string(),
                                               ScrubOptions{});
                check(!report.ok, "scrub symlink: refuses aliased output");
                check(refusal_names_the_alias(report.error),
                      "scrub symlink: refusal explains input/output aliasing");
                check(read_file(symlink_input) == before,
                      "scrub symlink: input bytes are unchanged after refusal");
            }
        }
    }

    const fs::path hardlink_input = root / "hardlink.mp4";
    fs::copy_file(source, hardlink_input, fs::copy_options::overwrite_existing, ec);
    check(!ec, "scrub hard-link: creates disposable input copy");
    if (!ec) {
        const fs::path hardlink_output = root / "hardlink-output.mp4";
        fs::create_hard_link(hardlink_input, hardlink_output, ec);
#ifdef _WIN32
        if (ec) {
            std::cout << "SKIP: scrub hard-link alias test unavailable: " << ec.message() << "\n";
        } else
#endif
        {
            check(!ec, "scrub hard-link: creates alias (required on this platform)");
            if (!ec) {
                const auto before = read_file(hardlink_input);
                const auto report = scrub_file(hardlink_input.string(), hardlink_output.string(),
                                               ScrubOptions{});
                check(!report.ok, "scrub hard-link: refuses aliased output");
                check(refusal_names_the_alias(report.error),
                      "scrub hard-link: refusal explains input/output aliasing");
                check(read_file(hardlink_input) == before,
                      "scrub hard-link: input bytes are unchanged after refusal");
            }
        }
    }
}

/// The coordinates make_fixture.py encodes, which must not survive a scrub.
constexpr double kOriginalLat = 48.858370;
constexpr double kOriginalLon = 2.294481;

void test_scrub_replaces_location() {
    const std::string out = temp("scrubbed.mp4");
    ScrubOptions opts;
    opts.values.latitude_deg = 12.5;
    opts.values.longitude_deg = -34.25;
    opts.values.altitude_amsl_m = 100.0;

    const auto report = scrub_file(fixture("parrot_v3_extensions.mp4"), out, opts);
    check(report.ok, "scrub succeeds");
    if (!report.ok) {
        std::cerr << "  (" << report.error << ")\n";
        return;
    }
    check_eq(report.samples_rewritten, uint32_t{8}, "every sample rewritten");
    check_eq(report.automation_extensions_rewritten, uint32_t{8}, "automation exts rewritten");
    check_eq(report.geotag_boxes_rewritten, uint32_t{1}, "geotag rewritten");
    // '©day', '©too', '©ART', '©nam' — the identity/date keys the fixture's
    // session_udta() writes. '©mak', '©mod', '©swr' are not identity-bearing
    // and are left alone.
    check_eq(report.identity_boxes_rewritten, uint32_t{4}, "identity boxes rewritten");

    // Size must be identical — the scrub patches in place so every box offset
    // and sample table in the file stays valid.
    check_eq(read_file(out).size(), read_file(fixture("parrot_v3_extensions.mp4")).size(),
             "output is the same size as the input");

    auto extractor = Mp4MetadataExtractor::open(out);
    check(extractor.has_value(), "scrubbed file still opens");
    if (!extractor) return;

    for (const auto& sample : extractor->extract_all_samples()) {
        auto frame = parse_v3(sample.data);
        check(frame.has_value(), "scrubbed sample still parses");
        if (!frame) return;

        check_near(frame->latitude_deg, 12.5, 1e-5, "latitude replaced");
        check_near(frame->longitude_deg, -34.25, 1e-5, "longitude replaced");
        check_near(frame->altitude_amsl_m, 100.0, 1e-2, "altitude replaced");

        check(frame->automation.has_value(), "automation extension survives");
        if (frame->automation) {
            check_near(frame->automation->framing_target_latitude_deg, 12.5, 1e-5,
                       "framing target latitude replaced");
            check_near(frame->automation->flight_destination_longitude_deg, -34.25, 1e-5,
                       "flight destination longitude replaced");
        }

        // Everything that is not location must come through untouched — that is
        // the reason to scrub a real file instead of synthesizing one.
        check_eq(int{frame->sv_count}, 11, "satellite count preserved");
        check_eq(int{frame->battery_percentage}, 77, "battery preserved");
        check_eq(int{frame->wifi_rssi_dbm}, -47, "rssi preserved");
        check_near(frame->north_speed_mps, 3.5, 1e-2, "north speed preserved");
        check_near(frame->drone_quat.norm(), 1.0, 1e-3, "attitude preserved");
        check_near(frame->ground_distance_m, 12.5, 1e-3, "AGL preserved");
        check(frame->thermal.has_value(), "thermal extension preserved");
    }
}

void test_original_coordinates_absent() {
    // The strongest check available: the encoded form of the real coordinates
    // must not appear anywhere in the output, in any sample or any box.
    const std::string out = temp("scrubbed2.mp4");
    ScrubOptions opts;
    opts.values.latitude_deg = 1.0;
    opts.values.longitude_deg = 2.0;
    const auto report = scrub_file(fixture("parrot_v3_extensions.mp4"), out, opts);
    check(report.ok, "scrub succeeds");
    if (!report.ok) return;

    const auto data = read_file(out);
    const auto encode_q10_22 = [](double deg) {
        const auto v = static_cast<uint32_t>(std::lround(deg * 4194304.0));
        return std::vector<uint8_t>{
            static_cast<uint8_t>(v >> 24), static_cast<uint8_t>(v >> 16),
            static_cast<uint8_t>(v >> 8), static_cast<uint8_t>(v)};
    };
    for (const auto& [label, deg] : {std::pair<const char*, double>{"latitude", kOriginalLat},
                                     {"longitude", kOriginalLon}}) {
        const auto needle = encode_q10_22(deg);
        const bool found =
            std::search(data.begin(), data.end(), needle.begin(), needle.end()) != data.end();
        check(!found, std::string("original ") + label + " byte pattern is gone");
    }

    // The geotag string must not retain the original digits either.
    const std::string text(reinterpret_cast<const char*>(data.data()), data.size());
    check(text.find("48.85837") == std::string::npos, "geotag latitude digits gone");
    check(text.find("002.29448") == std::string::npos, "geotag longitude digits gone");
}

void test_identity_and_date_scrubbed() {
    const std::string out = temp("scrubbed_identity.mp4");
    const auto report = scrub_file(fixture("parrot_v3_extensions.mp4"), out, ScrubOptions{});
    check(report.ok, "scrub succeeds");
    if (!report.ok) return;

    const auto data = read_file(out);
    const std::string text(reinterpret_cast<const char*>(data.data()), data.size());
    // Values session_udta() in make_fixture.py writes — none may survive.
    check(text.find("ANAFI-G059745") == std::string::npos, "friendly name gone");
    check(text.find("PI040416BA8G059745") == std::string::npos, "serial number gone");
    check(text.find("2018-10-06") == std::string::npos, "media date digits gone");
    check(text.find("06 Oct 2018") == std::string::npos, "title's restated date gone");
    // Maker and model are not identity-bearing (model feeds ST 0601 Item 10)
    // and must survive untouched.
    check(text.find("Parrot") != std::string::npos, "maker preserved");
    check(text.find("Anafi") != std::string::npos, "model preserved");
}

void test_zero_media() {
    const std::string out = temp("scrubbed_media.mp4");
    ScrubOptions opts;
    opts.zero_media = true;
    const auto report = scrub_file(fixture("parrot_v3.mp4"), out, opts);
    check(report.ok, "scrub with --zero-media succeeds");
    if (!report.ok) return;
    check(report.media_bytes_zeroed > 0, "media payload was zeroed");

    // The video fixture's samples all start 0xDE 0xAD 0xBE 0xEF.
    const auto data = read_file(out);
    const std::vector<uint8_t> needle{0xDE, 0xAD, 0xBE, 0xEF};
    check(std::search(data.begin(), data.end(), needle.begin(), needle.end()) == data.end(),
          "video payload is gone");

    // Telemetry must survive media zeroing.
    auto extractor = Mp4MetadataExtractor::open(out);
    check(extractor.has_value(), "file still opens after zeroing media");
    if (extractor) {
        check_eq(extractor->sample_count(), uint32_t{8}, "telemetry samples intact");
    }
}

void test_refuses_what_it_cannot_scrub() {
    // A proto file must be refused outright. Passing it through would produce an
    // output that looks scrubbed while still carrying real coordinates — worse
    // than failing.
    const std::string out = temp("should_not_exist.mp4");
    std::remove(out.c_str());

    const auto report = scrub_file(fixture("parrot_proto.mp4"), out, ScrubOptions{});
    check(!report.ok, "proto format is refused");
    check(report.error.find("proto") != std::string::npos, "error names the format");

    std::ifstream probe(out, std::ios::binary);
    check(!probe.good(), "no output file is written on refusal");

    // Likewise a file with no Parrot telemetry at all.
    const auto foreign = scrub_file(fixture("foreign_meta.mp4"), temp("foreign_out.mp4"),
                                    ScrubOptions{});
    check(!foreign.ok, "non-Parrot file is refused");
}

void test_unknown_extension_payload_is_cleared() {
    // An extension we cannot decode is an extension we cannot inspect, so it
    // must be blanked rather than trusted. Real ANAFI files carry an undocumented
    // 0x4536 in every sample; if it holds position, passing it through would leak
    // location out of a file the tool reported as scrubbed.
    const std::string out = temp("unknown_ext_scrubbed.mp4");
    ScrubOptions opts;
    opts.values.latitude_deg = 1.0;
    opts.values.longitude_deg = 2.0;

    const auto report = scrub_file(fixture("parrot_v3_unknown_ext.mp4"), out, opts);
    check(report.ok, "scrub succeeds with an unknown extension present");
    if (!report.ok) return;

    check_eq(report.unknown_extension_ids.size(), size_t{1}, "one unknown extension id");
    if (report.unknown_extension_ids.size() == 1) {
        check_eq(report.unknown_extension_ids[0], uint16_t{0x4536}, "unknown id reported");
    }
    check_eq(report.unknown_extension_payloads_zeroed, uint32_t{8}, "every payload blanked");

    // The coordinates hidden in that extension must be gone from the output.
    const auto data = read_file(out);
    const auto v_lat = static_cast<uint32_t>(std::lround(kOriginalLat * 4194304.0));
    const auto v_lon = static_cast<uint32_t>(std::lround(kOriginalLon * 4194304.0));
    const std::vector<uint8_t> needle{
        static_cast<uint8_t>(v_lat >> 24), static_cast<uint8_t>(v_lat >> 16),
        static_cast<uint8_t>(v_lat >> 8), static_cast<uint8_t>(v_lat),
        static_cast<uint8_t>(v_lon >> 24), static_cast<uint8_t>(v_lon >> 16),
        static_cast<uint8_t>(v_lon >> 8), static_cast<uint8_t>(v_lon)};
    check(std::search(data.begin(), data.end(), needle.begin(), needle.end()) == data.end(),
          "coordinates inside the unknown extension are gone");

    check_eq(data.size(), read_file(fixture("parrot_v3_unknown_ext.mp4")).size(),
             "blanking preserves file size");

    // The sample must still parse: id and length survive, only the payload went.
    auto extractor = Mp4MetadataExtractor::open(out);
    check(extractor.has_value(), "file still opens");
    if (extractor) {
        auto frame = parse_v3(extractor->extract_all_samples().at(0).data);
        check(frame.has_value(), "sample still parses after blanking");
        if (frame) {
            check_eq(frame->skipped_extension_ids.size(), size_t{1},
                     "extension is still structurally present");
        }
    }

    // Opting out must be possible, but must say plainly what it costs.
    ScrubOptions keep = opts;
    keep.zero_unknown_extensions = false;
    const auto kept = scrub_file(fixture("parrot_v3_unknown_ext.mp4"), temp("kept.mp4"), keep);
    check(kept.ok, "opt-out scrub succeeds");
    check_eq(kept.unknown_extension_payloads_zeroed, uint32_t{0}, "nothing blanked on opt-out");
    bool warned = false;
    for (const auto& w : kept.warnings) {
        if (w.find("NOT scrubbed") != std::string::npos) warned = true;
    }
    check(warned, "opt-out warns the output is not scrubbed");
}

void test_warns_about_imagery() {
    const auto report = scrub_file(fixture("parrot_v3.mp4"), temp("warn.mp4"), ScrubOptions{});
    check(report.ok, "scrub succeeds");
    // Without --zero-media the imagery still shows the location; that must be
    // said, not left for the user to realize.
    bool mentions_imagery = false;
    for (const auto& w : report.warnings) {
        if (w.find("imagery") != std::string::npos) mentions_imagery = true;
    }
    check(mentions_imagery, "warns that the imagery is not scrubbed");
}

}  // namespace

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <fixture-dir> <temp-dir>\n";
        return 2;
    }
    g_dir = argv[1];
    g_tmp = argv[2];

    test_refuses_input_output_aliases();
    test_scrub_replaces_location();
    test_original_coordinates_absent();
    test_identity_and_date_scrubbed();
    test_zero_media();
    test_refuses_what_it_cannot_scrub();
    test_unknown_extension_payload_is_cleared();
    test_warns_about_imagery();

    if (g_failures > 0) {
        std::cerr << "\n" << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "\nAll checks passed\n";
    return 0;
}
