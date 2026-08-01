---
type: DataFormat
title: Parrot Video Metadata Formats
description: Binary format specifications for Parrot drone telemetry in MP4 files (v1, v2, v3, proto), with exact v2/v3 byte layouts and the enum-numbering traps
tags: [parrot, metadata, telemetry, format, mp4]
generated:
  by: claude/opus-5
  at: 2026-07-25T23:30:00Z
---

# Parrot Video Metadata Formats

Parrot drones embed telemetry metadata in MP4 video files as timed metadata tracks.
Four format versions exist: v1 (deprecated), v2, v3, and proto (protobuf-based).

**Sources:**
- [[parrot-video-metadata-docs]] — HTML documentation
- [[libvideo-metadata]] — Reference implementation (C library)

## Format Overview

| Format | Drone Models | MIME Type | Identifier |
|--------|-------------|-----------|------------|
| v1 | Bebop/Bebop 2 (< FW 4.0) | `com.parrot.videometadata1` | 0x5031 (`P1`) |
| v2 | Bebop 2 (≥ FW 4.0), Disco, Bebop-Pro Thermal, Bluegrass | `com.parrot.videometadata2` | 0x5032 (`P2`) |
| v3 | ANAFI, ANAFI Thermal, ANAFI USA | `com.parrot.videometadata3` | 0x5033 (`P3`) |
| proto | ANAFI Ai, ANAFI UKR | `com.parrot.videometadataproto` | 0x5062 (`Pb`) |

## Integration in MP4

Metadata stored in dedicated timed metadata track with:
- **Track header** (`tkhd`): volume=0, width=0, height=0
- **Track reference** (`tref`): type=`cdsc`, points to video track
- **Handler** (`hdlr`): type=`meta`
- **Null media header** (`nmhd`)
- **Sample description** (`stsd`): TextMetaDataSampleEntry with MIME type

Each sample in the metadata track contains one frame's telemetry, synchronized
with video frames via capture timestamps (embedded in metadata + H.264/H.265 SEI).

## Version 1 (v1) — Deprecated

**Used:** Bebop/Bebop 2 firmware 3.2–3.9 (recording), 3.3–3.9 (streaming)

Fixed-point encoding (e.g., Q4.12 = 16 bits, 4 int + 12 frac). Network byte order.

### Recording Format (60 bytes)
```c
struct metadata_v1_recording {
    uint64_t frame_timestamp;     // µs, monotonic (split hi/lo 32 bits)
    int16_t  drone_yaw;            // rad, Q4.12
    int16_t  drone_pitch;          // rad, Q4.12
    int16_t  drone_roll;           // rad, Q4.12
    int16_t  camera_pan;           // rad, Q4.12
    int16_t  camera_tilt;          // rad, Q4.12
    int16_t  frame_quat[4];        // W, X, Y, Z, Q4.12
    int16_t  exposure_time;        // ms, Q8.8
    int16_t  gain;                 // ISO gain
    int8_t   wifi_rssi;            // dBm
    uint8_t  battery_percentage;   // %
    int32_t  gps_latitude;         // deg, Q12.20
    int32_t  gps_longitude;        // deg, Q12.20
    int32_t  gps_altitude_and_sv;  // bits 31..8: altitude (m, Q16.8), bits 7..0: SV count
    int32_t  altitude;             // m, Q16.16, relative to takeoff
    uint32_t distance_from_home;   // m, Q16.16
    int16_t  x_speed, y_speed, z_speed; // m/s, Q8.8
    uint8_t  state;                // bit 7: binning, bits 6..0: flying_state
    uint8_t  mode;                 // bit 7: animation, bits 6..0: piloting_mode
};
```

### Streaming Formats
- **Basic** (28 bytes, 0x5031, length=6): attitude, camera, exposure, wifi, battery
- **Extended** (56 bytes, 0x5031, length=13): basic + GPS, altitude, speed, state

## Version 2 & 3 (v2, v3)

**v2 used:** Bebop 2 ≥ FW 4.0, Disco ≥ 1.1, Bebop-Pro Thermal, Bluegrass  
**v3 used:** ANAFI range (ANAFI, ANAFI Thermal, ANAFI USA)

Extensible format: base structure + optional extensions.

### Base Structure
Identifier: 0x5032 (v2) or 0x5033 (v3), followed by `length` (32-bit words,
excludes `id`/`length` fields).

**v2 and v3 are not layout-compatible** — they need separate decoders. They share
a prefix through `drone_z` (offset 34), then diverge: v3 inserts a *frame base*
quaternion, replaces v2's explicit `camera_pan`/`camera_tilt` with the difference
between the base and frame quaternions, adds AWB gains, FOV, and link quality,
and reorders the trailing bytes.

