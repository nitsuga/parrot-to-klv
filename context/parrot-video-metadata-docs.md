---
type: Reference
title: Parrot Video Metadata Documentation
description: Official Parrot developer documentation for embedded video metadata formats
tags: [parrot, documentation, reference]
generated:
  by: claude/opus-5
  at: 2026-07-23T19:00:00Z
sources:
  - id: parrot-vmeta-html
    resource: "../references/Embedded Video Metadata - 8.4.2.html"
    title: >-
      Official Parrot developer documentation, saved from
      https://developer.parrot.com/docs/groundsdk-tools/video-metadata.html
---

# Parrot Video Metadata Documentation

Official documentation from Parrot Developers describing the video metadata
formats embedded in drone recordings and streams.

**Source:** `../references/Embedded Video Metadata - 8.4.2.html` (saved from
https://developer.parrot.com/docs/groundsdk-tools/video-metadata.html)

## Content

The documentation covers:

1. **Frame Metadata (Timed)**
   - Binary format specifications for v1, v2, v3, and proto
   - Integration in MP4 files (timed metadata track)
   - Integration in RTP streams (header extensions)
   - Field definitions and encoding (fixed-point, network byte order)
   - Extension mechanism (v2/v3)
   - Protobuf schema overview (proto)

2. **Session Metadata (Untimed)**
   - Device information (model, serial, firmware version)
   - Flight context (media date, run ID, boot ID, flight ID)
   - Camera information (type, serial, FOV, model)
   - Integration methods (MP4 tags, RTCP SDES, SDP)

3. **Binary Format Annexes**
   - Complete C struct definitions for v1, v2, v3
   - Bit masks and shifts for packed fields
   - Enum definitions (flying state, piloting mode, animations)
   - Extension structures (timestamp, follow-me, automation, thermal). **Not**
     LFIC: the reference documents no LFIC extension, though real recordings
     carry an undocumented `0x4536` — see [[parrot-metadata-formats]]

## Key Sections

- **§ Frame Metadata**: Timed data synchronized with video frames (telemetry,
  camera, exposure, etc.)
- **§ Session Metadata**: Untimed metadata (device info, context)
- **§ Annex: Frame Metadata Binary Format**: Detailed struct definitions with
  comments and examples

## Usage for This Project

Read this documentation to understand:
- Which fields are available in each format version
- How metadata is embedded in MP4 files (track structure, MIME types)
- Binary encoding details (fixed-point precision, byte order)
- Coordinate systems (NED, EGM96 altitude datum)

Cross-reference with [[libvideo-metadata]] source code for implementation details.
