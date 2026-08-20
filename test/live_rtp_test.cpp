// Copyright 2026 Eric Schneider
// Licensed under the Apache License, Version 2.0

#include "live/live_ingest.hpp"
#include "live/parrot_rtp_header_extension.hpp"

#include <cassert>
#include <cstdio>
#include <chrono>
#include <thread>
#include <filesystem>
#include <iostream>
#include <span>
#include <vector>

#include <gst/gst.h>
#include <gst/rtp/gstrtphdrext.h>

#include "misbklv/stream.hpp"
#include "mp4_metadata_extractor.hpp"
#include "transform.hpp"

static bool test_extension_registered() {
    std::cout << "test_extension_registered\n";
    bool ok = parrot_to_klv::register_parrot_extension();
    if (!ok) {
        std::cerr << "  register failed\n";
        return false;
    }
    // Verify extension is registered and can be created from URI.
    auto* ext =
        gst_rtp_header_extension_create_from_uri(parrot_to_klv::kParrotRtpHeaderExtensionUri);
    if (!ext) {
        std::cerr << "  create_from_uri returned null\n";
        return false;
    }
    // Verify supported flags advertise at least one form.
    auto flags = gst_rtp_header_extension_get_supported_flags(ext);
    if ((flags & (GST_RTP_HEADER_EXTENSION_ONE_BYTE | GST_RTP_HEADER_EXTENSION_TWO_BYTE)) == 0) {
        std::cerr << "  flags missing one/two byte\n";
        gst_object_unref(ext);
        return false;
    }
    gst_object_unref(ext);
    std::cout << "  ok\n";
    return true;
}

static bool test_read_captures_bytes() {
    std::cout << "test_read_captures_bytes\n";
    parrot_to_klv::parrot_extension_clear();
    if (!parrot_to_klv::register_parrot_extension()) {
        std::cerr << "  register failed\n";
        return false;
    }
    auto* ext =
        gst_rtp_header_extension_create_from_uri(parrot_to_klv::kParrotRtpHeaderExtensionUri);
    if (!ext) {
        std::cerr << "  create_from_uri returned null\n";
        return false;
    }
    // The RTP stack sets the extension ID via caps before calling read.
    gst_rtp_header_extension_set_id(ext, 1);
    // Simulate the RTP stack calling read with dummy Parrot payload.
    // Parrot P3 header is 0x5033 plus length word count plus payload;
    // for the stub we just pass opaque bytes.
    const guint8 dummy[] = {0x50, 0x33, 0x00, 0x04, 0x01, 0x02, 0x03, 0x04};
    GstBuffer* buf = gst_buffer_new();
    GST_BUFFER_PTS(buf) = 123456789;
    gboolean ret = gst_rtp_header_extension_read(ext,
                                                 GST_RTP_HEADER_EXTENSION_ONE_BYTE,
                                                 dummy, sizeof(dummy), buf);
    gst_buffer_unref(buf);
    if (!ret) {
        std::cerr << "  read returned FALSE\n";
        gst_object_unref(ext);
        return false;
    }
    auto popped = parrot_to_klv::parrot_extension_pop_next();
    if (!popped) {
        std::cerr << "  pop returned nullopt\n";
        gst_object_unref(ext);
        return false;
    }
    if (popped->payload.size() != sizeof(dummy)) {
        std::cerr << "  size mismatch: " << popped->payload.size() << " vs " << sizeof(dummy) << "\n";
        gst_object_unref(ext);
        return false;
    }
    for (size_t i = 0; i < sizeof(dummy); ++i) {
        if (popped->payload[i] != static_cast<std::byte>(dummy[i])) {
            std::cerr << "  byte mismatch at " << i << "\n";
            gst_object_unref(ext);
            return false;
        }
    }
    if (popped->pts_ns != 123456789) {
        std::cerr << "  pts mismatch: " << popped->pts_ns << " vs 123456789\n";
        gst_object_unref(ext);
        return false;
    }
    // Second pop should be empty
    if (parrot_to_klv::parrot_extension_pop_next().has_value()) {
        std::cerr << "  second pop should be empty\n";
        gst_object_unref(ext);
        return false;
    }
    gst_object_unref(ext);
    std::cout << "  ok\n";
    return true;
}

