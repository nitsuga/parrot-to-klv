// Copyright 2026 Eric Schneider
// Licensed under the Apache License, Version 2.0
//
// Covers the MP4 side of extraction: the box scan that recovers what minimp4
// discards, and sample extraction on top of it. Fixtures are synthetic (see
// test/make_fixture.py) because real Parrot footage cannot be committed.

#include "mp4_box_scan.hpp"
#include "mp4_metadata_extractor.hpp"

#include <cstdio>
#include <iostream>
#include <string>

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

std::string fixture(const char* dir, const char* name) {
    return std::string(dir) + "/" + name;
}

/// A sample arrived whole and uncorrupted if it still declares its own length.
///
/// Checked structurally rather than against expected bytes: the payload's field
/// values belong to parrot_v3_test, and duplicating the generator's constants
/// here would just make both tests fail for one reason.
void check_self_consistent(const std::vector<uint8_t>& data, const std::string& what) {
    if (data.size() < 4) {
        std::cerr << "FAIL: " << what << " — too short at " << data.size() << " bytes\n";
        ++g_failures;
        return;
    }
    const unsigned id = (unsigned(data[0]) << 8) | data[1];
    const unsigned length_words = (unsigned(data[2]) << 8) | data[3];
    if (id != 0x5033) {
        std::cerr << "FAIL: " << what << " — identifier is not 0x5033\n";
        ++g_failures;
    }
    if (4 + length_words * 4 != data.size()) {
        std::cerr << "FAIL: " << what << " — declared " << (4 + length_words * 4)
                  << " bytes, extracted " << data.size() << "\n";
        ++g_failures;
    }
}

void test_box_scan(const char* dir) {
    FILE* file = std::fopen(fixture(dir, "parrot_v3.mp4").c_str(), "rb");
    check(file != nullptr, "fixture opens");
    if (!file) return;

    const auto tracks = scan_mp4_tracks(file);
    std::fclose(file);

    check_eq(tracks.size(), size_t{2}, "track count");
    if (tracks.size() != 2) return;

    check_eq(tracks[0].handler_type, kHandlerVide, "track 0 handler is 'vide'");
    check_eq(tracks[0].track_id, uint32_t{1}, "track 0 id");

    check_eq(tracks[1].handler_type, kHandlerMeta, "track 1 handler is 'meta'");
    check_eq(tracks[1].track_id, uint32_t{2}, "track 1 id");
    check_eq(tracks[1].sample_entry_format, kSampleEntryMett, "track 1 sample entry is 'mett'");
    check_eq(tracks[1].mime_format,
             std::string("application/octet-stream;type=com.parrot.videometadata3"),
             "track 1 mime");
    check_eq(tracks[1].cdsc_track_ids.size(), size_t{1}, "track 1 cdsc count");
    if (!tracks[1].cdsc_track_ids.empty()) {
        check_eq(tracks[1].cdsc_track_ids[0], uint32_t{1}, "track 1 describes track 1");
    }
}

void test_format_detection(const char* dir) {
    struct Case {
        const char* file;
        ParrotFormat format;
    };
    const Case cases[] = {
        {"parrot_v3.mp4", ParrotFormat::V3},
        {"parrot_proto.mp4", ParrotFormat::Proto},
        // content_encoding present, so the MIME type is the second string.
        {"parrot_v3_encoded.mp4", ParrotFormat::V3},
    };

    for (const auto& c : cases) {
        auto extractor = Mp4MetadataExtractor::open(fixture(dir, c.file));
        check(extractor.has_value(), std::string(c.file) + " opens");
        if (!extractor) continue;
        check_eq(to_string(extractor->track_info().format), to_string(c.format),
                 std::string(c.file) + " format");
        check_eq(extractor->sample_count(), uint32_t{8}, std::string(c.file) + " sample count");
    }
}

void test_rejects_non_parrot(const char* dir) {
    // A 'meta' track whose MIME type is not Parrot's must be refused. Guessing
    // here would hand foreign bytes to a Parrot parser.
    auto extractor = Mp4MetadataExtractor::open(fixture(dir, "foreign_meta.mp4"));
    check(!extractor.has_value(), "foreign metadata track is rejected");

    auto missing = Mp4MetadataExtractor::open(fixture(dir, "does_not_exist.mp4"));
    check(!missing.has_value(), "missing file is rejected");
}

void test_sample_extraction(const char* dir) {
    auto extractor = Mp4MetadataExtractor::open(fixture(dir, "parrot_v3.mp4"));
    check(extractor.has_value(), "v3 fixture opens");
    if (!extractor) return;

    auto first = extractor->extract_sample(0);
    check(first.has_value(), "sample 0 extracts");
    if (first) {
        check_self_consistent(first->data, "sample 0 round-trip");
        check_eq(first->timestamp_us, uint64_t{0}, "sample 0 timestamp");
    }

    // 1000 units at timescale 30000 => 33333 µs per sample.
    auto third = extractor->extract_sample(3);
    check(third.has_value(), "sample 3 extracts");
    if (third) {
        check_self_consistent(third->data, "sample 3 round-trip");
        check_eq(third->timestamp_us, uint64_t{100000}, "sample 3 timestamp");
    }

    check(!extractor->extract_sample(8).has_value(), "past-the-end sample is refused");

    const auto all = extractor->extract_all_samples();
    check_eq(all.size(), size_t{8}, "extract_all_samples count");
}

}  // namespace

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <fixture-dir>\n";
        return 2;
    }
    const char* dir = argv[1];

    test_box_scan(dir);
    test_format_detection(dir);
    test_rejects_non_parrot(dir);
    test_sample_extraction(dir);

    if (g_failures > 0) {
        std::cerr << "\n" << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "\nAll checks passed\n";
    return 0;
}
