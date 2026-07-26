// Copyright 2026 Eric Schneider
// Licensed under the Apache License, Version 2.0

#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace parrot_to_klv {

/// Represents a single metadata sample extracted from MP4
struct MetadataSample {
    uint64_t timestamp_us;  ///< Sample timestamp in microseconds
    std::vector<uint8_t> data;  ///< Raw metadata bytes
};

/// Parrot metadata format, as named by the stsd MIME type.
///
/// This selects the payload parser, so it is never guessed: a track whose MIME
/// type we do not recognise is rejected rather than assumed.
enum class ParrotFormat {
    V1,     ///< com.parrot.videometadata1 — Bebop / Bebop 2 (deprecated)
    V2,     ///< com.parrot.videometadata2 — Bebop 2, Disco, Bluegrass
    V3,     ///< com.parrot.videometadata3 — ANAFI range
    Proto,  ///< com.parrot.videometadataproto — ANAFI Ai / UKR
};

/// Human-readable name for a format, for diagnostics.
std::string to_string(ParrotFormat format);

/// Result of MP4 metadata track identification
struct MetadataTrackInfo {
    int track_index = -1;  ///< Track index in MP4 file
    std::string mime_type;  ///< MIME type from stsd, verbatim
    ParrotFormat format = ParrotFormat::V3;  ///< Format the MIME type names
    uint32_t sample_count = 0;  ///< Number of metadata samples
    uint64_t duration_us = 0;  ///< Track duration in microseconds
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

    /// Where a metadata sample sits in the file, as (absolute offset, size).
    ///
    /// For rewriting samples in place — patching a sample where it lies keeps
    /// every box, table and chunk offset in the file valid.
    std::optional<std::pair<uint64_t, uint32_t>> locate_sample(uint32_t sample_index) const;

    /// Zero every sample of `track_index` within `file`, an in-memory image of
    /// the same MP4. Returns the number of bytes zeroed.
    ///
    /// Sizes are preserved, so the file stays structurally intact — the media
    /// simply no longer decodes to anything.
    uint64_t zero_track_samples(int track_index, std::vector<uint8_t>& file) const;

private:
    Mp4MetadataExtractor() = default;

    struct Impl;  // Forward declare implementation (pimpl for minimp4 types)
    std::unique_ptr<Impl> impl_;
    MetadataTrackInfo track_info_;
};

}  // namespace parrot_to_klv
