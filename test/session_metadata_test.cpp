// Copyright 2026 Eric Schneider
// Licensed under the Apache License, Version 2.0
//
// Checks the session-metadata parse against the tags test/make_fixture.py
// writes, the ISO 8601 -> epoch conversion (including the timezone offset, which
// is the part that silently ruins every frame's Item 2 if it is wrong), and the
// anchoring of a v3 monotonic clock to the media date.

#include "session_metadata.hpp"
#include "telemetry.hpp"
#include "transform.hpp"

#include <cstdint>
#include <cstdio>
#include <iostream>
#include <string>

using namespace parrot_to_klv;

namespace {

int g_failures = 0;

void check(bool condition, const std::string& what) {
    if (!condition) {
        std::cerr << "FAIL: " << what << "\n";
        ++g_failures;
    }
}

/// The line of `text` naming `item`, or "" — so an assertion about one item's
/// rendering does not depend on the report's column widths.
std::string line_for(const std::string& text, const std::string& item) {
    const auto at = text.find(item);
    if (at == std::string::npos) return {};
    const auto begin = text.rfind('\n', at);
    const auto end = text.find('\n', at);
    return text.substr(begin == std::string::npos ? 0 : begin + 1,
                       end == std::string::npos ? std::string::npos : end - (begin + 1));
}

template <typename T>
void check_eq(const T& actual, const T& expected, const std::string& what) {
    if (!(actual == expected)) {
        std::cerr << "FAIL: " << what << " — expected " << expected << ", got " << actual << "\n";
        ++g_failures;
    }
}

void check_fixture(const std::string& fixture_dir) {
    FILE* file = std::fopen((fixture_dir + "/parrot_v3.mp4").c_str(), "rb");
    if (!file) {
        std::cerr << "FAIL: cannot open parrot_v3.mp4\n";
        ++g_failures;
        return;
    }
    const SessionMetadata session = parse_session_metadata(file);
    std::fclose(file);

    check(!session.empty(), "session metadata is found");
    check(session.model && *session.model == "Anafi", "model from '©mod'");
    check(session.serial_number && *session.serial_number == "PI040416BA8G059745",
          "serial number from '©too'");
    check(session.friendly_name && *session.friendly_name == "ANAFI-G059745",
          "friendly name from '©ART'");
    check(session.maker && *session.maker == "Parrot", "maker from '©mak'");
    check(session.software_version && *session.software_version == "1.3.0",
          "software version from '©swr'");
    check(session.media_date && *session.media_date == "2018-10-06T18:12:52+02:00",
          "media date kept verbatim");
    // The geotag lives in moov/udta directly, not in the ilst — a parser that
    // only descends into ilst misses the recording's position entirely.
    check(session.geotag.has_value(), "geotag found outside the ilst");

    // 2018-10-06T18:12:52+02:00 is 16:12:52 UTC.
    check(session.media_date_epoch_s.has_value(), "media date parses to an epoch");
    if (session.media_date_epoch_s) {
        check_eq(*session.media_date_epoch_s, int64_t{1538842372},
                 "media date converts to UTC, honouring the +02:00 offset");
    }
}

void check_iso8601() {
    // The offset is the part that matters: dropping it would put every frame's
    // Item 2 two hours off, consistently and invisibly.
    check_eq(*parse_iso8601_to_epoch("2018-10-06T18:12:52+02:00"), int64_t{1538842372},
             "positive offset is subtracted");
    check_eq(*parse_iso8601_to_epoch("2018-10-06T16:12:52+00:00"), int64_t{1538842372},
             "UTC with an explicit zero offset");
    check_eq(*parse_iso8601_to_epoch("2018-10-06T10:12:52-06:00"), int64_t{1538842372},
             "negative offset is added");
    // Parrot writes a space instead of 'T' in some fields.
    check_eq(*parse_iso8601_to_epoch("2018-10-06 16:12:52+00:00"), int64_t{1538842372},
             "a space separator is accepted");

    check(!parse_iso8601_to_epoch("not a date").has_value(), "garbage does not parse");
    check(!parse_iso8601_to_epoch("").has_value(), "empty string does not parse");
    check(!parse_iso8601_to_epoch("2018-13-06T18:12:52+02:00").has_value(),
          "month 13 is rejected rather than normalized");
    check(!parse_iso8601_to_epoch("2018-10-06").has_value(), "a date with no time is rejected");
}

void check_anchoring() {
    SessionMetadata session;
    session.media_date_epoch_s = 1538842372;

    ParrotV3Frame frame;
    frame.drone_quat = Quaternion{1.0, 0.0, 0.0, 0.0};
    frame.frame_quat = frame.drone_quat;
    frame.frame_timestamp_us = 33333;

    auto snapshot = from_v3(frame);
    check(!snapshot.utc_timestamp_us.has_value(), "v3 starts with no absolute time");

    // First sample's clock was 0, so this frame is 33333 µs after the media date.
    anchor_to_session(snapshot, session, 0);
    check(snapshot.utc_timestamp_us.has_value(), "anchoring supplies an absolute time");
    if (snapshot.utc_timestamp_us) {
        check_eq(*snapshot.utc_timestamp_us, uint64_t{1538842372033333},
                 "anchored time is media date + elapsed monotonic");
    }

    // Item 2 now makes it into the packet, and the coverage no longer reports it
    // as missing.
    const auto klv = to_0601(snapshot, &session);
    check(klv.coverage.emitted.count(2) == 1, "Item 2 is emitted once anchored");
    check(klv.coverage.omitted.count(2) == 0, "…and no longer reported as omitted");

    // A session with no media date must leave the snapshot alone rather than
    // anchoring to the epoch.
    auto unanchored = from_v3(frame);
    anchor_to_session(unanchored, SessionMetadata{}, 0);
    check(!unanchored.utc_timestamp_us.has_value(),
          "no media date means no anchor, not a 1970 timestamp");

    // A frame that already has a trustworthy absolute time must not be
    // overwritten by a second-granularity anchor.
    ParrotProtoFrame proto;
    proto.utc_timestamp_us = 1770000000000000;
    proto.utc_timestamp_accuracy = 50;
    auto from_proto_snapshot = from_proto(proto);
    anchor_to_session(from_proto_snapshot, session, 0);
    check_eq(*from_proto_snapshot.utc_timestamp_us, uint64_t{1770000000000000},
             "a real UTC timestamp is not replaced by the anchor");
}

void check_session_items() {
    SessionMetadata session;
    session.model = "Anafi";
    session.serial_number = "PI040416BA8G059745";

    ParrotV3Frame frame;
    frame.drone_quat = Quaternion{1.0, 0.0, 0.0, 0.0};
    frame.frame_quat = frame.drone_quat;
    const auto klv = to_0601(from_v3(frame), &session);

    check(klv.coverage.emitted.count(4) == 1, "Item 4 tail number from the serial");
    check(klv.coverage.emitted.count(10) == 1, "Item 10 platform designation from the model");
    // Item 11 needs the camera spectrum, which nothing parses — it must stay
    // omitted rather than being assumed to be EO.
    check(klv.coverage.omitted.count(11) == 1, "Item 11 stays omitted without a spectrum");

    // The serial is device identity: withheld from a rendered report unless
    // the caller opts in, separately from the location opt-in.
    const std::string guarded = klv.coverage.to_text();
    check(guarded.find("PI040416") == std::string::npos, "serial is withheld by default");
    check(line_for(guarded, "Platform Tail Number").find("withheld") != std::string::npos,
          "…and Item 4 is marked withheld rather than silently dropped");
    check(klv.coverage.to_text(Disclosure{.location = true}).find("PI040416") ==
              std::string::npos,
          "revealing location does not reveal identity");
    check(klv.coverage.to_text(Disclosure{.identity = true}).find("PI040416") != std::string::npos,
          "identity disclosure reveals the serial");

    // Without session metadata the items are reported as blocked on it.
    const auto without = to_0601(from_v3(frame));
    check(without.coverage.omitted.at(4) == OmissionReason::NeedsSessionData,
          "Item 4 blames the missing session metadata");
    check(without.coverage.omitted.at(10) == OmissionReason::NeedsSessionData,
          "Item 10 blames the missing session metadata");
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: " << argv[0] << " <fixture-dir>\n";
        return 2;
    }
    check_fixture(argv[1]);
    check_iso8601();
    check_anchoring();
    check_session_items();

    if (g_failures > 0) {
        std::cerr << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "all session metadata checks passed\n";
    return 0;
}