static bool test_fifo_order() {
    std::cout << "test_fifo_order\n";
    parrot_to_klv::parrot_extension_clear();
    if (!parrot_to_klv::register_parrot_extension()) {
        std::cerr << "  register failed\n";
        return false;
    }
    auto* ext =
        gst_rtp_header_extension_create_from_uri(parrot_to_klv::kParrotRtpHeaderExtensionUri);
    if (!ext) {
        std::cerr << "  create_from_uri returned null\n";
        return false;
    }
    gst_rtp_header_extension_set_id(ext, 1);
    const guint8 p1[] = {0x50, 0x31, 0x01};
    const guint8 p2[] = {0x50, 0x33, 0x02, 0x03};
    GstBuffer* buf = gst_buffer_new();
    GST_BUFFER_PTS(buf) = 1000;
    if (!gst_rtp_header_extension_read(ext, GST_RTP_HEADER_EXTENSION_ONE_BYTE, p1, sizeof(p1), buf)) {
        std::cerr << "  read p1 failed\n";
        gst_buffer_unref(buf);
        gst_object_unref(ext);
        return false;
    }
    GST_BUFFER_PTS(buf) = 2000;
    if (!gst_rtp_header_extension_read(ext, GST_RTP_HEADER_EXTENSION_ONE_BYTE, p2, sizeof(p2), buf)) {
        std::cerr << "  read p2 failed\n";
        gst_buffer_unref(buf);
        gst_object_unref(ext);
        return false;
    }
    gst_buffer_unref(buf);
    if (parrot_to_klv::parrot_extension_count() != 2) {
        std::cerr << "  count expected 2, got " << parrot_to_klv::parrot_extension_count() << "\n";
        gst_object_unref(ext);
        return false;
    }
    auto first = parrot_to_klv::parrot_extension_pop_next();
    auto second = parrot_to_klv::parrot_extension_pop_next();
    if (!first || !second) {
        std::cerr << "  fifo pop missing\n";
        gst_object_unref(ext);
        return false;
    }
    if (first->payload.size() != sizeof(p1) || second->payload.size() != sizeof(p2)) {
        std::cerr << "  fifo size mismatch\n";
        gst_object_unref(ext);
        return false;
    }
    for (size_t i = 0; i < sizeof(p1); ++i) {
        if (first->payload[i] != static_cast<std::byte>(p1[i])) {
            std::cerr << "  fifo p1 byte mismatch at " << i << "\n";
            gst_object_unref(ext);
            return false;
        }
    }
    for (size_t i = 0; i < sizeof(p2); ++i) {
        if (second->payload[i] != static_cast<std::byte>(p2[i])) {
            std::cerr << "  fifo p2 byte mismatch at " << i << "\n";
            gst_object_unref(ext);
            return false;
        }
    }
    if (first->pts_ns != 1000 || second->pts_ns != 2000) {
        std::cerr << "  fifo pts mismatch\n";
        gst_object_unref(ext);
        return false;
    }
    parrot_to_klv::parrot_extension_clear();
    gst_object_unref(ext);
    std::cout << "  ok\n";
    return true;
}

static bool test_live_ingest_api() {
    std::cout << "test_live_ingest_api\n";
    parrot_to_klv::LiveIngestConfig cfg;
    cfg.rtsp_uri = "rtsp://192.168.0.1/live";
    cfg.udp_sink = "udp:239.255.0.1:5000";
    auto t0 = std::chrono::steady_clock::now();
    auto r = parrot_to_klv::live_ingest(cfg);
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::steady_clock::now() - t0)
                       .count();
    if (r) {
        std::cerr << "  expected Unsupported error, got ok\n";
        return false;
    }
    if (r.error() != misbklv::Error::Unsupported) {
        std::cerr << "  unexpected error code: " << static_cast<int>(r.error()) << "\n";
        return false;
    }
    // Should fail within 7 s (5 s RTSP timeout + overhead), not hang.
    if (elapsed > 7000) {
        std::cerr << "  rtsp unreachable took too long: " << elapsed << " ms\n";
        return false;
    }
    std::cout << "  rtsp unreachable correctly Unsupported in " << elapsed << " ms\n";
    // Empty config should be Unsupported (validation before network).
    parrot_to_klv::LiveIngestConfig empty;
    auto r2 = parrot_to_klv::live_ingest(empty);
    if (r2) {
        std::cerr << "  empty cfg expected error\n";
        return false;
    }
    if (r2.error() != misbklv::Error::Unsupported) {
        std::cerr << "  empty cfg expected Unsupported, got " << static_cast<int>(r2.error()) << "\n";
        return false;
    }
    std::cout << "  ok\n";
    return true;
}