**v3 base — 72 bytes** (`length` = 17 words with no extensions):

| Off | Type | Field | Encoding |
|-----|------|-------|----------|
| 0 | uint16 | `id` | 0x5033 |
| 2 | uint16 | `length` | 32-bit words after this pair, extensions included |
| 4 | int32 | `ground_distance` | Q16.16, m |
| 8 | int32 | `latitude` | Q10.22, deg |
| 12 | int32 | `longitude` | Q10.22, deg |
| 16 | int32 | `altitude_and_sv` | bits 31..8 = altitude AMSL Q16.8; bits 7..0 = SV count |
| 20 | int16 ×3 | `north/east/down_speed` | Q8.8, m/s |
| 26 | int16 | `air_speed` | Q8.8, m/s — **negative means no data** |
| 28 | int16 ×4 | `drone_*` quaternion | Q2.14, W X Y Z |
| 36 | int16 ×4 | `frame_base_*` quaternion | Q2.14, view without user pan/tilt |
| 44 | int16 ×4 | `frame_*` quaternion | Q2.14 |
| 52 | uint16 | `exposure_time` | Q8.8, ms |
| 54 | uint16 | `gain` | ISO gain, direct |
| 56 | uint16 ×2 | `awb_r_gain`, `awb_b_gain` | Q2.14 |
| 60 | uint16 ×2 | `picture_hfov`, `picture_vfov` | Q8.8, **degrees** |
| 64 | uint32 | `link_quality` | bits 31..8 = goodput kbit/s; bits 7..0 = quality 0–5 |
| 68 | int8 | `wifi_rssi` | dBm |
| 69 | uint8 | `battery_percentage` | % |
| 70 | uint8 | `state` | bit 7 = binning; bits 6..0 = flying state |
| 71 | uint8 | `mode` | bit 7 = animation; bits 6..0 = piloting mode |

**v2 base — 56 bytes** (`length` = 13). Same through offset 34, then: `frame_*`
quaternion (36), `camera_pan`/`camera_tilt` Q4.12 (44, 46), `exposure_time`,
`gain` (48, 50), then `state`, `mode`, `wifi_rssi`, `battery_percentage`
(52–55) — note the trailing four are ordered differently from v3.

**Enum values differ between the binary formats and protobuf.** Binary
`piloting_mode` is `MANUAL=0, RETURN_HOME=1, FLIGHT_PLAN=2, TRACKING=3,
MAGIC_CARPET=4, MOVE_TO=5`; protobuf `PilotingMode` prepends `PM_UNKNOWN=0` and
swaps the last two. Binary `flying_state` for v3 runs 0–8 and *does* match
protobuf's `FS_*`. v1/v2 `flying_state` stops at 5 and v2's `piloting_mode`
stops at 3 (`FOLLOW_ME`). Never map one numbering onto the other.

### Extension Mechanism

Extensions follow the base structure, each with:
```c
uint16_t ext_id;      // Extension identifier
uint16_t ext_length;  // Size in 32-bit words (excludes ext_id/ext_length)
// ... extension data (4-byte aligned) ...
```

**Standard extensions** — these four, and no others:
- **0x4531 (`E1`)**: Timestamp — frame capture timestamp (µs, monotonic), two
  32-bit halves. Should always be present in *recorded* metadata.
- **0x4532 (`E2`)**: Follow-me (v2 only) — target location, mode, animation
- **0x4533 (`E3`)**: Automation (v3 only) — framing target lat/lon/alt, flight
  destination lat/lon/alt, animation, follow-me / look-at-me / angle-locked flags
- **0x4534 (`E4`)**: Thermal — min/max/probe temperature spots (x, y, K in
  Q11.5), calibration state, per-spot validity flags

**0x4536 extension status unclear.** An earlier revision of this doc listed
`0x4536 (E6)` as an LFIC (Location From Image Coordinates) extension, but that
identifier appears nowhere in the reference documentation. However, real Parrot
ANAFI v3 recordings contain a 0x4536 extension in every sample, suggesting the
reference may be incomplete rather than the encoder being wrong. The extension's
content and structure remain undocumented. LFIC does exist in the protobuf
schema as `LFICMetadata`, but whether 0x4536 represents a binary encoding of it
is unknown.

Extensions are optional, order-independent, and 4-byte aligned (so every
`ext_length` is a whole number of 32-bit words). Only one of each type may
appear. Parsers must skip unknown extensions via `ext_length`.

## Protobuf Format (proto)

**Used:** ANAFI Ai, ANAFI UKR (≥ 2021)

Identifier: 0x5062 (`Pb`)

