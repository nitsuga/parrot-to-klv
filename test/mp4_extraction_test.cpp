// Copyright 2026 Eric Schneider
// Licensed under the Apache License, Version 2.0
//
// Covers the MP4 side of extraction: the box scan that recovers what minimp4
// discards, and sample extraction on top of it. Fixtures are synthetic (see
// test/make_fixture.py) because real Parrot footage cannot be committed.

#include "mp4_box_scan.hpp"
#include "mp4_metadata_extractor.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

using namespace parrot_to_klv;

namespace {

int g_failures = 0;

struct CerrCapture {
    std::streambuf* old;
    std::ostringstream buf;
    CerrCapture() { old = std::cerr.rdbuf(buf.rdbuf()); }
    ~CerrCapture() { std::cerr.rdbuf(old); }
    std::string str() const { return buf.str(); }
};

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

void test_out_of_bounds_chunk_offset_is_rejected(const char* dir) {
    // The second track's 'stco' chunk offset (0xFFFFFFF0) points past the end
    // of this ~1 KB file. zero_track_samples() must refuse it and touch
    // nothing -- the bounds-check hardening in issue #3.
    //
    // The wraparound issue #3 originally described needs an offset >= 2^63
    // (a 'co64' entry with its upper 32 bits set), which minimp4 itself
    // refuses to open in this build (MP4D_64BIT_SUPPORTED is off by
    // default), so that specific overflow cannot reach this code today. The
    // subtraction-form check in zero_track_samples() is still the correct
    // form regardless; this fixture is the ordinary case it must not
    // regress.
    auto extractor =
        Mp4MetadataExtractor::open(fixture(dir, "parrot_out_of_bounds_chunk_offset.mp4"));
    check(extractor.has_value(), "out-of-bounds-offset fixture opens");
    if (!extractor) return;

    std::ifstream in(fixture(dir, "parrot_out_of_bounds_chunk_offset.mp4"), std::ios::binary);
    std::vector<uint8_t> image((std::istreambuf_iterator<char>(in)),
                               std::istreambuf_iterator<char>());
    // Pinned explicitly, not just implied by the later equality check: a
    // fixture that shrank or grew out from under this test would still pass
    // `image == before` (both would just be wrong together) without this.
    check_eq(image.size(), size_t{1151}, "fixture is the expected size");
    const auto before = image;

    // Track 0 is the metadata track (skipped by callers, same as
    // parrot_scrub.cpp's --zero-media loop); track 1 is the poisoned one.
    const uint64_t zeroed = extractor->zero_track_samples(1, image);

    check_eq(zeroed, uint64_t{0}, "poisoned track: nothing zeroed");
    check(image == before, "poisoned track: file image untouched");
}

void test_missing_stsc_is_rejected(const char* dir) {
    // A metadata track whose 'stsc' box is missing entirely, while 'stsz'
    // still declares a sample and 'stco' still declares a chunk. Before the
    // sample-table validation in issue #3, this reached
    // MP4D_frame_offset() -> sample_to_chunk(), which dereferences
    // sample_to_chunk[0] unconditionally on its first loop iteration
    // whenever chunk_count >= 1 -- a NULL deref, since MP4D_open() does not
    // itself require an 'stsc' box to be present. open() must refuse the
    // whole track up front instead of accepting it and crashing later.
    auto extractor = Mp4MetadataExtractor::open(fixture(dir, "parrot_missing_stsc.mp4"));
    check(!extractor.has_value(), "a track missing 'stsc' is rejected");
}

void test_oversized_sample_size_is_skipped(const char* dir) {
    // A metadata sample whose 'stsz' entry declares 2 MiB while the real
    // payload sitting in mdat is a normal, tiny sample. Before the frame_bytes
    // cap in issue #3, this reached std::vector::resize(2 MiB) unchecked --
    // harmless for one sample at this size, but an attacker-controlled 'stsz'
    // entry can declare up to 4 GiB, and every consumer loop calls
    // extract_sample() once per declared sample. The track itself is fine
    // (every other table is well-formed and sample_count is sane), so open()
    // must still succeed; only the oversized sample is refused.
    auto extractor = Mp4MetadataExtractor::open(fixture(dir, "parrot_oversized_sample_size.mp4"));
    check(extractor.has_value(), "a track with one oversized sample still opens");
    if (!extractor) return;
    check(!extractor->extract_sample(0).has_value(),
          "a sample declaring 2 MiB is refused, not allocated");
}

void test_stsz_count_overflow_is_rejected(const char* dir) {
    // A metadata track whose 'stsz' declares sample_count near 0x40000000.
    // Before re-vendoring minimp4.h onto its integer-overflow fix (issue
    // #27), `sample_count * 4` wrapped in 32-bit arithmetic to a tiny
    // allocation, and the un-overflowed loop then wrote the full declared
    // sample_count of entries into it -- a heap buffer overflow write inside
    // MP4D_open() itself, unreachable by any wrapper-boundary check since it
    // happens before control ever returns to this project's code. open()
    // must refuse the file outright.
    auto extractor = Mp4MetadataExtractor::open(fixture(dir, "parrot_stsz_count_overflow.mp4"));
    check(!extractor.has_value(), "a track with an overflowing stsz sample_count is rejected");
}

void test_hostile_mdhd_duration_is_bounded(const char* dir) {
    // A version-1 'mdhd' declaring a duration near UINT64_MAX. Before issue
    // #4's fix, `duration_hi<<32|duration_lo, times 1'000'000` overflowed
    // uint64_t in native arithmetic before ever dividing by timescale,
    // wrapping duration_us to arbitrary garbage instead of a bounded value.
    auto extractor = Mp4MetadataExtractor::open(fixture(dir, "parrot_hostile_mdhd_duration.mp4"));
    check(extractor.has_value(), "a track with a hostile mdhd duration still opens");
    if (!extractor) return;
    // Mirrors mp4_metadata_extractor.cpp's kMaxSaneDurationUs (100 years in
    // microseconds), not exported: this fixture's near-UINT64_MAX declared
    // duration must clamp to that ceiling rather than wrap.
    constexpr uint64_t kMaxSaneDurationUs = 100ULL * 365 * 24 * 3600 * 1'000'000;
    check(extractor->track_info().duration_us == kMaxSaneDurationUs,
          "an overflowing duration multiply is clamped to the sane ceiling, not wrapped");
}

void test_tkhd_version_2_track_id_not_misread(const char* dir) {
    // A version-2 tkhd has 64-bit creation/modification times, pushing track_id
    // to offset 20 instead of 12. Write a probe word at the version-0 offset
    // where only the correct 16-byte-times offset would find it.
    FILE* file = std::fopen(fixture(dir, "parrot_tkhd_version_2.mp4").c_str(), "rb");
    check(file != nullptr, "fixture opens");
    if (!file) return;
    const auto tracks = scan_mp4_tracks(file);
    std::fclose(file);

    check_eq(tracks.size(), size_t{1}, "track count");
    if (tracks.size() != 1) return;
    check(tracks[0].track_id != uint32_t{0xDEADBEEF},
          "a version-2 tkhd's track_id is not misread from the wrong offset");
    check_eq(tracks[0].track_id, uint32_t{0},
             "a tkhd version this code does not know is rejected, not guessed at");
}

void test_track_index_mismatch_is_rejected(const char* dir) {
    // A foreign top-level 'trak' precedes the real moov. The box scan sees the
    // real metadata track at index 0, while minimp4 sees the foreign metadata
    // track there. Both handlers are 'meta', so the old handler-only check
    // accepted the wrong track. The extractor must fail closed on the count
    // mismatch before any caller can use the shifted index.
    auto extractor =
        Mp4MetadataExtractor::open(fixture(dir, "parrot_track_index_mismatch.mp4"));
    check(!extractor.has_value(),
          "a top-level track that shifts minimp4 indices is rejected");
}

void test_dual_parrot_metadata_track_warns(const char* dir) {
    // Two Parrot 'mett' tracks (same MIME type) referencing the same video
    // track. Mp4MetadataExtractor::open must pick the first and emit a
    // warning mentioning both the count and the chosen index.
    std::string err;
    std::optional<Mp4MetadataExtractor> extractor;
    {
        CerrCapture cap;
        extractor = Mp4MetadataExtractor::open(fixture(dir, "parrot_v3_dual_mett.mp4"));
        err = cap.str();
    }

    check(extractor.has_value(), "dual-mett fixture opens");
    if (!extractor) return;
    check_eq(extractor->track_info().track_index, 1, "dual-mett picks first meta track (index 1)");
    auto first = extractor->extract_sample(0);
    check(first.has_value(), "dual-mett sample 0 extracts");
    if (first) {
        check_eq(first->timestamp_us, uint64_t{0}, "dual-mett sample 0 is from first track (timestamp 0)");
    }
    check(err.find("Warning") != std::string::npos, "dual-mett warning contains 'Warning'");
    check(err.find("2 Parrot metadata tracks") != std::string::npos,
          "dual-mett warning mentions '2 Parrot metadata tracks'");
    check(err.find("using track") != std::string::npos, "dual-mett warning mentions 'using track'");
    check(err.find("ignoring 1 other") != std::string::npos,
          "dual-mett warning mentions 'ignoring 1 other(s)'");

    // Negative case: single-mett fixture must not emit the dual-track warning.
    {
        CerrCapture cap;
        auto single = Mp4MetadataExtractor::open(fixture(dir, "parrot_v3.mp4"));
        const std::string single_err = cap.str();
        check(single.has_value(), "single-mett fixture still opens (negative case)");
        check(single_err.find("Warning") == std::string::npos,
              "single-mett produces no Warning");
        check(single_err.find("Parrot metadata tracks") == std::string::npos,
              "single-mett produces no Parrot metadata tracks warning");
    }
}

void test_mime_parsing() {
    // Valid standard MIME strings
    auto v1 = format_from_mime("application/octet-stream;type=com.parrot.videometadata1");
    check(v1.has_value() && *v1 == ParrotFormat::V1, "v1 format from mime");

    auto v2 = format_from_mime("application/octet-stream;type=com.parrot.videometadata2");
    check(v2.has_value() && *v2 == ParrotFormat::V2, "v2 format from mime");

    auto v3 = format_from_mime("application/octet-stream;type=com.parrot.videometadata3");
    check(v3.has_value() && *v3 == ParrotFormat::V3, "v3 format from mime");

    auto proto = format_from_mime("application/octet-stream;type=com.parrot.videometadataproto");
    check(proto.has_value() && *proto == ParrotFormat::Proto, "proto format from mime");

    // Quoted parameters and whitespace
    auto quoted = format_from_mime("application/octet-stream; type=\"com.parrot.videometadata3\"");
    check(quoted.has_value() && *quoted == ParrotFormat::V3, "quoted v3 format parameter");

    auto extra = format_from_mime("application/octet-stream; type='com.parrot.videometadataproto' ; other=foo");
    check(extra.has_value() && *extra == ParrotFormat::Proto, "single-quoted proto with extra parameter");

    // Bare format identifier
    auto bare = format_from_mime("com.parrot.videometadata3");
    check(bare.has_value() && *bare == ParrotFormat::V3, "bare v3 identifier");

    // Case-insensitive parameter name (TYPE=, Type=)
    auto uppercase_type = format_from_mime("application/octet-stream;TYPE=com.parrot.videometadata3");
    check(uppercase_type.has_value() && *uppercase_type == ParrotFormat::V3, "uppercase TYPE= parameter parses");

    auto mixed_case_type = format_from_mime("application/octet-stream;Type=\"com.parrot.videometadataproto\"");
    check(mixed_case_type.has_value() && *mixed_case_type == ParrotFormat::Proto, "mixed-case Type= parameter parses");

    // Negative cases: magic substring in a non-type parameter (Issue #5)
    check(!format_from_mime("application/octet-stream;other=com.parrot.videometadata3").has_value(),
          "magic string in non-type parameter is rejected");
    check(!format_from_mime("application/octet-stream;comment=\"com.parrot.videometadata3\"").has_value(),
          "magic string in comment parameter is rejected");
    check(!format_from_mime("application/octet-stream;type=com.parrot.videometadata3_extra").has_value(),
          "unknown suffix is rejected");
    check(!format_from_mime("application/octet-stream;type=com.parrot.videometadata99").has_value(),
          "unknown version is rejected");
    check(!format_from_mime("").has_value(), "empty MIME string is rejected");
    check(!format_from_mime("application/octet-stream").has_value(), "bare octet-stream is rejected");
}

void test_malformed_and_truncated_inputs(const char* dir) {
    const std::string valid_path = fixture(dir, "parrot_v3.mp4");
    std::ifstream in(valid_path, std::ios::binary);
    check(in.good(), "valid v3 fixture opens");
    std::vector<uint8_t> valid_bytes((std::istreambuf_iterator<char>(in)),
                                     std::istreambuf_iterator<char>());
    in.close();

    const std::filesystem::path temp_file =
        std::filesystem::temp_directory_path() / "test_malformed_mp4_tmp.mp4";

    auto write_temp = [&](const std::vector<uint8_t>& data) {
        std::ofstream out(temp_file, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(data.data()), static_cast<long>(data.size()));
    };

    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::error_code ec;
            std::filesystem::remove(path, ec);
        }
    } cleanup{temp_file};

    // 1. Truncation at various boundaries
    const std::vector<size_t> trunc_lengths = {
        0, 1, 4, 8, 12, 16, 24, 32, 64, 128, 256, 512, 1024,
        valid_bytes.size() > 50 ? valid_bytes.size() - 50 : 0,
        valid_bytes.size() > 10 ? valid_bytes.size() - 10 : 0,
        valid_bytes.size() > 1 ? valid_bytes.size() - 1 : 0
    };
    for (size_t len : trunc_lengths) {
        if (len > valid_bytes.size()) continue;
        std::vector<uint8_t> truncated(valid_bytes.begin(), valid_bytes.begin() + len);
        write_temp(truncated);
        auto ext = Mp4MetadataExtractor::open(temp_file.string());
        if (ext) {
            for (size_t s = 0; s < ext->sample_count(); ++s) {
                (void)ext->extract_sample(s);
            }
        }
    }

    // 2. Box size larger than file size (moov declaring huge size past EOF)
    auto moov_huge = valid_bytes;
    for (size_t i = 0; i + 8 <= moov_huge.size(); ++i) {
        if (std::memcmp(&moov_huge[i + 4], "moov", 4) == 0) {
            moov_huge[i] = 0x7F;
            moov_huge[i + 1] = 0xFF;
            moov_huge[i + 2] = 0xFF;
            moov_huge[i + 3] = 0xFF;
            break;
        }
    }
    write_temp(moov_huge);
    auto moov_ext = Mp4MetadataExtractor::open(temp_file.string());
    check(!moov_ext.has_value(), "moov box size past EOF is rejected");

    // 3. Truncated mdat payload: moov table intact, but mdat payload is cut off
    for (size_t i = 0; i + 8 <= valid_bytes.size(); ++i) {
        if (std::memcmp(&valid_bytes[i + 4], "mdat", 4) == 0) {
            std::vector<uint8_t> mdat_cut(valid_bytes.begin(), valid_bytes.begin() + i + 8);
            write_temp(mdat_cut);
            auto cut_ext = Mp4MetadataExtractor::open(temp_file.string());
            if (cut_ext && cut_ext->sample_count() > 0) {
                check(!cut_ext->extract_sample(0).has_value(),
                      "sample extraction from truncated mdat returns nullopt");
            }
            break;
        }
    }

    // 4. Single-byte corruption sweep under bounds checker / ASan
    for (size_t i = 0; i < valid_bytes.size(); i += 37) {
        auto corrupted = valid_bytes;
        corrupted[i] ^= 0xFF;
        write_temp(corrupted);
        auto ext = Mp4MetadataExtractor::open(temp_file.string());
        if (ext) {
            for (size_t s = 0; s < ext->sample_count(); ++s) {
                (void)ext->extract_sample(s);
            }
        }
    }
}

