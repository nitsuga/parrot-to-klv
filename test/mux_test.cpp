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
//   3. the KLV lands on the video's timeline: packet PTS intervals match the
//      metadata track's sample spacing, to 90 kHz rounding
//
// The output is read back with libmisbklv's own KlvStream (a consumer's view,
// not our encoder's), while the PMT is parsed here — that check is about the
// container, so it must not go through the same code that wrote it.
//
// argv: <fixtures-dir> <temp-dir>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <optional>
#include <span>
#include <string>
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

struct TsSummary {
    std::vector<ElementaryStream> streams;   ///< what the first PMT announces
    std::vector<int64_t> klv_pts_90k;        ///< one per KLV PES, in order
};

/// The elementary streams and KLV presentation times in `path`.
///
/// The PES timestamps are read here rather than through the library's extract
/// path, which reports every packet as having no PTS — what is being checked is
/// where the muxer *placed* each packet, so it is read off the container.
///
/// Assumes the PMT fits in one 188-byte packet, which it does for a two-stream
/// program, and that a KLV packet is one PES — this is a test's reader, not a
/// demuxer.
TsSummary read_ts(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    std::vector<char> raw((std::istreambuf_iterator<char>(file)),
                          std::istreambuf_iterator<char>());
    const auto* bytes = reinterpret_cast<const uint8_t*>(raw.data());

    TsSummary summary;
    unsigned klv_pid = 0;
    for (size_t base = 0; base + 188 <= raw.size(); base += 188) {
        const uint8_t* pkt = bytes + base;
        if (pkt[0] != 0x47) break;
        const bool unit_start = (pkt[1] & 0x40) != 0;
        const unsigned pid = ((pkt[1] & 0x1F) << 8) | pkt[2];
        const unsigned adaptation = (pkt[3] >> 4) & 0x3;
        if ((adaptation & 0x1) == 0) continue;  // no payload
        size_t at = 4 + ((adaptation & 0x2) ? 1u + pkt[4] : 0u);
        if (!unit_start || at >= 188) continue;

        // A PES packet, not a section: KLV presentation times live here.
        if (klv_pid != 0 && pid == klv_pid && at + 14 <= 188 && pkt[at] == 0x00 &&
            pkt[at + 1] == 0x00 && pkt[at + 2] == 0x01) {
            const uint8_t* pes = pkt + at;
            if ((pes[7] >> 6) & 0x2) {  // PTS present
                const int64_t pts = (int64_t(pes[9] & 0x0E) << 29) |
                                    (int64_t(pes[10]) << 22) |
                                    (int64_t(pes[11] & 0xFE) << 14) |
                                    (int64_t(pes[12]) << 7) |
                                    (int64_t(pes[13]) >> 1);
                summary.klv_pts_90k.push_back(pts);
            }
            continue;
        }

        at += 1 + pkt[at];  // pointer_field
        if (at >= 188 || pkt[at] != 0x02) continue;  // table_id 0x02 = PMT
        if (!summary.streams.empty()) continue;      // first PMT is enough

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
            if (es.stream_type == 0x06) klv_pid = es.pid;
            summary.streams.push_back(es);
            i += 5 + es_info_length;
        }
    }
    return summary;
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
void check_conversion(const std::string& name, const std::string& mp4,
                      const std::string& ts, const Expected& expected) {
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
    const auto muxed = read_ts(ts);
    check(muxed.streams.size() == 2,
          name + ": PMT lists " + std::to_string(muxed.streams.size()) +
              " elementary stream(s), expected video + KLV");
    bool has_video = false, has_klv = false;
    for (const auto& es : muxed.streams) {
        if (es.stream_type == 0x1B) has_video = true;
        if (es.stream_type == 0x06 && es.klva) has_klv = true;
    }
    check(has_video, name + ": no H.264 elementary stream in the output");
    check(has_klv, name + ": no KLV (0x06 + KLVA) elementary stream in the output");

    // 2. Read the KLV back as a consumer would.
    std::vector<misbklv::Message> read_back;
    for (misbklv::Message& m : misbklv::KlvStream(ts)) {
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

    // 3. The muxer picks the stream's origin, so what has to hold is the
    //    *spacing*: frame n sits where the metadata track put it, relative to
    //    the first packet, within one 90 kHz tick of rounding.
    const auto& pts = muxed.klv_pts_90k;
    check(pts.size() == expected.packets.size(),
          name + ": " + std::to_string(pts.size()) + " KLV PES timestamps, expected " +
              std::to_string(expected.packets.size()));
    for (size_t i = 0; i < pts.size() && i < expected.timestamps_us.size(); ++i) {
        const int64_t want = static_cast<int64_t>(
            (expected.timestamps_us[i] - expected.timestamps_us[0]) * 90 / 1000);
        const int64_t got = pts[i] - pts[0];
        check(std::abs(got - want) <= 1,
              name + ": packet " + std::to_string(i) + " sits " + std::to_string(got) +
                  " ticks after the first, expected " + std::to_string(want));
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
