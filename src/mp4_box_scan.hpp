// Copyright 2026 Eric Schneider
// Licensed under the Apache License, Version 2.0

#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace parrot_to_klv {

/// Pack a four-character box/handler code big-endian, e.g. fourcc("meta").
constexpr uint32_t fourcc(const char (&s)[5]) {
    return (static_cast<uint32_t>(static_cast<uint8_t>(s[0])) << 24) |
           (static_cast<uint32_t>(static_cast<uint8_t>(s[1])) << 16) |
           (static_cast<uint32_t>(static_cast<uint8_t>(s[2])) << 8) |
           static_cast<uint32_t>(static_cast<uint8_t>(s[3]));
}

/// Render a four-character code back to text, for diagnostics.
std::string fourcc_to_string(uint32_t code);

inline constexpr uint32_t kHandlerMeta = fourcc("meta");
inline constexpr uint32_t kHandlerVide = fourcc("vide");
inline constexpr uint32_t kSampleEntryMett = fourcc("mett");

/// Per-track facts recovered by walking the MP4 box tree.
///
/// minimp4 keeps only what it needs to demux audio and video: `MP4D_track_t`'s
/// SampleDescription union covers 'soun' and 'vide' alone, so a timed metadata
/// track's sample entry — and with it the MIME type naming the payload format —
/// is parsed past and dropped. This carries the pieces it discards.
struct Mp4TrackDescription {
    uint32_t track_id = 0;
    uint32_t handler_type = 0;         ///< 'meta', 'vide', 'soun', ...
    uint32_t sample_entry_format = 0;  ///< 'mett' for a text metadata track
    std::string mime_format;           ///< stsd mime_format, verbatim
    std::vector<uint32_t> cdsc_track_ids;  ///< tref/cdsc — tracks this one describes
};

/// Walk `file`'s box tree and describe every 'trak', in file order.
///
/// File order is what lets a description be lined up with the corresponding
/// minimp4 track index; minimp4 appends a track per 'trak' box as it meets them.
/// Leaves the file position undefined. Returns empty if there is no readable
/// 'moov'.
std::vector<Mp4TrackDescription> scan_mp4_tracks(FILE* file);

}  // namespace parrot_to_klv
