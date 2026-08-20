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

inline constexpr const char* kParrotRtpHeaderExtensionUri =
    "urn:parrot:rtp:header-extension";

bool register_parrot_extension();

// Test helpers — capture of header-extension payloads.
// FIFO, bounded — drop oldest when full (see kMaxCaptured). The real path
// will decode these bytes via parrot_v3 / parrot_proto, but for the stub we
// just store raw bytes to prove the read path is wired.
std::optional<std::vector<std::byte>> parrot_extension_pop_next();
void parrot_extension_clear();
size_t parrot_extension_count();

}  // namespace parrot_to_klv
