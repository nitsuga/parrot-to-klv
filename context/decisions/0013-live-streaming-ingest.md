---
type: Decision
title: Live ingest and multicast output — vendor extraction stays here, generic mux comes from libmisbklv
description: Split the live-streaming work on the dependency line — libmisbklv owns the generic live mux/pacing/multicast surface, parrot-to-klv owns the RTSP pull, the Parrot RTP header-extension capture, the existing v3/proto decode, and the transform
decision_status: accepted
fork: 13
tags: [decision, streaming, live, rtsp, mux, phase-4]
generated:
  by: claude/opus-5
  at: 2026-08-20T23:10:00Z
sources:
  - id: parrot-vmeta-html
    resource: ../../references/Embedded Video Metadata - 8.4.2.html
    title: Parrot Embedded Video Metadata — "Integration in the RTP stream"
  - id: libmisbklv-0031
    resource: https://github.com/nitsuga/libmisbklv/blob/4a28859511c7b4c5117959335afd4e845f3e7313/context/decisions/0031-live-streaming-surface.md
    title: "libmisbklv ADR 0031 (fork 28, accepted) — Vendor-neutral live streaming surface"
---

# Context

Today this tool is a file converter: an MP4 in → its `mett` track extracted by
direct box scan ([[0001-mp4-extraction-approach]]) → the v3/proto formats
decoded ([[0003-protobuf-dependency]], [[0004-shared-telemetry-snapshot]]) →
mapped to ST 0601 in the transform → remuxed by libmisbklv's `KlvSink` with a
`video_source` and `Sei0604::Generate` into an MPEG-TS file ([[mux-stage]]).
Real-time streaming is currently listed out of scope and the integration API
is deferred in [`../../planning/ROADMAP.md`](../../planning/ROADMAP.md).

We now want both directions live: **stream in a live video source** (Parrot
drones send over RTSP) and **output a live MPEG-TS over UDP multicast**.

The decisive fact about the live input: for *streaming*, Parrot does **not** put
frame metadata in the video elementary stream. It rides as RTP **packet-header
extensions** — a 16-bit `defined by profile` of `P1`/`P2`/`P3`/`Pb` — sent on
the first packet of each frame; only the frame capture timestamp is in the ES
(H.264 picture-timing SEI / H.265 time-code SEI), and session metadata comes via
RTCP SDES / SDP.[^parrot-vmeta-html] A plain `rtspsrc ! rtph264depay` consumes
those extensions and drops them, so an "RTSP → TS handoff" loses the metadata.

Meanwhile libmisbklv is **providing the generic half of the problem** (their
fork 28 / ADR 0031, now accepted at 4a28859): a `video_source` that accepts a GStreamer
URI/description, multicast sink knobs, and `realtime` + a video source no longer
rejected.[^libmisbklv-0031] That is the mux/pacing/multicast surface both Parrot
and DJI converters share; the only thing that differs per vendor is the
metadata transport + decode, which is precisely what this project owns.

# Decision

Split on the dependency line, keeping the arrow `parrot-to-klv → libmisbklv`:

- **libmisbklv** owns the generic, vendor-neutral live surface (their ADR 0031):
  live `video_source`, multicast/broadcast sink knobs, and clock-paced
  `realtime` output with a video branch.
- **parrot-to-klv** owns the entire live ingest path *up to `Message`*:
  1. Pull RTSP and capture the Parrot RTP header-extension bytes with a small
     `GstRTPHeaderExtension` wired into `rtspsrc` (first packet of each frame;
     coalesce the `proto` split), before depay discards them.
  2. Decode with the **existing** `parrot_v3` / `parrot_proto` parsers, normalize
     to the existing `TelemetrySnapshot`, and map with the **existing** transform
     to `to_0601()`.
  3. `KlvSink{ sink="udp:239.x.x.x:port", realtime=true,
     video_source="<the rtspsrc…!rtph264depay…!h264parse path>",
     sei_0604=Generate, <multicast knobs> }` (`Generate` now wired for live H.264
     via libmisbklv 4ac014a probe — `pipeline:` and RTSP branches wire the same
     `h264parse` + `on_h264_buffer_inject_sei` probe that file path uses; live H.264
     therefore carries ST 0604 SEI and KLV↔video time sync. Non-H.264 live degrades
     to unstamped (no SEI) per libmisbklv #23 #1 — gated on H.264 caps, H.265 carried
     through), pushing each packet on the depay buffer's PTS. `pipeline:` is a test-only hermetic escape hatch
     for hermetic tests, gated at the CLI by `PARROT_TEST_ALLOW_PIPELINE`
     (see Consequences).
- The decoders + transform transfer **unchanged** — that is what the shared
  snapshot [[0004-shared-telemetry-snapshot]] was shaped for.
- Live is a **second mode** (its own input/CLI path), not a replacement for
  MP4-file conversion.
