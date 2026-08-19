// Copyright 2026 Eric Schneider
// Licensed under the Apache License, Version 2.0

#include "mp4_metadata_extractor.hpp"

#include "mp4_box_scan.hpp"

#define MINIMP4_IMPLEMENTATION
#include "minimp4.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace parrot_to_klv {

// Pimpl implementation holding minimp4 state
struct Mp4MetadataExtractor::Impl {
    FILE* file = nullptr;
    MP4D_demux_t demux = {};
    bool demux_open = false;
    int metadata_track_idx = -1;

    ~Impl() {
        if (demux_open) {
            MP4D_close(&demux);
        }
        if (file) {
            fclose(file);
        }
    }
};

namespace {

std::optional<ParrotFormat> parse_format_identifier(std::string_view id) {
    if (id == "com.parrot.videometadata1") return ParrotFormat::V1;
    if (id == "com.parrot.videometadata2") return ParrotFormat::V2;
    if (id == "com.parrot.videometadata3") return ParrotFormat::V3;
    if (id == "com.parrot.videometadataproto" || id == "com.parrot.videometadata.proto") {
        return ParrotFormat::Proto;
    }
    return std::nullopt;
}

bool starts_with_ci(std::string_view s, std::string_view prefix) {
    if (s.size() < prefix.size()) return false;
    for (size_t i = 0; i < prefix.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(s[i])) !=
            std::tolower(static_cast<unsigned char>(prefix[i]))) {
            return false;
        }
    }
    return true;
}

int64_t file_size_of(FILE* file) {
    if (std::fseek(file, 0, SEEK_END) != 0) {
        return -1;
    }
    const int64_t size = std::ftell(file);
    std::fseek(file, 0, SEEK_SET);
    return size;
}

// minimp4 read callback. Returns *non-zero on failure* — see minimp4_fgets,
// which treats any truthy return as an error.
int read_callback(int64_t offset, void* buffer, size_t size, void* token) {
    FILE* file = static_cast<FILE*>(token);
    if (fseek(file, static_cast<long>(offset), SEEK_SET) != 0) {
        return 1;
    }
    return fread(buffer, 1, size, file) != size;
}

/// A generous ceiling, not a real limit: no genuine Parrot recording comes
/// remotely close. Exists only so a hostile `stsz` sample count (up to
/// 2^32-1) cannot drive `extract_all_samples()`'s `reserve()` to attempt a
/// multi-hundred-gigabyte allocation, or a caller's loop over `sample_count()`
/// into a multi-hour fail loop, before a single byte of sample data is read.
constexpr uint32_t kMaxSaneSampleCount = 10'000'000;

/// Also generous: real Parrot metadata samples run to a few hundred bytes at
/// most (the largest fixture extension payloads are smaller still). A hostile
/// `stsz` entry can declare up to 4 GiB for one sample; without a cap that
/// reaches `resize()` before the short-read check below ever gets a chance to
/// reject it.
constexpr uint32_t kMaxSaneFrameBytes = 1 * 1024 * 1024;

/// A generous ceiling on the informational `duration_us` this project reports
/// (100 years in microseconds) -- not a real limit, just a bound the
/// multiply-then-divide below can be clamped to instead of silently wrapping.
/// `mdhd`'s duration is a bare 64-bit value a hostile file can set to
/// 2^64-1; `duration_hi<<32|duration_lo, times 1e6` overflows uint64_t well
/// before that.
constexpr uint64_t kMaxSaneDurationUs = 100ULL * 365 * 24 * 3600 * 1'000'000;

/// True when a track's sample-index tables are self-consistent enough for
/// `MP4D_frame_offset()` to be called safely.
///
/// minimp4 validates none of this itself — `MP4D_open` only checks
/// `track_count > 0`. A track missing `stco`/`co64` or `stsc` leaves
/// `chunk_offset`/`sample_to_chunk` null, and `MP4D_frame_offset`'s
/// `sample_to_chunk()` helper dereferences `sample_to_chunk[0]`
/// unconditionally on its very first loop iteration whenever `chunk_count` is
/// at least 1 — a hostile file that pairs a populated `stco` with a missing
/// `stsc` (or vice versa) crashes here, not on some later malformed-sample
/// path. A track that declares zero samples has nothing to validate: every
/// table pointer legitimately stays null in that case.
bool track_sample_tables_valid(const MP4D_track_t& track) {
    if (track.sample_count == 0) return true;
    if (track.sample_count > kMaxSaneSampleCount) return false;
    if (track.chunk_count == 0 || track.chunk_offset == nullptr) return false;
    if (track.sample_to_chunk_count == 0 || track.sample_to_chunk == nullptr) return false;
    if (track.entry_size == nullptr) return false;
#if MP4D_TIMESTAMPS_SUPPORTED
    // A missing 'stts' leaves these null; MP4D_frame_offset dereferences them
    // unconditionally whenever a caller asks for timestamp/duration, which
    // extract_sample() always does. This does not catch the narrower case of
    // an 'stts' present but declaring fewer samples than 'stsz' -- minimp4
    // discards that count once the box is parsed, so it is not recoverable
    // from the public struct without re-parsing 'stts' independently.
    if (track.timestamp == nullptr || track.duration == nullptr) return false;
#endif
    return true;
}