void test_largesize_signed_overflow_rejected() {
    const std::string tmp_path = "/tmp/parrot_largesize_overflow.mp4";
    {
        std::ofstream f(tmp_path, std::ios::binary);
        // Box 1 (normal ftyp): 16 bytes
        const uint8_t ftyp[16] = {
            0x00, 0x00, 0x00, 0x10,
            'f',  't',  'y',  'p',
            'i',  's',  'o',  'm',
            0x00, 0x00, 0x02, 0x00
        };
        f.write(reinterpret_cast<const char*>(ftyp), sizeof(ftyp));

        // Box 2 (moov with 64-bit largesize near INT64_MAX):
        // begin = 16. In unhardened code, begin + size = 16 + (INT64_MAX - 5)
        // overflows signed int64_t (UB).
        const uint8_t moov_hdr[16] = {
            0x00, 0x00, 0x00, 0x01,  // size = 1 (largesize follows)
            'm',  'o',  'o',  'v',
            0x7F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFA   // INT64_MAX - 5
        };
        f.write(reinterpret_cast<const char*>(moov_hdr), sizeof(moov_hdr));
    }

    FILE* file = std::fopen(tmp_path.c_str(), "rb");
    check(file != nullptr, "largesize overflow test file opens");
    if (file) {
        auto tracks = scan_mp4_tracks(file);
        std::fclose(file);
        check(tracks.empty(), "hostile near-INT64_MAX largesize box rejected without overflow");
    }
    std::remove(tmp_path.c_str());
}

