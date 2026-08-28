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
                 "media date converts to UTC, honoring the +02:00 offset");
    }
}

void check_iso8601() {
    auto epoch_pos = parse_iso8601_to_epoch("2018-10-06T18:12:52+02:00");
    check(epoch_pos.has_value(), "positive offset parses");
    if (epoch_pos) {
        check_eq(*epoch_pos, int64_t{1538842372}, "positive offset is subtracted");
    }

    auto epoch_zero = parse_iso8601_to_epoch("2018-10-06T16:12:52+00:00");
    check(epoch_zero.has_value(), "UTC zero offset parses");
    if (epoch_zero) {
        check_eq(*epoch_zero, int64_t{1538842372}, "UTC with an explicit zero offset");
    }

    auto epoch_neg = parse_iso8601_to_epoch("2018-10-06T10:12:52-06:00");
    check(epoch_neg.has_value(), "negative offset parses");
    if (epoch_neg) {
        check_eq(*epoch_neg, int64_t{1538842372}, "negative offset is added");
    }

    // Parrot writes a space instead of 'T' in some fields.
    auto epoch_space = parse_iso8601_to_epoch("2018-10-06 16:12:52+00:00");
    check(epoch_space.has_value(), "space separator parses");
    if (epoch_space) {
        check_eq(*epoch_space, int64_t{1538842372}, "a space separator is accepted");
    }

    check(!parse_iso8601_to_epoch("not a date").has_value(), "garbage does not parse");
    check(!parse_iso8601_to_epoch("").has_value(), "empty string does not parse");
    check(!parse_iso8601_to_epoch("2018-13-06T18:12:52+02:00").has_value(),
          "month 13 is rejected rather than normalized");
    check(!parse_iso8601_to_epoch("2018-10-06").has_value(), "a date with no time is rejected");

    // Regression tests for issue #4: a malformed or out-of-range offset used
    // to leave offset_s at 0 and still succeed, anchoring every frame's Item 2
    // hours off instead of failing loudly.
    check(!parse_iso8601_to_epoch("2018-10-06T18:12:52+02").has_value(),
          "an offset missing its minutes is rejected, not silently treated as +00:00");
    check(!parse_iso8601_to_epoch("2018-10-06T18:12:52+99:99").has_value(),
          "an offset outside a real UTC range is rejected");
    check(!parse_iso8601_to_epoch("2018-10-06T18:12:52+24:00").has_value(),
          "an offset hour of exactly 24 is rejected");

    // Regression test for issue #4: timegm silently normalizes a
    // calendar-impossible date (glibc turns 2018-02-31 into 2018-03-03)
    // instead of rejecting it; the range checks above only catch values no
    // calendar has at all (day 32, month 13), not ones impossible in the
    // specific month named.
    check(!parse_iso8601_to_epoch("2018-02-31T12:00:00+00:00").has_value(),
          "February 31st is rejected rather than normalized to March 3rd");

    // Regression test for a PR #30 review finding: `%2d` happily parses a
    // sign, so "18:-1:52" scans successfully with minute == -1. timegm then
    // normalizes it to hour 17, minute 59 -- still within the same day, so
    // the calendar round-trip check above (which only compares
    // year/month/day) does not catch it. A negative hour/minute/second must
    // be rejected directly, the same way month 13 and day 32 already are.
    //
    // Of the three, only the minute case is cleanly isolated to this fix:
    // a negative hour crosses a day boundary on normalization, so it was
    // already caught by the day/month round-trip check above even before
    // this fix; a negative second happens to also get rejected by this
    // function's offset-count validation, because find_first_of("+-", ...)
    // finds the second's own '-' before the real trailing offset and feeds
    // a bogus tail to the offset parser, which fails its != 2 check. Both
    // are kept as regression tests -- they pin real, correct behavior --
    // but are not each independent proof of *this* fix the way the minute
    // case is.
    check(!parse_iso8601_to_epoch("2018-10-06T18:-1:52+00:00").has_value(),
          "a negative minute is rejected rather than normalized into the previous hour");
    check(!parse_iso8601_to_epoch("2018-10-06T-1:12:52+00:00").has_value(),
          "a negative hour is rejected rather than normalized into the previous day");
    check(!parse_iso8601_to_epoch("2018-10-06T18:12:-1+00:00").has_value(),
          "a negative second is rejected rather than normalized into the previous minute");
}