Google Protocol Buffers (proto3) for flexible, forward-compatible serialization.
Schema defined in `vmeta.proto` (libvideo-metadata).

**Key message types:**
- `TimedMetadata` — per-frame telemetry (replaces v1/v2/v3 structs)
- Nested messages for location, quaternion, speed, camera, thermal, tracking, etc.

**Advantages:**
- Forward/backward compatibility (new fields don't break old parsers)
- Smaller wire format (variable-length encoding)
- Rich type system (nested messages, enums, repeated fields)

**Fields** (subset, see vmeta.proto for complete schema):
- Drone: location, attitude (quat), speed (NED), flying state
- Camera: location, orientation, FOV, exposure, gain, zoom
- Frame: timestamp (monotonic + UTC), quaternion
- Link: WiFi RSSI, quality, goodput
- Battery percentage
- Tracking: bounding box, class, state
- Thermal: spots, calibration state
- Automation: animation type, target locations
- LFIC: multiple targets with types

## Field Categories

All formats provide (to varying degrees):

### Flight Telemetry
- **Location**: GPS lat/lon/alt (AMSL/EGM96), SV count
- **Attitude**: Euler angles (yaw/pitch/roll) or quaternion
- **Speed**: NED (North-East-Down) vector, air speed
- **Altitude**: relative to takeoff, ground distance
- **State**: flying state (landed, takeoff, hover, flying, landing, emergency)
- **Mode**: piloting mode (manual, return home, flight plan, tracking, move-to)

### Camera/Image
- **Orientation**: frame quaternion (global + local), base quaternion
- **Gimbal**: pan/tilt angles
- **Exposure**: time (ms), ISO gain, AWB gains
- **FOV**: horizontal/vertical field of view (degrees)
- **Principal point**: normalized (x, y) in [0..1]
- **Zoom**: level (proto only)

### System/Link
- **WiFi**: RSSI (dBm), link quality (0–5), goodput (proto)
- **Battery**: charge percentage
- **Timestamps**: frame capture (monotonic µs), UTC timestamp

### Advanced (v3/proto)
- **Thermal**: calibration state, min/max/probe temperature spots
- **Tracking**: bounding box, target class, state
- **Automation**: animations (orbit, boomerang, dolly, reveal, etc.), target locations
- **LFIC**: location from image coordinates (cursor-on-target, user targets)

## Encoding Notes

### Fixed-Point (v1/v2/v3)
- **Q notation**: `Q<int>.<frac>` = <int> + <frac> bits
- Examples:
  - Q4.12 (16-bit): 4 int bits, 12 frac → range [-8, +8), resolution 1/4096
  - Q8.8 (16-bit): 8 int bits, 8 frac → range [-128, +128), resolution 1/256
  - Q16.16 (32-bit): 16 int bits, 16 frac → range [-32768, +32768), resolution 1/65536
  - Q12.20 (32-bit): 12 int bits, 20 frac → range [-2048, +2048), resolution ~1e-6
- **Network byte order** (big-endian)

### Coordinate Systems
- **NED**: North-East-Down (Z down is positive)
- **GPS altitude**: AMSL/EGM96 (Above Mean Sea Level, EGM96 geoid)
- **Quaternions**: W, X, Y, Z (scalar-first)
- **Euler angles**: Yaw (ψ), Pitch (θ), Roll (φ) in radians

## Usage Notes

**For parsing:**
1. Read MP4 metadata track MIME type from `stsd` to identify format
2. For v1: detect recording (60 bytes) vs. streaming (28/56 bytes) by size
3. For v2/v3: read base structure, then iterate extensions via `ext_id`/`ext_length`
4. For proto: deserialize protobuf using `vmeta.proto` schema
5. Check field validity flags (e.g., GPS location `valid` field)

**libvideo-metadata** provides `vmeta_frame_read()` to auto-detect and parse all
formats, returning a unified `vmeta_frame` structure (or converting to proto internally).

**For conversion to MISB 0601:** Focus on v3 and proto formats (modern ANAFI drones).
v1/v2 support can be added later if needed for older Bebop/Disco recordings.

## References

- **Why:** This documents all four formats, but the project **supports v3 and
  proto** (see [ROADMAP](../planning/ROADMAP.md) § Scope). v2 is kept here
  because it is the deferred add-on and its layout is the part of this document
  most likely to be needed next; v1 is documented for completeness only.
- **How to apply:** Use libvideo-metadata headers and source as reference. The
  v3 and proto parsers exist; a v2 parser would normalize into the same
  `TelemetrySnapshot` per
  [ADR 0004](./decisions/0004-shared-telemetry-snapshot.md).
