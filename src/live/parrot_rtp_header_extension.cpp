// Copyright 2026 Eric Schneider
// Licensed under the Apache License, Version 2.0

#include "live/parrot_rtp_header_extension.hpp"

#include <deque>
#include <mutex>
#include <vector>

namespace parrot_to_klv {
namespace {

// FIFO, bounded — drop oldest when full.
constexpr size_t kMaxCaptured = 128;
std::mutex g_mu;
std::deque<CapturedExtension> g_captured;

}  // namespace

std::optional<CapturedExtension> parrot_extension_pop_next() {
    std::lock_guard<std::mutex> lk(g_mu);
    if (g_captured.empty()) return std::nullopt;
    auto v = std::move(g_captured.front());
    g_captured.pop_front();
    return v;
}

void parrot_extension_clear() {
    std::lock_guard<std::mutex> lk(g_mu);
    g_captured.clear();
}

size_t parrot_extension_count() {
    std::lock_guard<std::mutex> lk(g_mu);
    return g_captured.size();
}

void parrot_extension_push_for_test(std::span<const uint8_t> payload, uint64_t pts_ns) {
    CapturedExtension cap;
    cap.payload.reserve(payload.size());
    for (auto b : payload) cap.payload.push_back(static_cast<std::byte>(b));
    cap.pts_ns = pts_ns;
    std::lock_guard<std::mutex> lk(g_mu);
    if (g_captured.size() >= kMaxCaptured) {
        g_captured.pop_front();
    }
    g_captured.push_back(std::move(cap));
}

void parrot_extension_push_for_test(std::span<const std::byte> payload, uint64_t pts_ns) {
    CapturedExtension cap;
    cap.payload = std::vector<std::byte>(payload.begin(), payload.end());
    cap.pts_ns = pts_ns;
    std::lock_guard<std::mutex> lk(g_mu);
    if (g_captured.size() >= kMaxCaptured) {
        g_captured.pop_front();
    }
    g_captured.push_back(std::move(cap));
}

}  // namespace parrot_to_klv

// ---- GObject type definition ----

struct _ParrotRtpHeaderExtension {
    GstRTPHeaderExtension parent;
};

G_DEFINE_TYPE(ParrotRtpHeaderExtension, parrot_rtp_header_extension,
              GST_TYPE_RTP_HEADER_EXTENSION)

static GstRTPHeaderExtensionFlags parrot_get_supported_flags(
    GstRTPHeaderExtension* /*ext*/) {
    // Parrot uses RFC 3550 generic header extension (defined-by-profile),
    // but GStreamer models everything as one-byte / two-byte RFC 5285.
    // Advertise both so the depayloader will deliver the extension bytes
    // regardless of the negotiated form — the stub just captures whatever
    // arrives.
    return static_cast<GstRTPHeaderExtensionFlags>(
        GST_RTP_HEADER_EXTENSION_ONE_BYTE | GST_RTP_HEADER_EXTENSION_TWO_BYTE);
}

static gsize parrot_get_max_size(GstRTPHeaderExtension* /*ext*/,
                                 const GstBuffer* /*input_meta*/) {
    return 8192;
}

static gssize parrot_write(GstRTPHeaderExtension* /*ext*/,
                           const GstBuffer* /*input_meta*/,
                           GstRTPHeaderExtensionFlags /*write_flags*/,
                           GstBuffer* /*output*/, guint8* /*data*/,
                           gsize /*size*/) {
    // Sending is not needed for ingest; report unsupported.
    return -1;
}

static gboolean parrot_read(GstRTPHeaderExtension* /*ext*/,
                            GstRTPHeaderExtensionFlags /*read_flags*/,
                            const guint8* data, gsize size,
                            GstBuffer* buffer) {
    parrot_to_klv::CapturedExtension cap;
    cap.payload.reserve(size);
    for (gsize i = 0; i < size; ++i) {
        cap.payload.push_back(static_cast<std::byte>(data[i]));
    }
    // Assumption: RTP buffer PTS at header-extension read equals reception
    // running_time per ADR 0013/ADR 0021; if rtspsrc buffering or
    // RTP-timestamp mode diverges, live KLV will drift from video. Hermetic
    // test injects PTS directly so cannot validate — validate against real/mock
    // RTSP source in follow-up.
    if (buffer != nullptr) {
        GstClockTime pts = GST_BUFFER_PTS(buffer);
        cap.pts_ns = pts;
    } else {
        cap.pts_ns = GST_CLOCK_TIME_NONE;
    }
    {
        std::lock_guard<std::mutex> lk(parrot_to_klv::g_mu);
        // Bounded FIFO — drop oldest when full.
        if (parrot_to_klv::g_captured.size() >= parrot_to_klv::kMaxCaptured) {
            parrot_to_klv::g_captured.pop_front();
        }
        parrot_to_klv::g_captured.push_back(std::move(cap));
    }
    return TRUE;
}

static void parrot_rtp_header_extension_class_init(
    ParrotRtpHeaderExtensionClass* klass) {
    auto* ext_class = GST_RTP_HEADER_EXTENSION_CLASS(klass);
    ext_class->get_supported_flags = parrot_get_supported_flags;
    ext_class->get_max_size = parrot_get_max_size;
    ext_class->write = parrot_write;
    ext_class->read = parrot_read;

    gst_rtp_header_extension_class_set_uri(
        ext_class, parrot_to_klv::kParrotRtpHeaderExtensionUri);

    gst_element_class_set_details_simple(GST_ELEMENT_CLASS(klass),
                                         "Parrot RTP header extension",
                                         GST_RTP_HDREXT_ELEMENT_CLASS,
                                         "Captures Parrot P1/P2/P3/Pb RTP header extensions",
                                         "Eric Schneider <eric@nitsuga.org>");
    gst_element_class_set_metadata(GST_ELEMENT_CLASS(klass),
                                   "Parrot RTP header extension",
                                   GST_RTP_HDREXT_ELEMENT_CLASS,
                                   "Captures Parrot P1/P2/P3/Pb RTP header extensions",
                                   "Eric Schneider <eric@nitsuga.org>");
}

static void parrot_rtp_header_extension_init(ParrotRtpHeaderExtension* /*self*/) {}

namespace parrot_to_klv {

bool register_parrot_extension() {
    static bool registered = false;
    if (registered) return true;

    if (!gst_is_initialized()) {
        gst_init(nullptr, nullptr);
    }

    // Ensure the type is registered.
    (void)PARROT_TYPE_RTP_HEADER_EXTENSION;

    gboolean ok = gst_element_register(
        nullptr, "parrot-rtp-header-extension", GST_RANK_MARGINAL,
        PARROT_TYPE_RTP_HEADER_EXTENSION);
    if (ok) registered = true;
    return ok != FALSE;
}

}  // namespace parrot_to_klv
