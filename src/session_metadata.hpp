// Copyright 2026 Eric Schneider
// Licensed under the Apache License, Version 2.0

#pragma once

#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>

namespace parrot_to_klv {

/// Parrot's untimed ("session") metadata: what the recording says about the
/// device and the flight, as opposed to the per-frame telemetry.
///
/// Parrot writes these two ways — a `moov/udta/meta/ilst` box with iTunes-style
/// four-character keys, and a `moov/meta/ilst` box with full reverse-DNS keys.
/// This parses the **four-character (udta) method**, which every ANAFI-range
/// recording carries; see `parse_session_metadata` for what that leaves out.
///
/// Fields are absent when the recording does not carry them, which is common:
/// pre-4.1 Bebop firmware wrote no session metadata at all.
struct SessionMetadata {
    std::optional<std::string> friendly_name;     ///< '©ART', e.g. "ANAFI-G059745"
    std::optional<std::string> maker;             ///< '©mak', e.g. "Parrot"
    std::optional<std::string> model;             ///< '©mod', e.g. "Anafi"
    std::optional<std::string> serial_number;     ///< '©too' — device identity
    std::optional<std::string> software_version;  ///< '©swr'
    std::optional<std::string> title;             ///< '©nam'
    std::optional<std::string> copyright;         ///< '©cpy'
    /// '©day' — ISO 8601, e.g. "2018-10-06T18:12:52+02:00". Kept verbatim
    /// alongside the parsed form below, because the string is what the file
    /// actually says and the parse can fail.
    std::optional<std::string> media_date;
    /// `media_date` as seconds since the Unix epoch, when it parses. **Second
    /// granularity** — that is all the recording carries, and it bounds how
    /// precisely a v3 monotonic clock can be anchored to wall time.
    std::optional<int64_t> media_date_epoch_s;
    /// '©xyz' geotag, ISO 6709. **Location-bearing** — it is the take-off
    /// position, and it survives a telemetry-only scrub. Never print it
    /// without the caller asking.
    std::optional<std::string> geotag;

    /// True when nothing at all was found.
    bool empty() const;
};

/// Parse session metadata from an open MP4.
///
/// Walks `moov/udta`, both the geotag written directly there (Parrot puts
/// '©xyz' in `moov/udta` rather than the `ilst`, for Android compatibility) and
/// the `meta/ilst` tags beneath it.
///
/// **Only the four-character key method is read.** Parrot also writes a
/// `moov/meta/ilst` with full keys — `com.parrot.serial`, `com.parrot.model.id`,
/// `com.parrot.build.id`, run/boot/flight IDs — which needs the `keys` box to
/// map indices to names. Everything ST 0601 currently needs is available under
/// the four-character keys, so that path is not implemented; the fields it
/// would add have no 0601 item anyway.
///
/// Leaves the file position undefined. Returns an empty struct for a file with
/// no `udta`, which is not an error.
SessionMetadata parse_session_metadata(FILE* file);

/// Parse an ISO 8601 timestamp of the form Parrot writes into '©day'
/// ("2018-10-06T18:12:52+02:00", sometimes with a space instead of the 'T')
/// into seconds since the Unix epoch.
///
/// Returns nothing rather than guessing if the string does not match — a wrong
/// anchor would put every frame's Item 2 at the wrong wall-clock time.
std::optional<int64_t> parse_iso8601_to_epoch(const std::string& text);

}  // namespace parrot_to_klv
