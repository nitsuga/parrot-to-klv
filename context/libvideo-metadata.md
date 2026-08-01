---
type: Reference
title: libvideo-metadata
description: Parrot's C library for parsing and serializing video metadata (reference only, not built)
tags: [parrot, library, reference, c]
generated:
  by: claude/opus-5
  at: 2026-07-23T19:00:00Z
sources:
  - id: libvideo-metadata
    resource: https://github.com/Parrot-Developers/libvideo-metadata
    title: Parrot-Developers/libvideo-metadata (C reference implementation)
---

# libvideo-metadata

Parrot's official C library for reading and writing video metadata embedded in
drone recordings and streams. Used as a **reference** for this project, not as a
build dependency.

**Source:** https://github.com/Parrot-Developers/libvideo-metadata  
**Local clone:** `/home/schneiderer/workspaces/github/libvideo-metadata`

## Why Reference-Only?

The library uses Parrot's Alchemy build system (`atom.mk`), which is heavyweight
to integrate. Instead, we examine the source code to understand the binary formats
and implement lightweight parsers tailored to our needs.

## Key Files

### Headers (API)
- `include/video-metadata/vmeta_frame.h` — Frame metadata API (enums, structs, functions)
- `include/video-metadata/vmeta_frame_v1.h` — v1 format structures
- `include/video-metadata/vmeta_frame_v2.h` — v2 format structures
- `include/video-metadata/vmeta_frame_v3.h` — v3 format structures
- `include/video-metadata/vmeta_frame_proto.h` — proto format wrapper
- `include/video-metadata/vmeta.h` — Common types (location, quaternion, euler, etc.)
- `proto/vmeta.proto` — Protobuf schema for proto format

### Source (Implementation)
- `src/vmeta_frame_v1.c` — v1 parser/serializer
- `src/vmeta_frame_v2.c` — v2 parser/serializer  
- `src/vmeta_frame_v3.c` — v3 parser/serializer
- `src/vmeta_frame_proto.c` — proto parser/serializer

## Useful Patterns from Source

1. **Format detection** (MIME type or identifier-based)
   - v1: check size (60 bytes recording, 28/56 streaming)
   - v2/v3: read 16-bit identifier (0x5032/0x5033)
   - proto: MIME type = `com.parrot.videometadataproto`

2. **Fixed-point conversion**
   - Q4.12: `float val = (int16_t)raw / 4096.0f;`
   - Q8.8: `float val = (int16_t)raw / 256.0f;`
   - Q16.16: `double val = (int32_t)raw / 65536.0;`
   - Q12.20: `double val = (int32_t)raw / 1048576.0;`

3. **Extension parsing (v2/v3)**
   ```c
   uint16_t base_len = read_u16(buf);  // in 32-bit words
   uint8_t *end = buf + base_len * 4;
   while (buf < end) {
       uint16_t ext_id = read_u16(buf);
       uint16_t ext_len = read_u16(buf);
       // Parse extension based on ext_id
       buf += ext_len * 4;
   }
   ```

4. **Validity checks**
   - GPS location: check `sv_count > 0` or `valid` flag
   - Quaternion: check magnitude ≈ 1.0
   - Altitude: check for special "invalid" values

## API Overview (for reference)

**Unified structure:**
```c
struct vmeta_frame {
    enum vmeta_frame_type type;  // V1_RECORDING, V1_STREAMING_BASIC, V1_STREAMING_EXTENDED, V2, V3, PROTO
    union {
        struct vmeta_frame_v1_recording v1_rec;
        struct vmeta_frame_v1_streaming_basic v1_strm_basic;
        struct vmeta_frame_v1_streaming_extended v1_strm_ext;
        struct vmeta_frame_v2 v2;
        struct vmeta_frame_v3 v3;
        struct vmeta_frame_proto *proto;
    };
};
```

**Key functions:**
- `vmeta_frame_read()` — Auto-detect and parse from buffer
- `vmeta_frame_get_location()` — Extract GPS location
- `vmeta_frame_get_drone_quat()` — Extract attitude quaternion
- `vmeta_frame_get_speed_ned()` — Extract NED speed vector
- Many more getters for specific fields

## Usage for This Project

1. **Study the headers** to understand field layouts and data types
2. **Examine the source** for parsing logic (byte order, fixed-point conversion, extension iteration)
3. **Reference the proto schema** (`vmeta.proto`) for protobuf format
4. **Implement our own parsers** using these patterns, tailored to what we need
   for MISB 0601 conversion

Don't copy code directly (3-clause BSD license compatible, but prefer clean
implementation). Use it to understand the binary protocol.