static std::string pick_encoder() {
    const char* candidates[] = {"openh264enc", "x264enc", "avenc_h264", "x265enc"};
    for (auto* name : candidates) {
        GstElementFactory* f = gst_element_factory_find(name);
        if (f) {
            gst_object_unref(f);
            return name;
        }
    }
    return {};
}
static std::string pipeline_desc_for(const std::string& enc) {
    if (enc == "x265enc") {
        return "pipeline:videotestsrc is-live=true num-buffers=30 ! videoconvert ! video/x-raw,width=320,height=240,framerate=30/1 ! " + enc + " ! h265parse";
    }
    return "pipeline:videotestsrc is-live=true num-buffers=30 ! videoconvert ! video/x-raw,width=320,height=240,framerate=30/1 ! " + enc + " ! h264parse";
}

static bool test_pipeline_hermetic() {
    std::cout << "test_pipeline_hermetic\n";
    parrot_to_klv::parrot_extension_clear();
    std::string enc = pick_encoder();
    if (enc.empty()) {
        std::cout << "  SKIP: no H264/H265 encoder available\n";
        return true;
    }
    const std::string out = "/tmp/live_test.ts";
    std::filesystem::remove(out);
    parrot_to_klv::LiveIngestConfig cfg;
    cfg.rtsp_uri = pipeline_desc_for(enc);
    cfg.udp_sink = "file:" + out;
    std::cout << "  using encoder " << enc << " desc: " << cfg.rtsp_uri << "\n";
    auto r = parrot_to_klv::live_ingest(cfg);
    if (!r) {
        std::cerr << "  pipeline ingest failed: " << static_cast<int>(r.error()) << "\n";
        return false;
    }
    if (!std::filesystem::exists(out)) {
        std::cerr << "  output file not created\n";
        return false;
    }
    auto sz = std::filesystem::file_size(out);
    if (sz < 1000) {
        std::cerr << "  output too small: " << sz << "\n";
        std::filesystem::remove(out);
        return false;
    }
    // Read back KLV — should have 3 packets with PTS 1s, 2s, 3s.
    size_t count = 0;
    std::vector<int64_t> pts;
    for (misbklv::Message& m : misbklv::KlvStream(out)) {
        ++count;
        pts.push_back(m.pts());
    }
    if (count != 3) {
        std::cerr << "  expected 3 KLV packets, got " << count << "\n";
        std::filesystem::remove(out);
        return false;
    }
    for (size_t i = 0; i < pts.size(); ++i) {
        int64_t want = 1'000'000'000LL + static_cast<int64_t>(i) * 1'000'000'000LL;
        // Live pipeline PTS is running_time; absolute offset from pipeline start
        // may be ~tens of ms (warmup). Check drift from first packet, not absolute.
        int64_t base_delta = pts[0] - (1'000'000'000LL);
        int64_t delta = (pts[i] - want) - base_delta;
        if (delta < 0) delta = -delta;
        if (delta > 100'000'000) {
            std::cerr << "  packet " << i << " pts " << pts[i] << " want " << want << " base_delta " << base_delta << " drift " << delta << "\n";
            std::filesystem::remove(out);
            return false;
        }
    }
    std::cout << "  ok (" << sz << " bytes, " << count << " packets)\n";
    std::filesystem::remove(out);
    return true;
}

static bool test_rtsp_unreachable() {
    std::cout << "test_rtsp_unreachable\n";
    parrot_to_klv::parrot_extension_clear();
    parrot_to_klv::LiveIngestConfig cfg;
    cfg.rtsp_uri = "rtsp://127.0.0.1:9/unreachable";
    cfg.udp_sink = "file:/tmp/live_rtsp_unreach.ts";
    std::filesystem::remove("/tmp/live_rtsp_unreach.ts");
    auto t0 = std::chrono::steady_clock::now();
    auto r = parrot_to_klv::live_ingest(cfg);
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::steady_clock::now() - t0)
                       .count();
    if (r) {
        std::cerr << "  expected Unsupported, got ok\n";
        std::filesystem::remove("/tmp/live_rtsp_unreach.ts");
        return false;
    }
    if (r.error() != misbklv::Error::Unsupported) {
        std::cerr << "  expected Unsupported, got " << static_cast<int>(r.error()) << "\n";
        return false;
    }
    if (elapsed > 7000) {
        std::cerr << "  rtsp unreachable took too long: " << elapsed << " ms\n";
        return false;
    }
    // No file should remain for a failed live open (sink discards).
    if (std::filesystem::exists("/tmp/live_rtsp_unreach.ts")) {
        std::cerr << "  unreachable left a file behind\n";
        std::filesystem::remove("/tmp/live_rtsp_unreach.ts");
        return false;
    }
    std::cout << "  ok in " << elapsed << " ms\n";
    return true;
}