void check_hostile_largesize_is_rejected(const std::string& fixture_dir) {
    // A '©ART' box declaring a 64-bit largesize near UINT64_MAX, sitting
    // after a real '©day' tag in the same ilst -- a hardening test for issue
    // #4's `read_box` finding. `read_box`'s old `pos + size > limit` bounds
    // check computed in size_t could wrap for a size this large; the fixed
    // `size > limit - pos` cannot. Note this is a defense-in-depth test, not
    // a provable pre/post divergence: given this box's fixed 16-byte
    // largesize header, the wrapped sum can only ever land at or below
    // body_begin, so `entry_text`'s own `body_begin < body_end` guard
    // already absorbs this exact shape even without the read_box fix --
    // confirmed empirically, both before and after the fix report the same
    // result here. The fix is still correct (a bounds check should not rely
    // on a downstream guard it does not control) and this test pins that the
    // malformed box does not corrupt parsing of the real tag next to it.
    FILE* file = std::fopen((fixture_dir + "/hostile_read_box_largesize.mp4").c_str(), "rb");
    if (!file) {
        std::cerr << "FAIL: cannot open hostile_read_box_largesize.mp4\n";
        ++g_failures;
        return;
    }
    const SessionMetadata session = parse_session_metadata(file);
    std::fclose(file);

    check(session.media_date && *session.media_date == "2018-10-06T18:12:52+02:00",
          "the real tag before the hostile box still parses");
    check(!session.friendly_name.has_value(),
          "a box with a wrapping largesize is not read as a valid '©ART'");
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

void check_v3_absent_data(const std::string& fixture_dir) {
    auto extractor = Mp4MetadataExtractor::open(fixture_dir + "/parrot_v3_absent_data.mp4");
    check(extractor.has_value(), "v3 absent-data fixture opens");
    if (!extractor) return;

    const auto samples = extractor->extract_all_samples();
    check_eq(samples.size(), size_t{8}, "v3 absent-data fixture sample count");
    if (samples.empty()) return;

    const auto frame = parse_v3(samples.front().data);
    check(frame.has_value(), "v3 absent-data sample parses");
    if (!frame) return;
    check(frame->automation.has_value(), "absent-data sample keeps its automation extension");
    if (frame->automation) {
        check(frame->automation->framing_target_latitude_deg == 0.0,
              "absent-data framing latitude is the zero sentinel");
        check(frame->automation->framing_target_longitude_deg == 0.0,
              "absent-data framing longitude is the zero sentinel");
    }

    auto snapshot = from_v3(*frame);
    check(!snapshot.target.has_value(), "zero framing target stays absent");
    check(!snapshot.monotonic_us.has_value(), "missing timestamp stays absent");

    SessionMetadata session;
    session.media_date_epoch_s = 1538842372;
    anchor_to_session(snapshot, session, uint64_t{0});
    check(!snapshot.utc_timestamp_us.has_value(),
          "a frame without a timestamp is not anchored at session start");

    const auto klv = to_0601(snapshot, &session);
    check(klv.coverage.emitted.count(2) == 0, "missing timestamp emits no Item 2");
    const auto item2 = klv.coverage.omitted.find(2);
    check(item2 != klv.coverage.omitted.end() && item2->second == OmissionReason::SourceAbsent,
          "missing timestamp reports Item 2 as absent source data");
    for (uint16_t tag : {40, 41, 42}) {
        check(klv.coverage.emitted.count(tag) == 0,
              "zero framing target emits no target item");
        const auto omitted = klv.coverage.omitted.find(tag);
        check(omitted != klv.coverage.omitted.end() &&
                  omitted->second == OmissionReason::SourceAbsent,
              "zero framing target reports its item absent");
    }

    auto mixed_extractor = Mp4MetadataExtractor::open(
        fixture_dir + "/parrot_v3_first_timestamp_absent.mp4");
    check(mixed_extractor.has_value(), "mixed timestamp fixture opens");
    if (mixed_extractor) {
        const auto mixed_samples = mixed_extractor->extract_all_samples();
        check_eq(mixed_samples.size(), size_t{8}, "mixed timestamp fixture sample count");
        if (mixed_samples.size() >= 2) {
            const auto mixed_first = parse_v3(mixed_samples[0].data);
            const auto mixed_later = parse_v3(mixed_samples[1].data);
            check(mixed_first.has_value() && mixed_later.has_value(),
                  "mixed timestamp samples parse");
            if (mixed_first && mixed_later) {
                auto first_snapshot = from_v3(*mixed_first);
                auto later_snapshot = from_v3(*mixed_later);
                check(!first_snapshot.monotonic_us.has_value(),
                      "mixed fixture's first timestamp is absent");
                check(later_snapshot.monotonic_us.has_value() &&
                          *later_snapshot.monotonic_us == uint64_t{33333},
                      "mixed fixture's later timestamp is present");

                const auto mixed_origin = first_snapshot.monotonic_us;
                anchor_to_session(first_snapshot, session, mixed_origin);
                anchor_to_session(later_snapshot, session, mixed_origin);
                check(!first_snapshot.utc_timestamp_us.has_value(),
                      "mixed fixture's first frame stays unanchored");
                check(!later_snapshot.utc_timestamp_us.has_value(),
                      "later frames stay unanchored without an origin clock");

                const auto later_klv = to_0601(later_snapshot, &session);
                const auto later_item2 = later_klv.coverage.omitted.find(2);
                check(later_item2 != later_klv.coverage.omitted.end() &&
                          later_item2->second == OmissionReason::NeedsSessionData,
                      "a clocked frame without an origin reports missing session data");
            }
        }
    }

    // Presence, not the value, distinguishes a genuine zero clock reading
    // from a frame that did not carry the extension.
    ParrotV3Frame zero_clock = *frame;
    zero_clock.frame_timestamp_us = uint64_t{0};
    auto zero_snapshot = from_v3(zero_clock);
    check(zero_snapshot.monotonic_us.has_value() && *zero_snapshot.monotonic_us == 0,
          "an engaged zero timestamp remains a real clock reading");
    anchor_to_session(zero_snapshot, session, uint64_t{0});
    check(zero_snapshot.utc_timestamp_us.has_value() &&
              *zero_snapshot.utc_timestamp_us == uint64_t{1538842372} * 1000000ULL,
          "a genuine zero timestamp anchors to the session start");
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
    check_hostile_largesize_is_rejected(argv[1]);
    check_anchoring();
    check_v3_absent_data(argv[1]);
    check_session_items();

    if (g_failures > 0) {
        std::cerr << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "all session metadata checks passed\n";
    return 0;
}
