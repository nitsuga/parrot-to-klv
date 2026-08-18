// Copyright 2026 Eric Schneider
// Licensed under the Apache License, Version 2.0

#include "mp4_metadata_extractor.hpp"

#include "mp4_box_scan.hpp"

#define MINIMP4_IMPLEMENTATION
#include "minimp4.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
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

/// Parrot writes `application/octet-stream;type=com.parrot.videometadata3`.
/// Match on the `type=` parameter; the media type prefix carries no information.
std::optional<ParrotFormat> format_from_mime(const std::string& mime) {
    const auto pos = mime.find("com.parrot.videometadata");
    if (pos == std::string::npos) {
        return std::nullopt;
    }
    const std::string suffix = mime.substr(pos + std::strlen("com.parrot.videometadata"));
    if (suffix == "1") return ParrotFormat::V1;
    if (suffix == "2") return ParrotFormat::V2;
    if (suffix == "3") return ParrotFormat::V3;
    if (suffix == "proto") return ParrotFormat::Proto;
    return std::nullopt;
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

    extractor.impl_->metadata_track_idx = track_idx;

    // Fill track info
    extractor.track_info_.track_index = track_idx;
    extractor.track_info_.mime_type = described.mime_format;
    extractor.track_info_.format = *format;
    extractor.track_info_.sample_count = track.sample_count;
    extractor.track_info_.duration_us =
        track.timescale > 0
            ? ((static_cast<uint64_t>(track.duration_hi) << 32 | track.duration_lo) * 1000000ULL) /
                  track.timescale
            : 0;

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
    if (frame_bytes == 0) {
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
        // hostile-file-controlled. `offset + frame_bytes` in additive form
        // can wrap past this check for an offset near UINT64_MAX, and the
        // wrapped value then casts to a negative `long` below — out of
        // bounds. minimp4 currently refuses to open a file whose 'co64'
        // upper bits are set at all (MP4D_64BIT_SUPPORTED is off by default
        // here), so today `offset` never exceeds 32 bits in practice; this
        // is defense in depth against that guard changing, not a live
        // exploit closed. Checking `offset` against `file.size()` first,
        // then subtracting, cannot overflow either way.
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