- **No new shared repo.** This ADR records the placement; a shared repo is only
  worth creating once two *shipped* live converters have converged on harness
  code libmisbklv can't host for neutrality. dji-to-klv (experimental) is a
  future consumer of the same libmisbklv surface with its own decode —
  parrot-to-klv is the reference for what that shared surface must be.

This placement is now **accepted** — libmisbklv ADR 0031 is now accepted
(4a28859), so implementation is unblocked. This ADR records the placement
decision; the live surface it depends on is the pin.

# Alternatives considered

- **Accept a client-handoff TS** (the client runs `rtspsrc ! mpegtsmux !
  udpsink` and we read a live TS): rejected — `rtph264depay` in that chain drops
  the RTP header extensions that carry the Parrot metadata, so the handoff loses
  the very data we convert. Re-supplying it another way is more work than
  extracting it here, and it forces libmisbklv down its live-0x15 remux fork.
- **Put the vendor extraction in libmisbklv**: rejected. It couples a published,
  versioned, vendor-neutral MISB/KLV library to a drone vendor's proprietary
  transport and format, forces a `protobuf` dependency into the core, and invites
  every other vendor in — the library becomes drone-vendor compat. It is the
  exact move their ADR 0020 warns about, and it inverts the dependency arrow.
- **A new shared repo (`drone-live-klv`) parameterized by a `VendorDecoder`,
  shared by parrot-to-klv and dji-to-klv**: rejected for now. Premature
  abstraction before this project has even shipped a live path; a three-repo
  version matrix with nothing proven against it. The parameterized interface
  leaks — DJI live transport is not Parrot's RTP extensions, so the "decoder"
  boundary doesn't hold at the transport layer. The genuinely shared code (the
  mux) already lives in libmisbklv. Revisit only when the two converters have
  converged on identical harness code.

# Consequences

- parrot-to-klv gains a live ingest path — the deferred "integration API" in
  [`../../planning/ROADMAP.md`](../../planning/ROADMAP.md) gets its first consumer,
  and that roadmap's "real-time streaming is out of scope" line changes once it
  is implemented.
- **New capability here:** capturing Parrot metadata from RTP header extensions
  (a small `GstRTPHeaderExtension` subclass). Today's input is MP4 only, so this
  is net-new and distinct from the box scan.
- **Timing model changes for live:** there is no "file start" to anchor a single
  timeline to. KLV PTS anchors to the reception running-time (matching
  libmisbklv ADR 0021's live semantics); Item 2 UTC stays the absolute clock.
  The MP4 "one timeline from the metadata track" story ([[mux-stage]]) does not
  transfer.
- No new GStreamer dependency beyond what libmisbklv's gst facade already
  implies; no protobuf/minimp4 dependency added here (both already decided in
  [[0003-protobuf-dependency]]).
- Live H.264 now uses `Sei0604::Generate` (ST 0604 SEI via `h264parse` + `on_h264_buffer_inject_sei` probe, wired for both `pipeline:` and RTSP live branches via libmisbklv 4ac014a); KLV↔video time sync is preserved. Non-H.264 live degrades to unstamped (no SEI) per libmisbklv #23 #1 — gated on H.264 caps, H.265 carried through (see ADR 0007).
- `pipeline:` video_source is a test-only hermetic escape hatch for hermetic
  tests, gated at the CLI by `PARROT_TEST_ALLOW_PIPELINE` (library validation
  still accepts it so direct `live_ingest({rtsp_uri="pipeline:..."})` hermetic
  tests pass without env).
- libmisbklv ADR 0031 is now accepted (4a28859), so implementation is unblocked.

# Assumptions / open questions

- Live ships as an added mode / CLI input, not a merge with MP4-file mode — to
  confirm at CLI design.
- Whether the ingest path is a callable library API or lives in `main()` — the
  deferred integration-API question, now on the critical path.
- SRT output and broadcast specifics are deferred until a requirement appears.
- Live SRT extraction and live-0x15 remain libmisbklv roadmap items; out of
  scope here.

# Citations

- [[parrot-video-metadata-docs]] and [[parrot-metadata-formats]] — the RTP
  header-extension carriage and the v3/proto layouts this reuses for live.
- [[mp4-extraction]], [[transform-stage]], [[mux-stage]] — the stages this
  generalizes.
- [[0001-mp4-extraction-approach]] — the original boundary this preserves:
  metadata = vendor path, video = libmisbklv.
- [[0004-shared-telemetry-snapshot]] — why the decoders + transform transfer to
  live unchanged.
- [[0006-inherited-st0604-sei-injection]] — the SEI `Generate` decision file
  mode uses; live now also uses `Generate` for H.264 via libmisbklv 4ac014a probe (see Decision bullet 3); non-H.264 degrades to unstamped per libmisbklv #23 #1 — gated on H.264 caps, H.265 carried through.
- libmisbklv ADR 0031 (fork 28, accepted at 4a28859) — the generic live surface this
  depends on (see `sources[libmisbklv-0031]`).
