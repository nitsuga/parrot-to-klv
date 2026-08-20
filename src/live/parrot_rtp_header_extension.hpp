// Copyright 2026 Eric Schneider
// Licensed under the Apache License, Version 2.0
#pragma once

#include <gst/gst.h>
#include <gst/rtp/gstrtphdrext.h>

#include <cstddef>
#include <optional>
#include <vector>

G_BEGIN_DECLS

#define PARROT_TYPE_RTP_HEADER_EXTENSION (parrot_rtp_header_extension_get_type())
G_DECLARE_FINAL_TYPE(ParrotRtpHeaderExtension, parrot_rtp_header_extension, PARROT,
                     RTP_HEADER_EXTENSION, GstRTPHeaderExtension)

G_END_DECLS

namespace parrot_to_klv {

// URI that rtspsrc / SDP extmap will negotiate. Parrot's defined-by-profile
// values are 0x5031/0x5032/0x5033/0x5062 (ASCII "P1"/"P2"/"P3"/"Pb"). The
// extension itself is the same byte layout — we use one URI to capture all.
inline constexpr const char* kParrotRtpHeaderExtensionUri =
    "urn:parrot:rtp:header-extension";

// Register the extension's GStreamer element factory so
// gst_rtp_header_extension_create_from_uri(kParrotRtpHeaderExtensionUri)
// succeeds and rtspsrc can auto-plug it via extmap.
bool register_parrot_extension();

// Test helpers — capture of the last header-extension payload.
// The real path will decode these bytes via parrot_v3 / parrot_proto, but
// for the stub we just store raw bytes to prove the read path is wired.
std::optional<std::vector<std::byte>> parrot_extension_pop_last();
void parrot_extension_clear();
size_t parrot_extension_count();

}  // namespace parrot_to_klv
