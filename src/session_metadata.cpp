// Copyright 2026 Eric Schneider
// Licensed under the Apache License, Version 2.0

#include "session_metadata.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "mp4_box_scan.hpp"

namespace parrot_to_klv {
namespace {

/// The four-character keys Parrot uses. 0xA9 is the copyright sign that leads
/// the iTunes-style keys; it cannot go through `fourcc()`, whose argument is a
/// plain char literal.
constexpr uint32_t kKeyArtist = 0xA9415254u;    // '©ART' friendly name
constexpr uint32_t kKeyMaker = 0xA96D616Bu;     // '©mak'
constexpr uint32_t kKeyModel = 0xA96D6F64u;     // '©mod'
constexpr uint32_t kKeyTool = 0xA9746F6Fu;      // '©too' serial number
constexpr uint32_t kKeySoftware = 0xA9737772u;  // '©swr'
constexpr uint32_t kKeyName = 0xA96E616Du;      // '©nam' title
constexpr uint32_t kKeyCopyright = 0xA9637079u; // '©cpy'
constexpr uint32_t kKeyDay = 0xA9646179u;       // '©day' media date
constexpr uint32_t kKeyXyz = 0xA978797Au;       // '©xyz' geotag

/// Extract the string from an ilst entry.
///
/// An entry is `<key>` containing a `data` box: 4 bytes of version/type, 4 of
/// locale, then the payload. Some writers put the string straight in the key
/// box instead, so both shapes are accepted.
std::optional<std::string> entry_text(const std::vector<uint8_t>& buf, const Mp4Box& entry) {
    Mp4Box data;
    if (read_mp4_box(buf, entry.body_begin, entry.body_end, data) &&
        data.type == fourcc("data") && data.body_begin + 8 <= data.body_end) {
        const size_t begin = data.body_begin + 8;
        return std::string(reinterpret_cast<const char*>(buf.data() + begin),
                           data.body_end - begin);
    }
    if (entry.body_begin < entry.body_end) {
        return std::string(reinterpret_cast<const char*>(buf.data() + entry.body_begin),
                           entry.body_end - entry.body_begin);
    }
    return std::nullopt;
}

/// A '©xyz' geotag is a plain box (not an ilst entry): size(2) language(2)
/// then the ISO 6709 string.
std::optional<std::string> geotag_text(const std::vector<uint8_t>& buf, const Mp4Box& box) {
    const size_t begin = box.body_begin + 4;
    if (begin >= box.body_end) return std::nullopt;
    return std::string(reinterpret_cast<const char*>(buf.data() + begin), box.body_end - begin);
}

/// Strip trailing NULs and whitespace some writers pad with.
std::string tidy(std::string text) {
    while (!text.empty() && (text.back() == '\0' || text.back() == ' ' || text.back() == '\n' ||
                             text.back() == '\r')) {
        text.pop_back();
    }
    return text;
}

}  // namespace

bool SessionMetadata::empty() const {
    return !friendly_name && !maker && !model && !serial_number && !software_version && !title &&
           !copyright && !media_date && !geotag;
}

std::optional<int64_t> parse_iso8601_to_epoch(const std::string& text) {
    int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
    char separator = 0;
    // "2018-10-06T18:12:52+02:00" — Parrot sometimes writes a space for the 'T'.
    if (std::sscanf(text.c_str(), "%4d-%2d-%2d%c%2d:%2d:%2d", &year, &month, &day, &separator,
                    &hour, &minute, &second) != 7) {
        return std::nullopt;
    }
    if (separator != 'T' && separator != ' ') return std::nullopt;
    // Lower bounds matter as much as upper ones: `%2d` happily parses a sign,
    // so "18:-1:52" scans successfully with minute == -1. Without this,
    // timegm's normalization (-1 minutes becomes hour-1, minute 59) can land
    // on a still-plausible time that the calendar round-trip check below
    // does not catch when it stays within the same day.
    if (month < 1 || month > 12 || day < 1 || day > 31 || hour < 0 || hour > 23 || minute < 0 ||
        minute > 59 || second < 0 || second > 60) {
        return std::nullopt;
    }

    std::tm tm{};
    tm.tm_year = year - 1900;
    tm.tm_mon = month - 1;
    tm.tm_mday = day;
    tm.tm_hour = hour;
    tm.tm_min = minute;
    tm.tm_sec = second;
    // timegm, not mktime: the local timezone of whoever runs the converter has
    // nothing to do with when the flight happened.
    const std::time_t utc = timegm(&tm);
    if (utc == static_cast<std::time_t>(-1)) return std::nullopt;
    // glibc's timegm normalizes an out-of-range struct in place rather than
    // rejecting it -- "2018-02-31" silently becomes 2018-03-03. The fields
    // above are still range-checked (month/day/hour/minute/second), which
    // only rules out values no calendar has at all; a date impossible in
    // *this* calendar (day 31 of a 28-day February) is caught here by
    // comparing against what timegm actually normalized to.
    if (tm.tm_mon != month - 1 || tm.tm_mday != day || tm.tm_year != year - 1900) {
        return std::nullopt;
    }

    // Apply the trailing offset, if any: "+02:00" means the wall-clock reading
    // is two hours ahead of UTC, so UTC is two hours earlier. An offset that
    // fails to parse or falls outside a real UTC offset's range must reject
    // the whole timestamp rather than silently fall back to +00:00 -- that
    // would anchor every frame's Item 2 at a confidently wrong wall time.
    const size_t time_end = text.find(separator);
    const size_t offset_pos = text.find_first_of("+-", time_end == std::string::npos ? 0 : time_end);
    int64_t offset_s = 0;
    if (offset_pos != std::string::npos) {
        int offset_hour = 0, offset_minute = 0;
        if (std::sscanf(text.c_str() + offset_pos + 1, "%2d:%2d", &offset_hour, &offset_minute) !=
            2) {
            return std::nullopt;
        }
        if (offset_hour < 0 || offset_hour > 23 || offset_minute < 0 || offset_minute > 59) {
            return std::nullopt;
        }
        offset_s = (static_cast<int64_t>(offset_hour) * 3600) + (offset_minute * 60);
        if (text[offset_pos] == '-') offset_s = -offset_s;
    }
    return static_cast<int64_t>(utc) - offset_s;
}

SessionMetadata parse_session_metadata(FILE* file) {
    SessionMetadata session;
    if (!file) return session;

    if (std::fseek(file, 0, SEEK_END) != 0) return session;
    const int64_t file_size = std::ftell(file);
    if (file_size <= 0) return session;
    if (std::fseek(file, 0, SEEK_SET) != 0) return session;

    // Scan top-level box headers seeking 'moov', so we only read the metadata
    // container into memory rather than buffering multi-gigabyte media tracks.
    int64_t moov_begin = -1;
    int64_t moov_end = -1;
    int64_t pos = 0;
    while (pos + 8 <= file_size) {
        if (std::fseek(file, static_cast<long>(pos), SEEK_SET) != 0) break;
        uint8_t hdr[8];
        if (std::fread(hdr, 1, 8, file) != 8) break;
        uint64_t size = (static_cast<uint64_t>(hdr[0]) << 24) | (static_cast<uint64_t>(hdr[1]) << 16) |
                        (static_cast<uint64_t>(hdr[2]) << 8) | hdr[3];
        const uint32_t type = (static_cast<uint32_t>(hdr[4]) << 24) | (static_cast<uint32_t>(hdr[5]) << 16) |
                              (static_cast<uint32_t>(hdr[6]) << 8) | hdr[7];
        int64_t header_len = 8;
        if (size == 1) {
            uint8_t hdr16[8];
            if (std::fread(hdr16, 1, 8, file) != 8) break;
            size = 0;
            for (int i = 0; i < 8; ++i) size = (size << 8) | hdr16[i];
            header_len = 16;
        } else if (size == 0) {
            size = static_cast<uint64_t>(file_size - pos);
        }
        if (size < static_cast<uint64_t>(header_len) || pos + static_cast<int64_t>(size) > file_size) {
            break;
        }
        if (type == fourcc("moov")) {
            moov_begin = pos + header_len;
            moov_end = pos + static_cast<int64_t>(size);
            break;
        }
        pos += static_cast<int64_t>(size);
    }

    if (moov_begin < 0 || moov_end <= moov_begin) return session;

    const int64_t moov_len = moov_end - moov_begin;
    constexpr int64_t kMaxMoovBytes = 100 * 1024 * 1024;  // 100 MB ceiling
    if (moov_len > kMaxMoovBytes) return session;

    std::vector<uint8_t> buf(static_cast<size_t>(moov_len));
    if (std::fseek(file, static_cast<long>(moov_begin), SEEK_SET) != 0) return session;
    if (std::fread(buf.data(), 1, buf.size(), file) != buf.size()) return session;

    const auto store = [&](std::optional<std::string>& field, std::optional<std::string> value) {
        if (!value) return;
        std::string text = tidy(std::move(*value));
        if (!text.empty()) field = std::move(text);
    };

    // moov -> udta -> (©xyz | meta -> ilst -> <key>).
    const auto walk = [&](auto&& self, size_t begin, size_t end, int depth) -> void {
        if (depth > 6) return;
        size_t pos = begin;
        Mp4Box box;
        while (read_mp4_box(buf, pos, end, box)) {
            switch (box.type) {
                case kKeyXyz: store(session.geotag, geotag_text(buf, box)); break;
                case kKeyArtist: store(session.friendly_name, entry_text(buf, box)); break;
                case kKeyMaker: store(session.maker, entry_text(buf, box)); break;
                case kKeyModel: store(session.model, entry_text(buf, box)); break;
                case kKeyTool: store(session.serial_number, entry_text(buf, box)); break;
                case kKeySoftware: store(session.software_version, entry_text(buf, box)); break;
                case kKeyName: store(session.title, entry_text(buf, box)); break;
                case kKeyCopyright: store(session.copyright, entry_text(buf, box)); break;
                case kKeyDay: store(session.media_date, entry_text(buf, box)); break;
                default:
                    if (box.type == fourcc("moov") || box.type == fourcc("udta") ||
                        box.type == fourcc("meta") || box.type == fourcc("ilst")) {
                        size_t child = box.body_begin;
                        // 'meta' is a FullBox: its version/flags precede its
                        // children. 'udta' and 'ilst' are not.
                        if (box.type == fourcc("meta") && child + 4 <= box.body_end) child += 4;
                        self(self, child, box.body_end, depth + 1);
                    }
                    break;
            }
            pos = box.body_end;
        }
    };
    walk(walk, 0, buf.size(), 0);

    if (session.media_date) {
        session.media_date_epoch_s = parse_iso8601_to_epoch(*session.media_date);
    }
    return session;
}

}  // namespace parrot_to_klv