static std::string g_fixture_dir;

static std::string resolve_fixture_dir(int argc, char** argv) {
    // Preferred: argv[1] passed from CMake add_test, like all other tests.
    if (argc > 1) {
        std::string cand = argv[1];
        if (std::filesystem::exists(cand + "/parrot_v3.mp4")) {
            return cand;
        }
    }
    // Fallback for direct binary invocation: walk from /proc/self/exe.
    try {
        auto exe = std::filesystem::read_symlink("/proc/self/exe");
        auto dir = exe.parent_path();
        for (int up = 0; up < 4; ++up) {
            auto cand = dir / "test" / "fixtures";
            if (std::filesystem::exists(cand / "parrot_v3.mp4")) return cand.string();
            cand = dir / ".." / "test" / "fixtures";
            if (std::filesystem::exists(cand / "parrot_v3.mp4")) return cand.string();
            dir = dir.parent_path();
        }
    } catch (...) {}
    // Last resort: relative paths for manual runs from build dir.
    const char* rel_candidates[] = {"test/fixtures", "../test/fixtures", "./test/fixtures", nullptr};
    for (int i = 0; rel_candidates[i] != nullptr; ++i) {
        if (std::filesystem::exists(std::string(rel_candidates[i]) + "/parrot_v3.mp4")) {
            return rel_candidates[i];
        }
    }
    return {};
}

static parrot_to_klv::CapturedExtension make_cap(std::span<const uint8_t> data, uint64_t pts_ns) {
    parrot_to_klv::CapturedExtension cap;
    cap.payload.reserve(data.size());
    for (auto b : data) cap.payload.push_back(static_cast<std::byte>(b));
    cap.pts_ns = pts_ns;
    return cap;
}

