// Copyright 2026 Eric Schneider
// Licensed under the Apache License, Version 2.0

#include "mp4_metadata_extractor.hpp"

#define MINIMP4_IMPLEMENTATION
#include "minimp4.h"

#include <cstdio>
#include <cstring>
#include <iostream>

namespace parrot_to_klv {

// Pimpl implementation holding minimp4 state
struct Mp4MetadataExtractor::Impl {
    FILE* file = nullptr;
    MP4D_demux_t demux = {};
    int metadata_track_idx = -1;

    ~Impl() {
        if (file) {
            fclose(file);
        }
    }
};

namespace {

// minimp4 callback: read bytes from file
int read_callback(int64_t offset, void* buffer, size_t size, void* token) {
    FILE* file = static_cast<FILE*>(token);
    if (fseek(file, offset, SEEK_SET) != 0) {
        return 0;
    }
    return static_cast<int>(fread(buffer, 1, size, file));
}

// Find metadata track (handler type 'meta' or 'nmhd')
int find_metadata_track(const MP4D_demux_t& demux) {
    for (unsigned i = 0; i < demux.track_count; ++i) {
        // Check handler type - metadata tracks have handler_type = 'meta'
        // Also check for 'nmhd' (null media header) which Parrot uses
        const auto& track = demux.track[i];
        if (track.handler_type == MP4D_HANDLER_TYPE_NONE ||
            track.handler_type == MP4D_HANDLER_TYPE_META) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

// Extract MIME type from stsd box (sample description)
std::string extract_mime_type(FILE* file, const MP4D_track_t& track) {
    // For Parrot metadata, MIME type is in stsd as TextMetaDataSampleEntry
    // Format: "application/octet-stream;type=<mime_type>"
    // where <mime_type> is com.parrot.videometadata{1,2,3,proto}

    // minimp4 stores sample description info in track.SampleDescription
    // For text metadata, the format string should be present

    // For now, try to detect from track properties
    // We'll refine this based on actual file structure

    // Placeholder: return empty, will be enhanced when we examine real MP4 structure
    return "";
}

}  // namespace

std::optional<Mp4MetadataExtractor> Mp4MetadataExtractor::open(const std::string& file_path) {
    Mp4MetadataExtractor extractor;
    extractor.impl_ = std::make_unique<Impl>();

    // Open file
    extractor.impl_->file = fopen(file_path.c_str(), "rb");
    if (!extractor.impl_->file) {
        std::cerr << "Failed to open file: " << file_path << std::endl;
        return std::nullopt;
    }

    // Parse MP4 structure with minimp4
    int ret = MP4D_open(&extractor.impl_->demux, read_callback, extractor.impl_->file, 0);
    if (ret != 0) {
        std::cerr << "Failed to parse MP4 file: " << file_path << " (error " << ret << ")" << std::endl;
        return std::nullopt;
    }

    // Find metadata track
    int track_idx = find_metadata_track(extractor.impl_->demux);
    if (track_idx < 0) {
        std::cerr << "No metadata track found in: " << file_path << std::endl;
        std::cerr << "Found " << extractor.impl_->demux.track_count << " tracks:" << std::endl;
        for (unsigned i = 0; i < extractor.impl_->demux.track_count; ++i) {
            std::cerr << "  Track " << i << ": handler_type="
                      << extractor.impl_->demux.track[i].handler_type << std::endl;
        }
        return std::nullopt;
    }

    extractor.impl_->metadata_track_idx = track_idx;
    const auto& track = extractor.impl_->demux.track[track_idx];

    // Extract MIME type (placeholder for now)
    std::string mime_type = extract_mime_type(extractor.impl_->file, track);
    if (mime_type.empty()) {
        // Default to v3 for ANAFI (we'll detect properly later)
        mime_type = "com.parrot.videometadata3";
        std::cerr << "Warning: Could not detect MIME type, assuming: " << mime_type << std::endl;
    }

    // Fill track info
    extractor.track_info_.track_index = track_idx;
    extractor.track_info_.mime_type = mime_type;
    extractor.track_info_.sample_count = track.sample_count;
    extractor.track_info_.duration_us =
        (track.duration_units * 1000000ULL) / extractor.impl_->demux.timescale;

    std::cout << "Found metadata track " << track_idx << ":" << std::endl;
    std::cout << "  MIME type: " << mime_type << std::endl;
    std::cout << "  Samples: " << track.sample_count << std::endl;
    std::cout << "  Duration: " << (extractor.track_info_.duration_us / 1000000.0) << " s" << std::endl;

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

    // Get sample offset and size
    int64_t offset = MP4D_frame_offset(&impl_->demux, impl_->metadata_track_idx,
                                       sample_index, nullptr);
    uint32_t size = MP4D_frame_size(&impl_->demux, impl_->metadata_track_idx, sample_index);

    if (offset < 0 || size == 0) {
        return std::nullopt;
    }

    // Read sample data
    MetadataSample sample;
    sample.data.resize(size);

    if (fseek(impl_->file, offset, SEEK_SET) != 0) {
        return std::nullopt;
    }

    size_t bytes_read = fread(sample.data.data(), 1, size, impl_->file);
    if (bytes_read != size) {
        return std::nullopt;
    }

    // Calculate timestamp (in microseconds)
    // Get timestamp from track using sample index
    uint64_t timestamp_units = 0;
    for (uint32_t i = 0; i < sample_index; ++i) {
        timestamp_units += MP4D_frame_duration(&impl_->demux, impl_->metadata_track_idx, i);
    }
    sample.timestamp_us = (timestamp_units * 1000000ULL) / track.timescale;

    return sample;
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