void test_large_file_offset_seeking() {
    const std::string tmp_path = "/tmp/parrot_large_offset_test.bin";
    const int64_t target_offset = 3LL * 1024 * 1024 * 1024;  // 3 GiB
    const uint8_t payload[8] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};

    // Create a sparse file with bytes placed at 3 GiB
    FILE* out = std::fopen(tmp_path.c_str(), "wb");
    check(out != nullptr, "large file offset test file opens for write");
    if (!out) return;

    check_eq(fseek64(out, target_offset, SEEK_SET), 0, "fseek64 seeks to 3 GiB");
    check_eq(std::fwrite(payload, 1, sizeof(payload), out), sizeof(payload),
             "wrote payload at 3 GiB");
    std::fclose(out);

    // Read back and verify 64-bit seek and tell
    FILE* in = std::fopen(tmp_path.c_str(), "rb");
    check(in != nullptr, "large file offset test file opens for read");
    if (in) {
        check_eq(fseek64(in, 0, SEEK_END), 0, "fseek64 seeks to EOF");
        const int64_t file_size = ftell64(in);
        check_eq(file_size, target_offset + static_cast<int64_t>(sizeof(payload)),
                 "ftell64 reports file size > 3 GiB");

        check_eq(fseek64(in, target_offset, SEEK_SET), 0, "fseek64 seeks to 3 GiB for read");
        check_eq(ftell64(in), target_offset, "ftell64 matches target offset > 2 GiB");

        uint8_t read_buf[8] = {};
        check_eq(std::fread(read_buf, 1, sizeof(read_buf), in), sizeof(read_buf),
                 "read payload from 3 GiB");
        check(std::memcmp(read_buf, payload, sizeof(payload)) == 0,
              "payload at 3 GiB matches expected bytes");
        std::fclose(in);
    }
    std::remove(tmp_path.c_str());
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
    test_mime_parsing();
    test_sample_extraction(dir);
    test_out_of_bounds_chunk_offset_is_rejected(dir);
    test_missing_stsc_is_rejected(dir);
    test_oversized_sample_size_is_skipped(dir);
    test_stsz_count_overflow_is_rejected(dir);
    test_hostile_mdhd_duration_is_bounded(dir);
    test_tkhd_version_2_track_id_not_misread(dir);
    test_track_index_mismatch_is_rejected(dir);
    test_dual_parrot_metadata_track_warns(dir);
    test_malformed_and_truncated_inputs(dir);
    test_largesize_signed_overflow_rejected();
    test_large_file_offset_seeking();

    if (g_failures > 0) {
        std::cerr << "\n" << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "\nAll checks passed\n";
    return 0;
}
