// Copyright 2026 Eric Schneider
// Licensed under the Apache License, Version 2.0
//
// Checks the mux stage: an MP4 in, an MPEG-TS out carrying the source's video
// plus an ST 0601 KLV stream. Three properties, for both supported formats:
//
//   1. the output's PMT lists two elementary streams — H.264 video and KLV
//      (stream type 0x06 with a "KLVA" registration descriptor)
//   2. every KLV packet read back is byte-identical to what the transform
//      produced for that sample, in sample order
//   3. the KLV lands on the source's timeline: each packet comes back at its own
//      metadata sample's presentation time, to 90 kHz rounding
//   4. the video elementary stream carries ST 0604 Precision Time Stamps, and
//      each is an ST 0601 item 2 value we emitted — libmisbklv generates these
//      only when asked (its ADR 0024), so without this the output would look
//      correct in every other respect and still be useless to a client reading
//      time from the video
//
// The output is read back with libmisbklv's own KlvStream (a consumer's view,
// not our encoder's), while the PMT is parsed here — that check is about the
// container, so it must not go through the same code that wrote it.
//
// argv: <fixtures-dir> <temp-dir>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "misbklv/message.hpp"
#include "misbklv/stream.hpp"

#include "mp4_metadata_extractor.hpp"
#include "mux.hpp"
#include "parrot_proto.hpp"
#include "parrot_v3.hpp"
#include "session_metadata.hpp"
#include "telemetry.hpp"
#include "transform.hpp"

using namespace parrot_to_klv;

