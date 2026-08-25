---
type: Reference
title: Parrot Video Metadata Documentation
description: Official Parrot developer documentation for embedded video metadata formats
tags: [parrot, documentation, reference]
generated:
  by: claude/opus-5
  at: 2026-08-24T18:00:00Z
sources:
  - id: parrot-vmeta-doc-link
    resource: "../references/parrot-video-metadata.md"
    title: >-
      Link file for Parrot's embedded-metadata documentation — URL, version and
      date observed, terms, and the pinned replacements (the snapshot was
      withdrawn per ADR 0016)
  - id: parrot-vmeta-proto
    resource: "../references/vmeta.proto"
    title: Parrot's vmeta.proto, vendored from libvideo-metadata under BSD-3-Clause
  - id: libvideo-metadata-pin
    resource: https://github.com/Parrot-Developers/libvideo-metadata/tree/22f715adbb52ad14693b3b98fa537f3dbef53409
    title: Parrot-Developers/libvideo-metadata at 22f715a — BSD-3-Clause reference implementation
---

# Parrot Video Metadata Documentation

Official documentation from Parrot Developers describing the video metadata
formats embedded in drone recordings and streams.

**Source:** <https://developer.parrot.com/docs/groundsdk-tools/video-metadata.html>,
version 8.4.2 as observed 2026-08-24.

> **The local snapshot was withdrawn.** This documentation is copyrighted by
> Parrot with no redistribution grant, so the verbatim HTML export that used to
> back this concept was removed from `../references/` before this repository
> could be made public — see
> [[0016-references-licensing-withdrawal]]. What remains is
> `../references/parrot-video-metadata.md` (the link file: URL, version and date
> observed, terms) and `../references/vmeta.proto` (the schema itself, vendored
> under BSD-3-Clause).
>
> Consequences for reading this concept: the **schema and the binary struct
> layouts** are still locally checkable, against `../references/vmeta.proto` and
> the pinned `vmeta_frame_v3.h` / `vmeta_frame_v2.h` in [[libvideo-metadata]].
> The **prose** below — fixed-point encodings, field semantics, the extension
> narrative — is this project's reading of a page that is no longer held
> locally, and re-checking it means fetching the live URL at whatever version it
> then serves. The URL is unversioned, so that may not be 8.4.2.

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

Cross-reference with [[libvideo-metadata]] source code for implementation
details. Since the withdrawal that source is the **primary** one: prefer a
commit-pinned citation into `libvideo-metadata` over a section reference to this
page wherever both would serve.
