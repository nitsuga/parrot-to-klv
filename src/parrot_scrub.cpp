// Copyright 2026 Eric Schneider
// Licensed under the Apache License, Version 2.0

#include "parrot_scrub.hpp"

#include "mp4_box_scan.hpp"
#include "mp4_metadata_extractor.hpp"
#include "parrot_v3.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>

namespace parrot_to_klv {
namespace {

constexpr double kQ10_22 = 4194304.0;
constexpr double kQ16_16 = 65536.0;
constexpr double kQ16_8 = 256.0;

// Offsets into the v3 base structure; see context/parrot-metadata-formats.md.
constexpr size_t kOffLatitude = 8;
constexpr size_t kOffLongitude = 12;
constexpr size_t kOffAltitudeAndSv = 16;

void store_be32(std::vector<uint8_t>& b, size_t off, uint32_t v) {
    b[off] = static_cast<uint8_t>(v >> 24);
    b[off + 1] = static_cast<uint8_t>(v >> 16);
    b[off + 2] = static_cast<uint8_t>(v >> 8);
    b[off + 3] = static_cast<uint8_t>(v);
}

uint16_t load_be16(const std::vector<uint8_t>& b, size_t off) {
    return static_cast<uint16_t>((static_cast<uint16_t>(b[off]) << 8) | b[off + 1]);
}

uint32_t load_be32(const std::vector<uint8_t>& b, size_t off) {
    return (static_cast<uint32_t>(b[off]) << 24) | (static_cast<uint32_t>(b[off + 1]) << 16) |
           (static_cast<uint32_t>(b[off + 2]) << 8) | b[off + 3];
}

int32_t to_q10_22(double deg) {
    return static_cast<int32_t>(std::lround(deg * kQ10_22));
}

/// Rewrite the location fields of one v3 sample in place.
/// Returns true if an automation extension was also rewritten.
bool scrub_v3_sample(std::vector<uint8_t>& s, const ScrubValues& v) {
    store_be32(s, kOffLatitude, static_cast<uint32_t>(to_q10_22(v.latitude_deg)));
    store_be32(s, kOffLongitude, static_cast<uint32_t>(to_q10_22(v.longitude_deg)));

    // Altitude occupies bits 31..8; the satellite count in bits 7..0 is not
    // location-bearing, so it is preserved.
    const uint32_t sv = load_be32(s, kOffAltitudeAndSv) & 0xFFu;
    const int32_t alt = static_cast<int32_t>(std::lround(v.altitude_amsl_m * kQ16_8));
    store_be32(s, kOffAltitudeAndSv, (static_cast<uint32_t>(alt) << 8) | sv);

    // The automation extension carries two more coordinate triples.
    bool rewrote_automation = false;
    const size_t declared = 4 + static_cast<size_t>(load_be16(s, 2)) * 4;
    size_t off = kParrotV3BaseSize;
    while (off + 4 <= declared && off + 4 <= s.size()) {
        const uint16_t id = load_be16(s, off);
        const size_t payload_bytes = static_cast<size_t>(load_be16(s, off + 2)) * 4;
        const size_t body = off + 4;
        if (body + payload_bytes > s.size()) {
            break;
        }
        if (id == kExtAutomation && payload_bytes >= 24) {
            const int32_t alt_q16_16 =
                static_cast<int32_t>(std::lround(v.altitude_amsl_m * kQ16_16));
            for (size_t base : {body, body + 12}) {  // framing target, flight destination
                store_be32(s, base, static_cast<uint32_t>(to_q10_22(v.latitude_deg)));
                store_be32(s, base + 4, static_cast<uint32_t>(to_q10_22(v.longitude_deg)));
                store_be32(s, base + 8, static_cast<uint32_t>(alt_q16_16));
            }
            rewrote_automation = true;
        }
        off = body + payload_bytes;
    }
    return rewrote_automation;
}

/// Overwrite every digit of an ISO 6709 geotag string with '0', preserving
/// signs, separators and — critically — length, so no box needs resizing.
/// The result parses as a valid coordinate at the null island.
size_t zero_digits(std::vector<uint8_t>& buf, size_t begin, size_t end) {
    size_t changed = 0;
    for (size_t i = begin; i < end && i < buf.size(); ++i) {
        if (buf[i] >= '1' && buf[i] <= '9') {
            buf[i] = '0';
            ++changed;
        }
    }
    return changed;
}

struct Box {
    uint32_t type = 0;
    size_t body_begin = 0;
    size_t body_end = 0;
};

bool read_box(const std::vector<uint8_t>& b, size_t pos, size_t limit, Box& out) {
    if (pos + 8 > limit) return false;
    uint64_t size = load_be32(b, pos);
    out.type = load_be32(b, pos + 4);
    size_t header = 8;
    if (size == 1) {
        if (pos + 16 > limit) return false;
        size = (static_cast<uint64_t>(load_be32(b, pos + 8)) << 32) | load_be32(b, pos + 12);
        header = 16;
    } else if (size == 0) {
        size = limit - pos;
    }
    if (size < header || pos + size > limit) return false;
    out.body_begin = pos + header;
    out.body_end = pos + static_cast<size_t>(size);
    return true;
}

/// Find 'moov'/'udta' and zero any geotag it holds.
///
/// Parrot writes the recording's position into a '©xyz' box as an ISO 6709
/// string — a location leak entirely separate from the telemetry track, and one
/// that survives every metadata-level scrub.
void scrub_geotag(std::vector<uint8_t>& file, ScrubReport& report) {
    const uint32_t kMoov = fourcc("moov");
    const uint32_t kUdta = fourcc("udta");
    const uint32_t kMeta = fourcc("meta");
    const uint32_t kIlst = fourcc("ilst");
    // '©xyz' — 0xA9 is the copyright sign, then 'x' 'y' 'z'. Not expressible
    // through fourcc(), whose argument is a plain char literal.
    const uint32_t kXyz = 0xA978797Au;

    // Recurse through moov -> udta -> (meta -> ilst) looking for ©xyz.
    const auto walk = [&](auto&& self, size_t begin, size_t end, int depth) -> void {
        if (depth > 6) return;
        size_t pos = begin;
        Box box;
        while (read_box(file, pos, end, box)) {
            if (box.type == kXyz) {
                // Payload: size(2) language(2) then the string.
                const size_t str = box.body_begin + 4;
                if (str < box.body_end && zero_digits(file, str, box.body_end) > 0) {
                    ++report.geotag_boxes_rewritten;
                }
            } else if (box.type == kMoov || box.type == kUdta || box.type == kMeta ||
                       box.type == kIlst) {
                size_t child = box.body_begin;
                // 'meta' is a FullBox: skip its version/flags before its children.
                if (box.type == kMeta && child + 4 <= box.body_end) child += 4;
                self(self, child, box.body_end, depth + 1);
            }
            pos = box.body_end;
        }
    };
    walk(walk, 0, file.size(), 0);
}

}  // namespace

ScrubReport scrub_file(const std::string& input, const std::string& output,
                       const ScrubOptions& options) {
    ScrubReport report;

    // Identify the track and format before touching anything.
    auto extractor = Mp4MetadataExtractor::open(input);
    if (!extractor) {
        report.error = "no Parrot metadata track found (nothing to scrub)";
        return report;
    }
    const auto& info = extractor->track_info();
    if (info.format != ParrotFormat::V3) {
        // Passing the file through unscrubbed would be worse than refusing: the
        // output would carry the original coordinates under a name that says
        // otherwise.
        report.error = "only the v3 format can be scrubbed; this file is " +
                       to_string(info.format) + ". Refusing to write an output that "
                       "would look scrubbed but is not.";
        return report;
    }

    std::ifstream in(input, std::ios::binary);
    if (!in) {
        report.error = "cannot read " + input;
        return report;
    }
    std::vector<uint8_t> file((std::istreambuf_iterator<char>(in)),
                              std::istreambuf_iterator<char>());
    in.close();
    if (file.empty()) {
        report.error = "empty input";
        return report;
    }

    // Rewrite each metadata sample where it sits in the file.
    for (uint32_t i = 0; i < extractor->sample_count(); ++i) {
        auto located = extractor->locate_sample(i);
        if (!located) {
            report.error = "could not locate sample " + std::to_string(i);
            return report;
        }
        const auto [offset, size] = *located;
        if (offset + size > file.size() || size < kParrotV3BaseSize) {
            report.error = "sample " + std::to_string(i) + " lies outside the file";
            return report;
        }
        std::vector<uint8_t> sample(file.begin() + static_cast<long>(offset),
                                    file.begin() + static_cast<long>(offset + size));
        if (!parse_v3(sample)) {
            report.error = "sample " + std::to_string(i) + " is not decodable v3";
            return report;
        }
        if (scrub_v3_sample(sample, options.values)) {
            ++report.automation_extensions_rewritten;
        }
        std::copy(sample.begin(), sample.end(), file.begin() + static_cast<long>(offset));
        ++report.samples_rewritten;
    }

    scrub_geotag(file, report);

    if (options.zero_media) {
        FILE* raw = std::fopen(input.c_str(), "rb");
        if (raw) {
            const auto tracks = scan_mp4_tracks(raw);
            std::fclose(raw);
            for (size_t t = 0; t < tracks.size(); ++t) {
                if (static_cast<int>(t) == info.track_index) continue;
                report.media_bytes_zeroed += extractor->zero_track_samples(
                    static_cast<int>(t), file);
            }
        }
    } else {
        report.warnings.push_back(
            "video and audio payloads are untouched — the imagery still shows where "
            "this was recorded. Use --zero-media if the output will be committed.");
    }

    report.warnings.push_back(
        "session metadata (serial number, friendly name, software version) is not "
        "scrubbed; it identifies the aircraft, not the location.");

    std::ofstream out(output, std::ios::binary | std::ios::trunc);
    if (!out) {
        report.error = "cannot write " + output;
        return report;
    }
    out.write(reinterpret_cast<const char*>(file.data()), static_cast<long>(file.size()));
    if (!out) {
        report.error = "write failed";
        return report;
    }

    report.ok = true;
    return report;
}

}  // namespace parrot_to_klv
