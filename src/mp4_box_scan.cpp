// Copyright 2026 Eric Schneider
// Licensed under the Apache License, Version 2.0

#include "mp4_box_scan.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace parrot_to_klv {
namespace {

// moov/trak/mdia/minf/stbl/stsd/<sample entry> is six levels; the slack absorbs
// wrappers some writers add without letting a cyclic or malformed file recurse
// without bound.
constexpr int kMaxDepth = 10;

// A sample entry string longer than this means we have lost the plot, not that
// the file has a very long MIME type.
constexpr size_t kMaxStringBytes = 512;

constexpr uint32_t kBoxMoov = fourcc("moov");
constexpr uint32_t kBoxTrak = fourcc("trak");
constexpr uint32_t kBoxMdia = fourcc("mdia");
constexpr uint32_t kBoxMinf = fourcc("minf");
constexpr uint32_t kBoxStbl = fourcc("stbl");
constexpr uint32_t kBoxTkhd = fourcc("tkhd");
constexpr uint32_t kBoxHdlr = fourcc("hdlr");
constexpr uint32_t kBoxStsd = fourcc("stsd");
constexpr uint32_t kBoxTref = fourcc("tref");
constexpr uint32_t kRefCdsc = fourcc("cdsc");

bool read_exact(FILE* file, void* dst, size_t n) {
    return std::fread(dst, 1, n, file) == n;
}

bool read_u32(FILE* file, uint32_t& out) {
    std::array<uint8_t, 4> b{};
    if (!read_exact(file, b.data(), b.size())) {
        return false;
    }
    out = (static_cast<uint32_t>(b[0]) << 24) | (static_cast<uint32_t>(b[1]) << 16) |
          (static_cast<uint32_t>(b[2]) << 8) | b[3];
    return true;
}

bool read_u64(FILE* file, uint64_t& out) {
    uint32_t hi = 0;
    uint32_t lo = 0;
    if (!read_u32(file, hi) || !read_u32(file, lo)) {
        return false;
    }
    out = (static_cast<uint64_t>(hi) << 32) | lo;
    return true;
}

/// A null-terminated UTF-8 string, bounded by the enclosing box.
/// Consumes the terminator. Returns false if the box ends without one.
bool read_c_string(FILE* file, int64_t box_end, std::string& out) {
    out.clear();
    while (ftell64(file) < box_end && out.size() < kMaxStringBytes) {
        int c = std::fgetc(file);
        if (c == EOF) {
            return false;
        }
        if (c == '\0') {
            return true;
        }
        out.push_back(static_cast<char>(c));
    }
    return false;
}

struct BoxHeader {
    uint32_t type = 0;
    int64_t body_begin = 0;  ///< absolute offset of the payload
    int64_t body_end = 0;    ///< absolute offset one past the payload
};

/// Read one box header at the current position. `parent_end` bounds it.
/// Returns false at a clean end-of-range or on a malformed header.
bool read_box_header(FILE* file, int64_t parent_end, BoxHeader& out) {
    const int64_t begin = ftell64(file);
    if (begin < 0 || parent_end < begin || parent_end - begin < 8) {
        return false;
    }

    uint32_t size32 = 0;
    if (!read_u32(file, size32) || !read_u32(file, out.type)) {
        return false;
    }

    int64_t size = size32;
    if (size32 == 1) {
        // 64-bit largesize follows the type.
        uint64_t large = 0;
        if (!read_u64(file, large) || large > static_cast<uint64_t>(INT64_MAX)) {
            return false;
        }
        size = static_cast<int64_t>(large);
    } else if (size32 == 0) {
        // Extends to the end of the enclosing range.
        size = parent_end - begin;
    }

    const int64_t cur = ftell64(file);
    if (cur < 0) {
        return false;
    }
    const int64_t header_bytes = cur - begin;
    // `begin + size > parent_end` can wrap for a hostile 64-bit largesize near
    // INT64_MAX. Safe subtraction `size > parent_end - begin` prevents signed overflow.
    if (size < header_bytes || size > parent_end - begin) {
        return false;
    }

    out.body_begin = begin + header_bytes;
    out.body_end = begin + size;
    return true;
}

void parse_tkhd(FILE* file, const BoxHeader& box, Mp4TrackDescription& track) {
    if (fseek64(file, box.body_begin, SEEK_SET) != 0) {
        return;
    }
    uint32_t version_and_flags = 0;
    if (!read_u32(file, version_and_flags)) {
        return;
    }
    // creation_time and modification_time widen with version 1. ISO/IEC
    // 14496-12 defines no tkhd version past 1; a version > 1 has an unknown
    // field layout here (Parrot never writes one), so reject rather than
    // guess -- reading version 0's 8-byte offset from a wider header would
    // land mid-field and yield a garbage track_id.
    const uint8_t version = static_cast<uint8_t>(version_and_flags >> 24);
    if (version > 1) {
        return;
    }
    const int64_t times_bytes = version == 1 ? 16 : 8;
    if (fseek64(file, times_bytes, SEEK_CUR) != 0) {
        return;
    }
    const int64_t pos = ftell64(file);
    if (pos < 0 || box.body_end - pos < 4) {
        return;
    }
    read_u32(file, track.track_id);
}

void parse_hdlr(FILE* file, const BoxHeader& box, Mp4TrackDescription& track) {
    // FullBox(4) + pre_defined(4), then handler_type.
    if (fseek64(file, box.body_begin + 8, SEEK_SET) != 0) {
        return;
    }
    const int64_t pos = ftell64(file);
    if (pos < 0 || box.body_end - pos < 4) {
        return;
    }
    read_u32(file, track.handler_type);
}

void parse_stsd(FILE* file, const BoxHeader& box, Mp4TrackDescription& track) {
    if (fseek64(file, box.body_begin, SEEK_SET) != 0) {
        return;
    }
    uint32_t version_and_flags = 0;
    uint32_t entry_count = 0;
    if (!read_u32(file, version_and_flags) || !read_u32(file, entry_count)) {
        return;
    }

    for (uint32_t i = 0; i < entry_count; ++i) {
        BoxHeader entry;
        if (!read_box_header(file, box.body_end, entry)) {
            return;
        }
        if (i == 0) {
            track.sample_entry_format = entry.type;
        }

        if (entry.type == kSampleEntryMett) {
            // SampleEntry: reserved[6] + data_reference_index(2).
            if (fseek64(file, entry.body_begin + 8, SEEK_SET) != 0) {
                return;
            }
            // TextMetaDataSampleEntry: an optional content_encoding then
            // mime_format, both null-terminated. Writers that omit
            // content_encoding put the MIME type first, so decide by content
            // rather than by position.
            std::string first;
            if (!read_c_string(file, entry.body_end, first)) {
                return;
            }
            if (first.find('/') != std::string::npos) {
                track.mime_format = first;
            } else {
                std::string second;
                if (read_c_string(file, entry.body_end, second)) {
                    track.mime_format = second;
                }
            }
            track.sample_entry_format = entry.type;
            return;
        }

        if (fseek64(file, entry.body_end, SEEK_SET) != 0) {
            return;
        }
    }
}

void parse_tref(FILE* file, const BoxHeader& box, Mp4TrackDescription& track) {
    if (fseek64(file, box.body_begin, SEEK_SET) != 0) {
        return;
    }
    // Each child's box type *is* the reference type; its payload is an array of
    // track IDs.
    BoxHeader ref;
    while (read_box_header(file, box.body_end, ref)) {
        if (ref.type == kRefCdsc) {
            const int64_t count = (ref.body_end - ref.body_begin) / 4;
            for (int64_t i = 0; i < count; ++i) {
                uint32_t id = 0;
                if (!read_u32(file, id)) {
                    break;
                }
                track.cdsc_track_ids.push_back(id);
            }
        }
        if (fseek64(file, ref.body_end, SEEK_SET) != 0) {
            return;
        }
    }
}

bool is_container(uint32_t type) {
    return type == kBoxMoov || type == kBoxTrak || type == kBoxMdia || type == kBoxMinf ||
           type == kBoxStbl;
}

/// Descend `[begin, end)`, appending a description per 'trak' met.
void walk(FILE* file, int64_t begin, int64_t end, int depth,
          std::vector<Mp4TrackDescription>& tracks) {
    if (depth > kMaxDepth || fseek64(file, begin, SEEK_SET) != 0) {
        return;
    }

    BoxHeader box;
    while (read_box_header(file, end, box)) {
        if (box.type == kBoxTrak) {
            tracks.emplace_back();
            walk(file, box.body_begin, box.body_end, depth + 1, tracks);
        } else if (is_container(box.type)) {
            walk(file, box.body_begin, box.body_end, depth + 1, tracks);
        } else if (!tracks.empty()) {
            // Leaves only mean something inside a 'trak'.
            Mp4TrackDescription& track = tracks.back();
            switch (box.type) {
                case kBoxTkhd: parse_tkhd(file, box, track); break;
                case kBoxHdlr: parse_hdlr(file, box, track); break;
                case kBoxStsd: parse_stsd(file, box, track); break;
                case kBoxTref: parse_tref(file, box, track); break;
                default: break;
            }
        }

        if (fseek64(file, box.body_end, SEEK_SET) != 0) {
            return;
        }
    }
}

}  // namespace

