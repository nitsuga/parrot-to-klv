// Copyright 2026 Eric Schneider
// Licensed under the Apache License, Version 2.0

#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace parrot_to_klv {

/// Represents a single metadata sample extracted from MP4
struct MetadataSample {
    uint64_t timestamp_us;  ///< Sample timestamp in microseconds
    std::vector<uint8_t> data;  ///< Raw metadata bytes
};

/// Result of MP4 metadata track identification
struct MetadataTrackInfo {
    int track_index;  ///< Track index in MP4 file
    std::string mime_type;  ///< MIME type from stsd (e.g., "com.parrot.videometadata3")
    uint32_t sample_count;  ///< Number of metadata samples
    uint64_t duration_us;  ///< Track duration in microseconds
};

/// MP4 metadata track extractor
///
/// Extracts timed metadata tracks from MP4 files using minimp4.
/// Specifically designed for Parrot drone telemetry embedded as
/// MP4 metadata tracks (handler type 'meta', MIME type com.parrot.videometadata*).
class Mp4MetadataExtractor {
public:
    /// Open an MP4 file and locate metadata track
    /// @param file_path Path to MP4 file
    /// @return Extractor instance, or nullopt on failure
    static std::optional<Mp4MetadataExtractor> open(const std::string& file_path);

    /// Destructor (closes file)
    ~Mp4MetadataExtractor();

    // No copy (owns file handle)
    Mp4MetadataExtractor(const Mp4MetadataExtractor&) = delete;
    Mp4MetadataExtractor& operator=(const Mp4MetadataExtractor&) = delete;

    // Move is fine
    Mp4MetadataExtractor(Mp4MetadataExtractor&&) noexcept;
    Mp4MetadataExtractor& operator=(Mp4MetadataExtractor&&) noexcept;

    /// Get metadata track information
    const MetadataTrackInfo& track_info() const { return track_info_; }

    /// Get total number of metadata samples
    uint32_t sample_count() const { return track_info_.sample_count; }

    /// Extract a specific metadata sample by index
    /// @param sample_index Sample index (0-based)
    /// @return Sample data, or nullopt if index invalid or extraction fails
    std::optional<MetadataSample> extract_sample(uint32_t sample_index);

    /// Extract all metadata samples
    /// @return Vector of samples, or empty on failure
    std::vector<MetadataSample> extract_all_samples();

private:
    Mp4MetadataExtractor() = default;

    struct Impl;  // Forward declare implementation (pimpl for minimp4 types)
    std::unique_ptr<Impl> impl_;
    MetadataTrackInfo track_info_;
};

}  // namespace parrot_to_klv
