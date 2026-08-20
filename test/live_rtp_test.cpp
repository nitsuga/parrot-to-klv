// Copyright 2026 Eric Schneider
// Licensed under the Apache License, Version 2.0

#include "live/live_ingest.hpp"
#include "live/parrot_rtp_header_extension.hpp"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <iostream>

#include <gst/gst.h>
#include <gst/rtp/gstrtphdrext.h>

#include "misbklv/stream.hpp"

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
    if (popped->size() != sizeof(dummy)) {
        std::cerr << "  size mismatch: " << popped->size() << " vs " << sizeof(dummy) << "\n";
        gst_object_unref(ext);
        return false;
    }
    for (size_t i = 0; i < sizeof(dummy); ++i) {
        if ((*popped)[i] != static_cast<std::byte>(dummy[i])) {
            std::cerr << "  byte mismatch at " << i << "\n";
            gst_object_unref(ext);
            return false;
        }
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
    if (!gst_rtp_header_extension_read(ext, GST_RTP_HEADER_EXTENSION_ONE_BYTE, p1, sizeof(p1), buf)) {
        std::cerr << "  read p1 failed\n";
        gst_buffer_unref(buf);
        gst_object_unref(ext);
        return false;
    }
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
    if (first->size() != sizeof(p1) || second->size() != sizeof(p2)) {
        std::cerr << "  fifo size mismatch\n";
        gst_object_unref(ext);
        return false;
    }
    for (size_t i = 0; i < sizeof(p1); ++i) {
        if ((*first)[i] != static_cast<std::byte>(p1[i])) {
            std::cerr << "  fifo p1 byte mismatch at " << i << "\n";
            gst_object_unref(ext);
            return false;
        }
    }
    for (size_t i = 0; i < sizeof(p2); ++i) {
        if ((*second)[i] != static_cast<std::byte>(p2[i])) {
            std::cerr << "  fifo p2 byte mismatch at " << i << "\n";
            gst_object_unref(ext);
            return false;
        }
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

int main(int argc, char* argv[]) {
    gst_init(&argc, &argv);
    bool ok = true;
    ok &= test_extension_registered();
    ok &= test_read_captures_bytes();
    ok &= test_fifo_order();
    ok &= test_live_ingest_api();
    ok &= test_pipeline_hermetic();
    ok &= test_rtsp_unreachable();
    if (ok) {
        std::cout << "live_rtp_test: all ok\n";
        return 0;
    }
    std::cerr << "live_rtp_test: failed\n";
    return 1;
}