static bool test_real_decode() {
    std::cout << "test_real_decode\n";
    parrot_to_klv::parrot_extension_clear();
    std::string fixture_dir = g_fixture_dir;
    if (fixture_dir.empty()) {
        std::fprintf(stderr, "FATAL: fixture dir not found (pass as argv[1] or run from build tree)\n");
        std::abort();
    }
    std::string enc = pick_encoder();
    if (enc.empty()) {
        std::cout << "  SKIP: no encoder available\n";
        return true;
    }

    // --- v3 real decode ---
    {
        auto extractor = parrot_to_klv::Mp4MetadataExtractor::open(fixture_dir + "/parrot_v3.mp4");
        if (!extractor) {
            std::cerr << "  cannot open parrot_v3.mp4\n";
            return false;
        }
        auto s0 = extractor->extract_sample(0);
        auto s1 = extractor->extract_sample(1);
        if (!s0 || !s1) {
            std::cerr << "  cannot extract v3 samples\n";
            return false;
        }
        // Verify the samples decode via existing parsers and to_0601 before pushing.
        auto f0 = parrot_to_klv::parse_v3(std::span<const uint8_t>(s0->data.data(), s0->data.size()));
        if (!f0) {
            std::cerr << "  v3 sample 0 does not parse\n";
            return false;
        }
        auto snap = parrot_to_klv::from_v3(*f0);
        auto klv = parrot_to_klv::to_0601(snap, nullptr);
        if (klv.packet.empty()) {
            std::cerr << "  v3 to_0601 empty\n";
            return false;
        }

        const std::string out = "/tmp/live_real_v3.ts";
        std::filesystem::remove(out);
        parrot_to_klv::LiveIngestConfig cfg;
        cfg.rtsp_uri = pipeline_desc_for(enc);
        cfg.udp_sink = "file:" + out;
        // Deterministic injection — no async thread. live_ingest populates the
        // queue after its entry clear and drains immediately, so no 600 ms poll.
        cfg.test_injected_extensions.push_back(
            make_cap(std::span<const uint8_t>(s0->data.data(), s0->data.size()), 1'000'000'000ULL));
        cfg.test_injected_extensions.push_back(
            make_cap(std::span<const uint8_t>(s1->data.data(), s1->data.size()), 2'000'000'000ULL));
        auto r = parrot_to_klv::live_ingest(cfg);
        if (!r) {
            std::cerr << "  v3 live_ingest failed: " << static_cast<int>(r.error()) << "\n";
            return false;
        }
        size_t count = 0;
        std::vector<int64_t> pts;
        for (misbklv::Message& m : misbklv::KlvStream(out)) {
            ++count;
            pts.push_back(m.pts());
        }
        std::filesystem::remove(out);
        if (count != 2) {
            std::cerr << "  v3 expected 2 KLV packets, got " << count << "\n";
            return false;
        }
        // PTS should be the injected running_time (1s, 2s), not dummy spacing.
        if (pts[0] < 900'000'000LL || pts[0] > 1'100'000'000LL) {
            std::cerr << "  v3 pts[0] unexpected: " << pts[0] << "\n";
            return false;
        }
        if (pts[1] < 1'900'000'000LL || pts[1] > 2'100'000'000LL) {
            std::cerr << "  v3 pts[1] unexpected: " << pts[1] << "\n";
            return false;
        }
        std::cout << "  v3 real decode ok (" << count << " packets, pts " << pts[0] << ", " << pts[1] << ")\n";
    }

    // --- proto real decode ---
    {
        auto extractor = parrot_to_klv::Mp4MetadataExtractor::open(fixture_dir + "/parrot_proto.mp4");
        if (!extractor) {
            std::cerr << "  cannot open parrot_proto.mp4\n";
            return false;
        }
        auto s0 = extractor->extract_sample(0);
        if (!s0) {
            std::cerr << "  cannot extract proto sample\n";
            return false;
        }
        auto f0 = parrot_to_klv::parse_proto(std::span<const uint8_t>(s0->data.data(), s0->data.size()));
        if (!f0) {
            std::cerr << "  proto sample does not parse\n";
            return false;
        }
        auto snap = parrot_to_klv::from_proto(*f0);
        auto klv = parrot_to_klv::to_0601(snap, nullptr);
        if (klv.packet.empty()) {
            std::cerr << "  proto to_0601 empty\n";
            return false;
        }

        const std::string out = "/tmp/live_real_proto.ts";
        std::filesystem::remove(out);
        parrot_to_klv::LiveIngestConfig cfg;
        cfg.rtsp_uri = pipeline_desc_for(enc);
        cfg.udp_sink = "file:" + out;
        cfg.test_injected_extensions.push_back(
            make_cap(std::span<const uint8_t>(s0->data.data(), s0->data.size()), 3'000'000'000ULL));
        auto r = parrot_to_klv::live_ingest(cfg);
        if (!r) {
            std::cerr << "  proto live_ingest failed: " << static_cast<int>(r.error()) << "\n";
            return false;
        }
        size_t count = 0;
        int64_t pts0 = 0;
        for (misbklv::Message& m : misbklv::KlvStream(out)) {
            if (count == 0) pts0 = m.pts();
            ++count;
        }
        std::filesystem::remove(out);
        if (count != 1) {
            std::cerr << "  proto expected 1 KLV packet, got " << count << "\n";
            return false;
        }
        if (pts0 < 2'900'000'000LL || pts0 > 3'100'000'000LL) {
            std::cerr << "  proto pts unexpected: " << pts0 << "\n";
            return false;
        }
        std::cout << "  proto real decode ok (pts " << pts0 << ")\n";
    }

    // --- proto split coalesce ---
    {
        auto extractor = parrot_to_klv::Mp4MetadataExtractor::open(fixture_dir + "/parrot_proto.mp4");
        if (!extractor || extractor->sample_count() == 0) {
            std::cerr << "  cannot open proto for split test\n";
            return false;
        }
        auto s0 = extractor->extract_sample(0);
        if (!s0 || s0->data.size() < 10) {
            std::cerr << "  proto sample too small for split\n";
            return false;
        }
        size_t half = s0->data.size() / 2;
        std::vector<uint8_t> part1(s0->data.begin(), s0->data.begin() + half);
        std::vector<uint8_t> part2(s0->data.begin() + half, s0->data.end());

        const std::string out = "/tmp/live_real_proto_split.ts";
        std::filesystem::remove(out);
        parrot_to_klv::LiveIngestConfig cfg;
        cfg.rtsp_uri = pipeline_desc_for(enc);
        cfg.udp_sink = "file:" + out;
        cfg.test_injected_extensions.push_back(
            make_cap(std::span<const uint8_t>(part1.data(), part1.size()), 4'000'000'000ULL));
        cfg.test_injected_extensions.push_back(
            make_cap(std::span<const uint8_t>(part2.data(), part2.size()), 4'000'000'000ULL + 1000000));
        auto r = parrot_to_klv::live_ingest(cfg);
        if (!r) {
            std::cerr << "  proto split live_ingest failed: " << static_cast<int>(r.error()) << "\n";
            return false;
        }
        size_t count = 0;
        int64_t pts0 = 0;
        for (misbklv::Message& m : misbklv::KlvStream(out)) {
            if (count == 0) pts0 = m.pts();
            ++count;
        }
        std::filesystem::remove(out);
        if (count != 1) {
            std::cerr << "  proto split expected 1 coalesced packet, got " << count << " (coalesce failed)\n";
            return false;
        }
        // PTS should be from first chunk.
        if (pts0 < 3'900'000'000LL || pts0 > 4'100'000'000LL) {
            std::cerr << "  proto split pts unexpected: " << pts0 << "\n";
            return false;
        }
        std::cout << "  proto split coalesce ok (pts " << pts0 << ")\n";
    }

    parrot_to_klv::parrot_extension_clear();
    std::cout << "  ok\n";
    return true;
}

static bool test_continuous_live() {
    std::cout << "test_continuous_live\n";
    parrot_to_klv::parrot_extension_clear();
    std::string enc = pick_encoder();
    if (enc.empty()) {
        std::cout << "  SKIP: no encoder available\n";
        return true;
    }
    const std::string out = "/tmp/live_continuous.ts";
    std::filesystem::remove(out);
    parrot_to_klv::LiveIngestConfig cfg;
    // Unbounded live pipeline (no num-buffers) — must run until run_duration
    cfg.rtsp_uri = "pipeline:videotestsrc is-live=true ! videoconvert ! video/x-raw,width=320,height=240,framerate=30/1 ! " + enc + " ! h264parse";
    cfg.udp_sink = "file:" + out;
    cfg.run_duration = std::chrono::milliseconds(2000);
    auto t0 = std::chrono::steady_clock::now();
    auto r = parrot_to_klv::live_ingest(cfg);
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::steady_clock::now() - t0)
                       .count();
    if (!r) {
        std::cerr << "  continuous live_ingest failed: " << static_cast<int>(r.error()) << "\n";
        return false;
    }
    if (elapsed < 1800 || elapsed > 4000) {
        std::cerr << "  continuous elapsed unexpected: " << elapsed << " ms (want ~2000)\n";
        std::filesystem::remove(out);
        return false;
    }
    if (!std::filesystem::exists(out)) {
        std::cerr << "  continuous output not created\n";
        return false;
    }
    auto sz = std::filesystem::file_size(out);
    if (sz < 1000) {
        std::cerr << "  continuous output too small: " << sz << "\n";
        std::filesystem::remove(out);
        return false;
    }
    size_t count = 0;
    for (auto& m : misbklv::KlvStream(out)) { (void)m; ++count; }
    if (count == 0) {
        std::cerr << "  continuous expected at least 1 KLV packet, got 0\n";
        std::filesystem::remove(out);
        return false;
    }
    // Unbounded pipeline with no injected telemetry should have produced dummy.
    // With Generate live, dummy PTS at 3s may be culled if run_duration is 2s
    // (mux paces by PTS vs running_time), so accept 2 or 3.
    if (count < 2 || count > 3) {
        std::cerr << "  continuous dummy count unexpected: " << count << " (want 2..3)\n";
        std::filesystem::remove(out);
        return false;
    }
    std::cout << "  ok (" << sz << " bytes, " << count << " packets, " << elapsed << " ms)\n";
    std::filesystem::remove(out);

    // Late-arrival real decode during continuous window: verify the loop drains
    // extensions that arrive after the sink is already running.
    {
        std::filesystem::remove(out);
        parrot_to_klv::parrot_extension_clear();
        auto extractor = parrot_to_klv::Mp4MetadataExtractor::open(g_fixture_dir + "/parrot_v3.mp4");
        if (!extractor) {
            std::cerr << "  cannot open v3 fixture for late-arrival test\n";
            return false;
        }
        auto s0 = extractor->extract_sample(0);
        if (!s0) {
            std::cerr << "  cannot extract sample for late-arrival\n";
            return false;
        }
        parrot_to_klv::LiveIngestConfig cfg2;
        cfg2.rtsp_uri = "pipeline:videotestsrc is-live=true ! videoconvert ! video/x-raw,width=320,height=240,framerate=30/1 ! " + enc + " ! h264parse";
        cfg2.udp_sink = "file:" + out;
        cfg2.run_duration = std::chrono::milliseconds(2000);
        // Run live_ingest in background, push after 400ms
        std::thread bg([&cfg2]() { (void)parrot_to_klv::live_ingest(cfg2); });
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
        // Push real v3 payload with PTS
        std::vector<uint8_t> payload_u8(s0->data.begin(), s0->data.end());
        parrot_to_klv::parrot_extension_push_for_test(
            std::span<const uint8_t>(payload_u8.data(), payload_u8.size()), 1'000'000'000ULL);
        bg.join();
        size_t cnt = 0;
        std::vector<int64_t> pts;
        for (misbklv::Message& m : misbklv::KlvStream(out)) {
            ++cnt;
            pts.push_back(m.pts());
        }
        std::filesystem::remove(out);
        if (cnt < 3) {
            std::cerr << "  late-arrival expected at least 3 packets (2..3 dummy + 1 real), got " << cnt << "\n";
            return false;
        }
        bool has_real_pts = false;
        for (auto p : pts) if (p >= 900'000'000LL && p <= 1'100'000'000LL) has_real_pts = true;
        if (!has_real_pts) {
            std::cerr << "  late-arrival missing real PTS near 1s\n";
            return false;
        }
        std::cout << "  late-arrival ok (" << cnt << " packets)\n";
    }
    parrot_to_klv::parrot_extension_clear();
    return true;
}

int main(int argc, char* argv[]) {
    // Save fixture dir before gst_init strips arguments.
    g_fixture_dir = resolve_fixture_dir(argc, argv);
    gst_init(&argc, &argv);
    bool ok = true;
    ok &= test_extension_registered();
    ok &= test_read_captures_bytes();
    ok &= test_fifo_order();
    ok &= test_live_ingest_api();
    ok &= test_pipeline_hermetic();
    ok &= test_rtsp_unreachable();
    ok &= test_real_decode();
    ok &= test_continuous_live();
    if (ok) {
        std::cout << "live_rtp_test: all ok\n";
        return 0;
    }
    std::cerr << "live_rtp_test: failed\n";
    return 1;
}
