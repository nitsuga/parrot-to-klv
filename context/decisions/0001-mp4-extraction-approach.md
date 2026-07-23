---
type: Decision
title: ADR 0001 — Direct MP4 Parsing for Metadata Extraction
description: Use lightweight MP4 box parsing instead of GStreamer qtdemux for Parrot metadata extraction
status: accepted
tags: [decision, mp4, extraction, architecture]
timestamp: 2026-07-23T20:00:00Z
---

# Context

Parrot drone telemetry is embedded in MP4 files as a timed metadata track. We
need to extract these samples to convert them to MISB 0601 KLV for MPEG-TS output.

Two extraction approaches considered:

1. **GStreamer `qtdemux`** — leverage GStreamer's MP4 demuxer
2. **Direct MP4 parsing** — read boxes directly, extract metadata samples

# Decision

Use **direct MP4 parsing** with a lightweight library or minimal custom parser for
Phase 1.

# Status

Accepted (2026-07-23)

# Consequences

## Positive

- **Simple and debuggable** — clear, sequential code path for metadata extraction
- **No custom GStreamer plugin needed** — `qtdemux` doesn't expose metadata tracks
  as pads without customization
- **Clear separation of concerns**:
  - MP4 parsing → Parrot format parsing → MISB mapping (our code)
  - KLV encoding + MPEG-TS muxing (libmisbklv via GStreamer)
- **Easier testing** — can unit test Parrot parser independently
- **Smaller dependency surface** — GStreamer only needed for output (via libmisbklv),
  not input

## Negative

- **Need MP4 box parsing code** — either integrate a small library (e.g., minimp4)
  or write our own minimal reader
- **Sequential file reading** — not streaming-optimized initially (can add later if
  needed)
- **Video extraction complexity** — need to handle video track separately if remuxing
  (vs. unified GStreamer pipeline)

## Neutral

- Can revisit if requirements change (e.g., need streaming input, complex
  multi-track handling)
- libmisbklv already provides GStreamer backend for KLV → MPEG-TS, so GStreamer
  is a dependency regardless

# Alternatives Considered

## GStreamer qtdemux

**Rejected because:**
- `qtdemux` doesn't expose metadata tracks as output pads by default
- Would require custom GStreamer element/plugin to access metadata track
- Adds complexity without clear benefit for Phase 1 MVP
- Harder to debug Parrot parsing when embedded in GStreamer pipeline

**Could reconsider if:**
- Need streaming/incremental MP4 processing
- Need complex multi-track synchronization
- Want fully unified GStreamer pipeline (MP4 in → TS out)

# Implementation Notes

**Phase 1 approach:**

1. Use lightweight MP4 box reader:
   - Option A: `minimp4` (single-header, public domain, ~2K LOC)
   - Option B: Custom minimal reader (just `moov`/`trak`/`mdia`/`stbl` boxes)
   
2. Locate metadata track:
   - Find `trak` with `hdlr` type = `meta`
   - Read `stsd` to get MIME type (`com.parrot.videometadata{1,2,3,proto}`)
   - Extract samples via `stts`/`stsc`/`stco`/`stsz` tables

3. Parse Parrot binary format per [[parrot-metadata-formats]]

4. Convert to MISB 0601 per [[field-mapping-analysis]]

5. Use libmisbklv `Message` + `KlvSink` for KLV encoding + TS muxing

**Video handling TBD:**
- Extract video samples from MP4
- Pass to libmisbklv alongside KLV
- Or use GStreamer for video path: `qtdemux.video_0 ! h264parse ! mpegtsmux`
  (decision deferred to Phase 3)

# References

- Investigation showed `gst-discoverer-1.0` on sample file didn't list metadata
  track, only video + audio
- GStreamer `qtdemux` documentation doesn't show metadata track pad exposure
- [[parrot-video-metadata-docs]] — metadata in MP4 `tref` track, type `cdsc`