std::string fourcc_to_string(uint32_t code) {
    std::string out(4, '\0');
    for (int i = 0; i < 4; ++i) {
        const auto c = static_cast<unsigned char>((code >> (8 * (3 - i))) & 0xFF);
        out[i] = (c >= 0x20 && c < 0x7F) ? static_cast<char>(c) : '?';
    }
    return out;
}

std::vector<Mp4TrackDescription> scan_mp4_tracks(FILE* file) {
    std::vector<Mp4TrackDescription> tracks;
    if (!file || fseek64(file, 0, SEEK_END) != 0) {
        return tracks;
    }
    const int64_t file_size = ftell64(file);
    if (file_size <= 0) {
        return tracks;
    }

    // 'moov' may sit before or after 'mdat'; walk the top level to find it.
    if (fseek64(file, 0, SEEK_SET) != 0) {
        return tracks;
    }
    BoxHeader box;
    while (read_box_header(file, file_size, box)) {
        if (box.type == kBoxMoov) {
            walk(file, box.body_begin, box.body_end, 1, tracks);
            break;
        }
        if (fseek64(file, box.body_end, SEEK_SET) != 0) {
            break;
        }
    }
    return tracks;
}

bool read_mp4_box(const std::vector<uint8_t>& buf, size_t pos, size_t limit, Mp4Box& out) {
    auto be32 = [&buf](size_t at) {
        return static_cast<uint32_t>(buf[at]) << 24 | static_cast<uint32_t>(buf[at + 1]) << 16 |
               static_cast<uint32_t>(buf[at + 2]) << 8 | static_cast<uint32_t>(buf[at + 3]);
    };
    if (pos + 8 > limit) return false;
    uint64_t size = be32(pos);
    out.type = be32(pos + 4);
    size_t header = 8;
    if (size == 1) {
        // 64-bit largesize.
        if (pos + 16 > limit) return false;
        size = static_cast<uint64_t>(be32(pos + 8)) << 32 | be32(pos + 12);
        header = 16;
    } else if (size == 0) {
        // Extends to the end of the enclosing box.
        size = limit - pos;
    }
    // `pos + size > limit` can wrap for a hostile 64-bit largesize near
    // UINT64_MAX. `size > limit - pos` cannot: pos <= limit is already
    // guaranteed above (the pos+8/pos+16 > limit checks), so limit - pos
    // never underflows.
    if (size < header || size > limit - pos) return false;
    out.body_begin = pos + header;
    out.body_end = pos + static_cast<size_t>(size);
    return true;
}

}  // namespace parrot_to_klv