/// Locate the Parrot metadata track among the scanned descriptions.
/// Returns the index into `tracks`, or -1.
int find_parrot_metadata_track(const std::vector<Mp4TrackDescription>& tracks) {
    for (size_t i = 0; i < tracks.size(); ++i) {
        if (tracks[i].handler_type == kHandlerMeta &&
            format_from_mime(tracks[i].mime_format).has_value()) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

/// Report what we did find, so a non-Parrot or unexpected file says why.
void report_tracks(const std::vector<Mp4TrackDescription>& tracks) {
    std::cerr << "Found " << tracks.size() << " track(s):\n";
    for (size_t i = 0; i < tracks.size(); ++i) {
        const auto& t = tracks[i];
        std::cerr << "  Track " << i << ": id=" << t.track_id
                  << " handler='" << fourcc_to_string(t.handler_type) << "'"
                  << " sample_entry='" << fourcc_to_string(t.sample_entry_format) << "'";
        if (!t.mime_format.empty()) {
            std::cerr << " mime=\"" << t.mime_format << "\"";
        }
        std::cerr << "\n";
    }
}

}  // namespace

std::optional<ParrotFormat> format_from_mime(const std::string& mime) {
    if (auto direct = parse_format_identifier(mime)) {
        return direct;
    }

    // Match type parameter: e.g. "application/octet-stream;type=com.parrot.videometadata3"
    size_t pos = 0;
    while (pos < mime.size()) {
        size_t next_semi = mime.find(';', pos);
        if (next_semi == std::string::npos) {
            next_semi = mime.size();
        }
        std::string_view param = std::string_view(mime).substr(pos, next_semi - pos);

        // Trim leading and trailing whitespace
        while (!param.empty() && (param.front() == ' ' || param.front() == '\t')) {
            param.remove_prefix(1);
        }
        while (!param.empty() && (param.back() == ' ' || param.back() == '\t')) {
            param.remove_suffix(1);
        }

        if (starts_with_ci(param, "type=")) {
            std::string_view val = param.substr(5);
            while (!val.empty() && (val.front() == ' ' || val.front() == '\t')) {
                val.remove_prefix(1);
            }
            while (!val.empty() && (val.back() == ' ' || val.back() == '\t')) {
                val.remove_suffix(1);
            }
            if (val.size() >= 2 && ((val.front() == '"' && val.back() == '"') ||
                                    (val.front() == '\'' && val.back() == '\''))) {
                val.remove_prefix(1);
                val.remove_suffix(1);
            }
            if (auto fmt = parse_format_identifier(val)) {
                return fmt;
            }
        }

        pos = next_semi + 1;
    }

    return std::nullopt;
}

std::string to_string(ParrotFormat format) {
    switch (format) {
        case ParrotFormat::V1: return "v1";
        case ParrotFormat::V2: return "v2";
        case ParrotFormat::V3: return "v3";
        case ParrotFormat::Proto: return "proto";
    }
    return "unknown";
}

std::optional<Mp4MetadataExtractor> Mp4MetadataExtractor::open(const std::string& file_path) {
    Mp4MetadataExtractor extractor;
    extractor.impl_ = std::make_unique<Impl>();

    // Open file
    extractor.impl_->file = fopen(file_path.c_str(), "rb");
    if (!extractor.impl_->file) {
        std::cerr << "Failed to open file: " << file_path << std::endl;
        return std::nullopt;
    }

    const int64_t size = file_size_of(extractor.impl_->file);
    if (size <= 0) {
        std::cerr << "Empty or unreadable file: " << file_path << std::endl;
        return std::nullopt;
    }

    // Recover what minimp4 discards: sample entry MIME types and tref links.
    const auto tracks = scan_mp4_tracks(extractor.impl_->file);
    if (tracks.empty()) {
        std::cerr << "No 'moov' tracks found in: " << file_path << std::endl;
        return std::nullopt;
    }

    const int track_idx = find_parrot_metadata_track(tracks);
    if (track_idx < 0) {
        std::cerr << "No Parrot metadata track in: " << file_path << "\n";
        report_tracks(tracks);
        return std::nullopt;
    }
    const auto& described = tracks[static_cast<size_t>(track_idx)];

    // The MIME type names the payload format outright — never infer it.
    const auto format = format_from_mime(described.mime_format);
    if (!format) {
        std::cerr << "Unrecognized Parrot MIME type: " << described.mime_format << std::endl;
        return std::nullopt;
    }

    // Parse MP4 structure with minimp4, which owns the sample index.
    // NB: file_size must be real — minimp4 treats read_pos >= read_size as EOF,
    // so passing 0 aborts parsing at the first skip.
    if (MP4D_open(&extractor.impl_->demux, read_callback, extractor.impl_->file, size) != 1) {
        std::cerr << "Failed to parse MP4 file: " << file_path << std::endl;
        return std::nullopt;
    }
    extractor.impl_->demux_open = true;

    // Both enumerations follow 'trak' order, so the indices line up. Check that
    // rather than trust it — a mismatch would silently read the wrong track.
    if (static_cast<size_t>(track_idx) >= extractor.impl_->demux.track_count) {
        std::cerr << "Track index " << track_idx << " beyond minimp4's "
                  << extractor.impl_->demux.track_count << " track(s)" << std::endl;
        return std::nullopt;
    }
    const auto& track = extractor.impl_->demux.track[track_idx];
    if (track.handler_type != described.handler_type) {
        std::cerr << "Track " << track_idx << " disagrees between scanners: box scan says '"
                  << fourcc_to_string(described.handler_type) << "', minimp4 says '"
                  << fourcc_to_string(track.handler_type) << "'" << std::endl;
        return std::nullopt;
    }
    if (!track_sample_tables_valid(track)) {
        std::cerr << "Track " << track_idx << " is missing or has an implausible "
                     "sample-index table ('stco'/'co64', 'stsc', 'stts', or a sample "
                     "count past a sane ceiling) -- refusing rather than reading past "
                     "one that is not there" << std::endl;
        return std::nullopt;
    }

    extractor.impl_->metadata_track_idx = track_idx;

    // Fill track info
    extractor.track_info_.track_index = track_idx;
    extractor.track_info_.mime_type = described.mime_format;
    extractor.track_info_.format = *format;
    extractor.track_info_.sample_count = track.sample_count;
    if (track.timescale > 0) {
        const uint64_t duration_units =
            (static_cast<uint64_t>(track.duration_hi) << 32) | track.duration_lo;
        // duration_units * 1'000'000 can exceed UINT64_MAX for a hostile mdhd
        // (any duration_units past ~1.8e13) before the division ever brings
        // it back down. Check against the multiply's own overflow threshold
        // first, rather than compute in a wider type (no standard C++ 128-bit
        // integer) and clamp after the fact.
        constexpr uint64_t kMaxMultiplyableUnits = UINT64_MAX / 1'000'000ULL;
        extractor.track_info_.duration_us =
            duration_units > kMaxMultiplyableUnits
                ? kMaxSaneDurationUs
                : std::min<uint64_t>(duration_units * 1'000'000ULL / track.timescale,
                                     kMaxSaneDurationUs);
    } else {
        extractor.track_info_.duration_us = 0;
    }

    std::cout << "Found metadata track " << track_idx << ":" << std::endl;
    std::cout << "  MIME type: " << described.mime_format << std::endl;
    std::cout << "  Format: " << to_string(*format) << std::endl;
    std::cout << "  Samples: " << track.sample_count << std::endl;
    std::cout << "  Duration: " << (extractor.track_info_.duration_us / 1000000.0) << " s"
              << std::endl;
    if (described.cdsc_track_ids.empty()) {
        std::cout << "  Note: no tref/cdsc reference to a video track" << std::endl;
    }

    return extractor;
}

Mp4MetadataExtractor::~Mp4MetadataExtractor() = default;

Mp4MetadataExtractor::Mp4MetadataExtractor(Mp4MetadataExtractor&&) noexcept = default;
Mp4MetadataExtractor& Mp4MetadataExtractor::operator=(Mp4MetadataExtractor&&) noexcept = default;

std::optional<MetadataSample> Mp4MetadataExtractor::extract_sample(uint32_t sample_index) {
    if (!impl_ || impl_->metadata_track_idx < 0) {
        return std::nullopt;
    }

    const auto& track = impl_->demux.track[impl_->metadata_track_idx];
    if (sample_index >= track.sample_count) {
        return std::nullopt;
    }

    // One call yields offset, size and timestamp together. The timestamp is in
    // *track* timescale units — minimp4's header comment saying otherwise is
    // wrong; see its MP4D_frame_offset, which reads tr->timestamp[].
    unsigned frame_bytes = 0;
    unsigned timestamp_units = 0;
    unsigned duration_units = 0;
    const MP4D_file_offset_t offset =
        MP4D_frame_offset(&impl_->demux, static_cast<unsigned>(impl_->metadata_track_idx),
                          sample_index, &frame_bytes, &timestamp_units, &duration_units);
    if (frame_bytes == 0 || frame_bytes > kMaxSaneFrameBytes) {
        return std::nullopt;
    }

    // Read sample data
    MetadataSample sample;
    sample.data.resize(frame_bytes);

    if (fseek(impl_->file, static_cast<long>(offset), SEEK_SET) != 0) {
        return std::nullopt;
    }
    if (fread(sample.data.data(), 1, frame_bytes, impl_->file) != frame_bytes) {
        return std::nullopt;
    }

    sample.timestamp_us =
        track.timescale > 0
            ? (static_cast<uint64_t>(timestamp_units) * 1000000ULL) / track.timescale
            : 0;

    return sample;
}

std::optional<std::pair<uint64_t, uint32_t>> Mp4MetadataExtractor::locate_sample(
    uint32_t sample_index) const {
    if (!impl_ || impl_->metadata_track_idx < 0) {
        return std::nullopt;
    }
    const auto& track = impl_->demux.track[impl_->metadata_track_idx];
    if (sample_index >= track.sample_count) {
        return std::nullopt;
    }
    unsigned frame_bytes = 0;
    const MP4D_file_offset_t offset =
        MP4D_frame_offset(&impl_->demux, static_cast<unsigned>(impl_->metadata_track_idx),
                          sample_index, &frame_bytes, nullptr, nullptr);
    if (frame_bytes == 0) {
        return std::nullopt;
    }
    return std::make_pair(static_cast<uint64_t>(offset), static_cast<uint32_t>(frame_bytes));
}

uint64_t Mp4MetadataExtractor::zero_track_samples(int track_index,
                                                  std::vector<uint8_t>& file) const {
    if (!impl_ || track_index < 0 ||
        static_cast<unsigned>(track_index) >= impl_->demux.track_count) {
        return 0;
    }
    const auto& track = impl_->demux.track[track_index];
    uint64_t zeroed = 0;
    for (unsigned i = 0; i < track.sample_count; ++i) {
        unsigned frame_bytes = 0;
        const MP4D_file_offset_t offset =
            MP4D_frame_offset(&impl_->demux, static_cast<unsigned>(track_index), i, &frame_bytes,
                              nullptr, nullptr);
        // Subtraction form: `offset` comes from the sample table (stco/co64),
        // hostile-file-controlled, and is 64-bit wide (MINIMP4_ALLOW_64BIT
        // defaults on). `offset + frame_bytes` in additive form can wrap past
        // this check for an offset near UINT64_MAX, and the wrapped value
        // then casts to a negative `long` below — out of bounds. minimp4
        // currently refuses to open a file whose 'co64' upper bits are set at
        // all (MP4D_64BIT_SUPPORTED, unlike MINIMP4_ALLOW_64BIT, is off by
        // default here), so today `offset` never exceeds 32 bits in
        // practice; this is defense in depth against that guard changing,
        // not a live exploit closed. Checking `offset` against `file.size()`
        // first, then subtracting, cannot overflow either way.
        if (frame_bytes == 0 || static_cast<uint64_t>(offset) > file.size() ||
            frame_bytes > file.size() - static_cast<uint64_t>(offset)) {
            continue;
        }
        std::fill_n(file.begin() + static_cast<long>(offset), frame_bytes, uint8_t{0});
        zeroed += frame_bytes;
    }
    return zeroed;
}

std::vector<MetadataSample> Mp4MetadataExtractor::extract_all_samples() {
    std::vector<MetadataSample> samples;
    samples.reserve(sample_count());

    for (uint32_t i = 0; i < sample_count(); ++i) {
        auto sample = extract_sample(i);
        if (sample) {
            samples.push_back(std::move(*sample));
        } else {
            std::cerr << "Warning: Failed to extract sample " << i << std::endl;
        }
    }

    return samples;
}

}  // namespace parrot_to_klv
