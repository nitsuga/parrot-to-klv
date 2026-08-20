// Copyright 2026 Eric Schneider
// Licensed under the Apache License, Version 2.0
#pragma once

#include <gst/gst.h>
#include <gst/rtp/gstrtphdrext.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

G_BEGIN_DECLS

#define PARROT_TYPE_RTP_HEADER_EXTENSION (parrot_rtp_header_extension_get_type())
G_DECLARE_FINAL_TYPE(ParrotRtpHeaderExtension, parrot_rtp_header_extension, PARROT,
                     RTP_HEADER_EXTENSION, GstRTPHeaderExtension)

G_END_DECLS

namespace parrot_to_klv {

inline constexpr const char* kParrotRtpHeaderExtensionUri =
    "urn:parrot:rtp:header-extension";

bool register_parrot_extension();

struct CapturedExtension {
    std::vector<std::byte> payload;
    // PTS from GstBuffer in nanoseconds; GST_CLOCK_TIME_NONE means none.
    uint64_t pts_ns = GST_CLOCK_TIME_NONE;
};

// FIFO, bounded — drop oldest when full (see kMaxCaptured). Stores payload
// plus the RTP buffer PTS (running_time) so live KLV PTS can be anchored to
// reception time per ADR 0013.
std::optional<CapturedExtension> parrot_extension_pop_next();
void parrot_extension_clear();
size_t parrot_extension_count();

// Test helper — push captured bytes without GStreamer. Requires a payload span
// and optional PTS (ns). Used to simulate header-extension arrival in tests.
void parrot_extension_push_for_test(std::span<const uint8_t> payload, uint64_t pts_ns = GST_CLOCK_TIME_NONE);
void parrot_extension_push_for_test(std::span<const std::byte> payload, uint64_t pts_ns = GST_CLOCK_TIME_NONE);

}  // namespace parrot_to_klv