namespace {

int g_failures = 0;

void check(bool condition, const std::string& what) {
    if (!condition) {
        std::cerr << "FAIL: " << what << "\n";
        ++g_failures;
    }
}

// --- just enough MPEG-TS to see what was muxed ------------------------------

struct ElementaryStream {
    unsigned pid = 0;
    unsigned stream_type = 0;
    bool klva = false;  ///< carries the "KLVA" registration descriptor
};

/// The elementary streams the first PMT in `path` announces.
///
/// Read here rather than through the library, because what is being checked is
/// the *container* the muxer produced: asking the same library that wrote it
/// what it wrote would prove nothing.
///
/// Assumes the PMT fits in one 188-byte packet, which it does for a two-stream
/// program — this is a test's reader, not a demuxer.
std::vector<ElementaryStream> read_pmt(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    std::vector<char> raw((std::istreambuf_iterator<char>(file)),
                          std::istreambuf_iterator<char>());
    const auto* bytes = reinterpret_cast<const uint8_t*>(raw.data());

    for (size_t base = 0; base + 188 <= raw.size(); base += 188) {
        const uint8_t* pkt = bytes + base;
        if (pkt[0] != 0x47) break;
        const bool unit_start = (pkt[1] & 0x40) != 0;
        const unsigned adaptation = (pkt[3] >> 4) & 0x3;
        if ((adaptation & 0x1) == 0) continue;  // no payload
        size_t at = 4 + ((adaptation & 0x2) ? 1u + pkt[4] : 0u);
        if (!unit_start || at >= 188) continue;
        at += 1 + pkt[at];  // pointer_field
        if (at >= 188 || pkt[at] != 0x02) continue;  // table_id 0x02 = PMT

        std::vector<ElementaryStream> streams;
        const unsigned section_length = ((pkt[at + 1] & 0x0F) << 8) | pkt[at + 2];
        const size_t section_end = at + 3 + section_length - 4;  // less the CRC
        const unsigned program_info_length = ((pkt[at + 10] & 0x0F) << 8) | pkt[at + 11];
        for (size_t i = at + 12 + program_info_length; i + 5 <= section_end && i + 5 <= 188;) {
            ElementaryStream es;
            es.stream_type = pkt[i];
            es.pid = ((pkt[i + 1] & 0x1F) << 8) | pkt[i + 2];
            const unsigned es_info_length = ((pkt[i + 3] & 0x0F) << 8) | pkt[i + 4];
            for (size_t d = i + 5; d + 2 <= i + 5 + es_info_length;) {
                const unsigned tag = pkt[d], len = pkt[d + 1];
                if (tag == 0x05 && len >= 4 &&
                    std::string(reinterpret_cast<const char*>(pkt + d + 2), 4) == "KLVA") {
                    es.klva = true;
                }
                d += 2 + len;
            }
            streams.push_back(es);
            i += 5 + es_info_length;
        }
        return streams;  // the first PMT is enough
    }
    return {};
}

/// Concatenated PES payload for one PID — the elementary stream as muxed.
std::vector<uint8_t> read_es(const std::string& path, unsigned want_pid) {
    std::ifstream file(path, std::ios::binary);
    std::vector<char> raw((std::istreambuf_iterator<char>(file)),
                          std::istreambuf_iterator<char>());
    const auto* bytes = reinterpret_cast<const uint8_t*>(raw.data());

    std::vector<uint8_t> es, pes;
    auto flush = [&] {
        if (pes.size() >= 9) {
            const size_t header = 9 + pes[8];
            if (header <= pes.size()) es.insert(es.end(), pes.begin() + header, pes.end());
        }
        pes.clear();
    };
    for (size_t base = 0; base + 188 <= raw.size(); base += 188) {
        const uint8_t* pkt = bytes + base;
        if (pkt[0] != 0x47) break;
        if ((((pkt[1] & 0x1F) << 8) | pkt[2]) != want_pid) continue;
        const unsigned adaptation = (pkt[3] >> 4) & 0x3;
        if ((adaptation & 0x1) == 0) continue;
        size_t at = 4 + ((adaptation & 0x2) ? 1u + pkt[4] : 0u);
        if (at >= 188) continue;
        if (pkt[1] & 0x40) flush();  // payload_unit_start: the previous PES ended
        pes.insert(pes.end(), pkt + at, pkt + 188);
    }
    flush();
    return es;
}

/// Every ST 0604 Precision Time Stamp in an H.264 byte stream, in microseconds.
///
/// Finds the ASCII identifier "MISPmicrosectime" (ST 0604.6 §7.1) and reads the
/// 11-byte Modified Precision Time Stamp after the Time Status byte — 2-byte
/// groups split by 0xFF fillers, most significant first (§7.4 Table 2).
std::vector<uint64_t> read_0604_timestamps(const std::vector<uint8_t>& es) {
    static constexpr char kId[] = "MISPmicrosectime";
    constexpr size_t kIdLen = 16;
    std::vector<uint64_t> out;
    for (size_t i = 0; i + kIdLen + 12 <= es.size(); ++i) {
        if (std::memcmp(es.data() + i, kId, kIdLen) != 0) continue;
        const size_t ts = i + kIdLen + 1;  // past the Time Status byte
        if (es[ts + 2] != 0xFF || es[ts + 5] != 0xFF || es[ts + 8] != 0xFF) continue;
        static constexpr size_t kAt[8] = {0, 1, 3, 4, 6, 7, 9, 10};
        uint64_t v = 0;
        for (size_t b = 0; b < 8; ++b) v = (v << 8) | es[ts + kAt[b]];
        out.push_back(v);
        i += kIdLen + 12 - 1;
    }
    return out;
}

// --- what the transform produces, computed independently of the mux ---------

struct Expected {
    std::vector<std::vector<uint8_t>> packets;
    std::vector<uint64_t> timestamps_us;
};

template <typename Parse, typename Normalize>
Expected expected_packets(const std::string& mp4, Parse parse, Normalize normalize) {
    Expected out;
    auto extractor = Mp4MetadataExtractor::open(mp4);
    if (!extractor) return out;

    SessionMetadata session;
    if (FILE* handle = std::fopen(mp4.c_str(), "rb")) {
        session = parse_session_metadata(handle);
        std::fclose(handle);
    }

    uint64_t first_monotonic_us = 0;
    if (auto first = extractor->extract_sample(0)) {
        if (auto frame = parse(first->data)) {
            first_monotonic_us = normalize(*frame).monotonic_us;
        }
    }
    for (uint32_t i = 0; i < extractor->sample_count(); ++i) {
        auto sample = extractor->extract_sample(i);
        if (!sample) continue;
        auto frame = parse(sample->data);
        if (!frame) continue;
        auto snapshot = normalize(*frame);
        anchor_to_session(snapshot, session, first_monotonic_us);
        auto klv = to_0601(snapshot, &session);
        if (klv.packet.empty()) continue;
        out.packets.push_back(std::move(klv.packet));
        out.timestamps_us.push_back(sample->timestamp_us);
    }
    return out;
}

/// Convert one fixture and check the output against `expected`.
///
/// `expect_st0604` is the two branches of fork 7 (ADR 0007): an H.264 source
/// gets Precision Time Stamps generated into its video, anything else is
/// carried through without them. Both directions are asserted — "no SEI" is a
/// deliberate outcome here, not an absence worth shrugging at.
void check_conversion(const std::string& name, const std::string& mp4,
                      const std::string& ts, const Expected& expected,
                      bool expect_st0604 = true) {
    const auto report = mux_to_ts(mp4, ts);
    check(report.error.empty(), name + ": conversion failed — " + report.error);
    if (!report.error.empty()) return;

    check(report.frames_emitted == expected.packets.size(),
          name + ": emitted " + std::to_string(report.frames_emitted) + " packets, expected " +
              std::to_string(expected.packets.size()));
    check(report.frames_skipped == 0, name + ": skipped a sample it should have converted");
    check(!report.coverage.emitted.empty(), name + ": reported no coverage for the conversion");

    // 1. Both streams reached the PMT. A KLV-only output is the failure mode
    //    the video branch exists to prevent, so it is asserted, not assumed.
    const auto streams = read_pmt(ts);
    check(streams.size() == 2, name + ": PMT lists " + std::to_string(streams.size()) +
                                   " elementary stream(s), expected video + KLV");
    bool has_video = false, has_klv = false;
    for (const auto& es : streams) {
        // 0x1B is H.264, 0x24 HEVC — the codec is the source's business, not
        // this property's. What matters is that video survived the mux.
        if (es.stream_type == 0x1B || es.stream_type == 0x24) has_video = true;
        if (es.stream_type == 0x06 && es.klva) has_klv = true;
    }
    check(has_video, name + ": no video elementary stream in the output");
    check(has_klv, name + ": no KLV (0x06 + KLVA) elementary stream in the output");

    // 2 and 3. Read the KLV back as a consumer would — bytes and timing both.
    std::vector<misbklv::Message> read_back;
    std::vector<int64_t> pts;
    for (misbklv::Message& m : misbklv::KlvStream(ts)) {
        pts.push_back(m.pts());
        read_back.push_back(std::move(m));
    }
    check(read_back.size() == expected.packets.size(),
          name + ": read back " + std::to_string(read_back.size()) + " packets, expected " +
              std::to_string(expected.packets.size()));

    for (size_t i = 0; i < read_back.size() && i < expected.packets.size(); ++i) {
        auto encoded = read_back[i].encode();
        check(bool(encoded), name + ": packet " + std::to_string(i) + " did not re-encode");
        if (!encoded) continue;
        const auto& want = expected.packets[i];
        bool same = encoded->size() == want.size();
        for (size_t b = 0; same && b < want.size(); ++b) {
            same = static_cast<uint8_t>((*encoded)[b]) == want[b];
        }
        check(same, name + ": packet " + std::to_string(i) + " came back changed");
    }

    // 4. The video carries ST 0604 Precision Time Stamps, and they are OUR
    //    timestamps. This is the whole reason mux.cpp asks for
    //    Sei0604::Generate — libmisbklv's default leaves the video untouched
    //    (its ADR 0024), so without that argument this output would be silently
    //    correct in every other respect and useless to a client reading time
    //    from the video stream.
    check(report.st0604_generated == expect_st0604,
          name + ": report says st0604_generated=" +
              (report.st0604_generated ? "true" : "false") + ", expected the opposite");

    // stream_type 0x1B is H.264, 0x24 is HEVC. Take whichever the source used —
    // and *fail* if neither is present rather than skipping the checks below.
    // A guard that quietly passes on a missing video PID is how an H.265
    // regression stayed invisible long enough to become fork 7.
    unsigned video_pid = 0;
    for (const auto& es : streams)
        if (es.stream_type == 0x1B || es.stream_type == 0x24) video_pid = es.pid;
    check(video_pid != 0, name + ": PMT lists no video elementary stream");
    if (video_pid) {
        // ST 0604 carries ABSOLUTE time (ST 0601 item 2, µs since 1970), not the
        // presentation timestamps above — so the expectation is the item 2 value
        // inside each packet we expected to emit.
        std::vector<uint64_t> want_absolute;
        for (const auto& packet : expected.packets) {
            auto message = misbklv::Message::parse(
                std::as_bytes(std::span<const uint8_t>(packet)));
            if (!message) continue;
            if (auto item2 = message->get<uint64_t>(2)) want_absolute.push_back(*item2);
        }

        const auto stamps = read_0604_timestamps(read_es(ts, video_pid));
        if (expect_st0604) {
            check(!stamps.empty(), name + ": video elementary stream carries no ST 0604 SEI");
            size_t foreign = 0;
            for (uint64_t got : stamps)
                if (std::find(want_absolute.begin(), want_absolute.end(), got) ==
                    want_absolute.end())
                    ++foreign;
            check(foreign == 0, name + ": " + std::to_string(foreign) +
                                    " ST 0604 timestamp(s) match no emitted KLV item 2");
        } else {
            // Preserve must actually preserve: asking for generation on this
            // source would not have degraded to an unstamped stream, it would
            // have refused the conversion outright.
            check(stamps.empty(), name + ": expected no ST 0604 on a non-H.264 source, found " +
                                      std::to_string(stamps.size()));
        }
    }

    // Each packet must come back where its metadata sample sat — the same
    // timeline, origin included, not merely the same spacing. The only slack is
    // the 90 kHz grid the container rounds to.
    constexpr int64_t kTickNs = 1000000000 / 90000 + 1;
    for (size_t i = 0; i < pts.size() && i < expected.timestamps_us.size(); ++i) {
        const int64_t want = static_cast<int64_t>(expected.timestamps_us[i]) * 1000;
        check(std::abs(pts[i] - want) <= kTickNs,
              name + ": packet " + std::to_string(i) + " came back at " +
                  std::to_string(pts[i]) + " ns, expected " + std::to_string(want));
    }
}

}  // namespace

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <fixtures-dir> <temp-dir>\n";
        return 2;
    }
    const std::string fixtures = argv[1];
    const std::string temp = argv[2];

    check_conversion(
        "v3", fixtures + "/parrot_v3_video.mp4", temp + "/mux_v3.ts",
        expected_packets(
            fixtures + "/parrot_v3_video.mp4",
            [](std::span<const uint8_t> d) { return parse_v3(d, nullptr); },
            [](const ParrotV3Frame& f) { return from_v3(f); }));

    check_conversion(
        "proto", fixtures + "/parrot_proto_video.mp4", temp + "/mux_proto.ts",
        expected_packets(
            fixtures + "/parrot_proto_video.mp4",
            [](std::span<const uint8_t> d) { return parse_proto(d, nullptr); },
            [](const ParrotProtoFrame& f) { return from_proto(f); }));

    // The shape of a real recording: video + audio + metadata. Every assertion
    // above applies unchanged, and the PMT check earns a second meaning here —
    // exactly two elementary streams means the source's audio was *dropped*,
    // not forwarded. A source with audio also makes the muxer link a pad it has
    // no use for, which is where real footage found a bug no fixture had.
    check_conversion(
        "v3+audio", fixtures + "/parrot_v3_video_audio.mp4", temp + "/mux_audio.ts",
        expected_packets(
            fixtures + "/parrot_v3_video_audio.mp4",
            [](std::span<const uint8_t> d) { return parse_v3(d, nullptr); },
            [](const ParrotV3Frame& f) { return from_v3(f); }));

    // An H.265 source: the other branch of fork 7. It must convert like any
    // other — same packets, same timeline, video in the PMT — and come out with
    // no ST 0604, because generation is H.264-only upstream. Asking for it here
    // does not degrade, it fails the conversion, so this fixture is the guard
    // against a well-meaning change to mux.cpp putting Generate back.
    check_conversion(
        "v3+h265", fixtures + "/parrot_v3_video_h265.mp4", temp + "/mux_h265.ts",
        expected_packets(
            fixtures + "/parrot_v3_video_h265.mp4",
            [](std::span<const uint8_t> d) { return parse_v3(d, nullptr); },
            [](const ParrotV3Frame& f) { return from_v3(f); }),
        /*expect_st0604=*/false);

    // A source with no usable video stream must fail loudly and leave nothing
    // behind — a half-written .ts would look like a conversion that worked.
    const std::string orphan = temp + "/mux_novideo.ts";
    std::remove(orphan.c_str());
    const auto failed = mux_to_ts(fixtures + "/parrot_v3.mp4", orphan);
    check(!failed.error.empty(), "a fixture with no real video should not convert");
    check(!std::ifstream(orphan).good(), "a failed conversion left an output file behind");

    if (g_failures == 0) std::cout << "All mux tests passed\n";
    return g_failures == 0 ? 0 : 1;
}
