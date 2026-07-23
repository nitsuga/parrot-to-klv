---
type: DataFormat
title: Parrot Video Metadata Formats
description: Binary format specifications for Parrot drone telemetry embedded in MP4 files (v1, v2, v3, proto)
tags: [parrot, metadata, telemetry, format, mp4]
timestamp: 2026-07-23T19:00:00Z
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

**Common fields** (both v2 and v3):
- Drone attitude (quaternion or Euler)
- Camera orientation (pan, tilt)
- Frame quaternion
- Location (GPS lat/lon/alt, SV count)
- Speed (NED)
- Altitude (relative to takeoff)
- Distance from home
- Exposure time, gain
- WiFi RSSI, battery %
- Flying state, piloting mode

**v3 additions:**
- More flying states (user takeoff, motor ramping, emergency landing)
- Automation animations (vs. v2 follow-me only)
- Thermal metadata support

### Extension Mechanism

Extensions follow the base structure, each with:
```c
uint16_t ext_id;      // Extension identifier
uint16_t ext_length;  // Size in 32-bit words (excludes ext_id/ext_length)
// ... extension data (4-byte aligned) ...
```

**Standard extensions:**
- **0x4531 (`E1`)**: Timestamp — frame capture timestamp (µs, monotonic)
- **0x4532 (`E2`)**: Follow-me (v2 only) — target location, mode, animation
- **0x4533 (`E3`)**: Automation (v3 only) — framing target, flight destination, animation
- **0x4534 (`E4`)**: Thermal — calibration state, min/max/probe temperature spots
- **0x4536 (`E6`)**: LFIC (Location From Image Coordinates) — target cursor position, location

Extensions are optional and order-independent. Parsers skip unknown extensions.

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

- **Why:** This project needs to parse all Parrot metadata formats to convert to
  MISB 0601 KLV for MPEG-TS output.
- **How to apply:** Use libvideo-metadata headers and source as reference to
  implement lightweight parsers for each format. Prioritize v3/proto for initial
  implementation.
