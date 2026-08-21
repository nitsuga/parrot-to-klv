---
type: Decision
title: File replay to a live sink — MP4 -> UDP/SRT multicast with paced realtime playback
description: Bridge the deterministic offline path to the live sink — replay an MP4's video on the pipeline clock and emit its mett-track telemetry as KLV (PTS = zero-based file running time) to reach the deferred -i file.mp4 -o udp:/srt: route, with paced emission and play-through-close semantics
decision_status: accepted
fork: 13
tags: [decision, live, streaming, replay, mux, phase-4]
generated:
  by: claude/opus-5
  at: 2026-08-21T00:00:00Z
---

# Context

The unified CLI ([[0014-unified-cli-input-output-inference]]) resolves
`-i <input> -o <output>` by type inference. Of the four `input × output`
combinations, three route correctly today:

- `path → path`: offline, deterministic `mux_to_ts` (byte-identical, file-clock).
- `rspt → {path | udp:/srt:}`: `live_ingest`, KLV from RTP header extensions.
- `rspt` with no `-o`, and `path → {udp:/srt:}`: clear errors.

The missing cell is **`path → {udp:/srt:}`** — play a recorded Parrot MP4 out
as a live multicast/stream. `live_ingest` cannot serve it: its KLV source is the
RTP header-extension FIFO, and an MP4's telemetry lives in the `mett` timed-
metadata track, which that path never reads. The router currently answers with
"not yet supported" ([[0014-unified-cli-input-output-inference]]).

The pieces the bridge needs already exist on both sides of the split:

- **Extraction/transform:** `mp4_metadata_extractor` reads the `mett` track
  (each `MetadataSample` carries a `timestamp_us`), `parrot_v3`/`parrot_proto`
  decode it, `to_0601` maps it to ST 0601 — exactly the offline pipeline.
- **Live mux/clock:** libmisbklv's insert path, per ADR 0031 item 3, replays a
  `file:` `video_source` at `realtime` on the pipeline clock to a live sink, and
  ADR 0021 defines KLV PTS as nanoseconds from the start of the source — the
  same running-time timeline the muxer consumes. The `udp:` multicast knobs
  (ttl/iface/loop) are already exposed via the CLI (`--iface`/`--ttl`/`--loop`).

So the bridge is glue: feed the mett-track KLV into a live `KlvSink` whose
video is the same file replayed on the clock.

# Decision

Implement `-i file.mp4 -o udp:/srt:…` as a **paced realtime replay of the
recording**, with these semantics:

1. **Build a live `KlvSink`** with
   `InsertConfig{ sink=<udp:|srt:>, realtime=true, video_source="file:<path>",
   sei_0604=Generate, udp_ttl/iface/loop from the CLI }`. libmisbklv replays the
   file's video on the pipeline clock (ADR 0031 item 3); the H.264 file gets ST
   0604 via the Generate probe exactly as the RTSP live branch does.

2. **KLV clock = the mett track's presentation timestamp, anchored zero-based**
   to the file's earliest presentation time (per ADR 0021). Each `MetadataSample`
   → `set_pts(timestamp_us - origin_ns, in ns)` → `emit()`. Because the video
   and `mett` tracks share the file's presentation timeline, KLV PTS equals the
   video branch's running time, so the muxer interleaves KLV with the frames it
   describes. This is the only correct live mapping (ADR 0021).

3. **Pace emission to the recording's own timeline**, not an up-front burst.
   Sleep between emits proportional to consecutive `timestamp_us` deltas at
   realtime. Reason: the muxer buffers KLV at its PTS until the running clock
   reaches it; emitting the whole flight up front would pile an unbounded KLV
   backlog into the muxer on a long recording. Paced emission keeps the in-flight
   KLV bounded at ~one inter-frame interval.

4. **Play through to the recording's natural end, then close (EOS)** and return
   success — **unless `--repeat` is given**, in which case the replay loops until
   SIGINT. One play-through is a single pre-recorded broadcast; `--repeat` makes
   it a continuous loop. The flag is named `--repeat`, not `--loop`, because
   `--loop` is already the multicast **loopback** toggle ([[0014-unified-cli-input-output-inference]]).
   A SIGINT/SIGTERM (via `request_mux_interrupt`, already wired) stops the replay
   cleanly with no partial output, reusing the interruption handling from
   [[0006-inherited-st0604-sei-injection]].

5. **Reuse, do not fork the offline path.** `mux_to_ts` (deterministic file-
   clock conversion) is untouched; this is a **separate function** that reuses
   `mp4_metadata_extractor` + `parrot_v3`/`parrot_proto` + `to_0601` + `KlvSink`.
   The `live_ingest` RTSP path is untouched. The router in `main.cpp` simply
   replaces the "not yet supported" error for `path → {udp:/srt:}` with a call
   to this function (udp knobs and sink translation already parse).

6. **ST 0604 note:** as with RTSP live, a non-H.264 file replays but is
   unstamped (no SEI) per ADR 0007 — H.265 carried through without SEI.

# Alternatives considered

- **Emit all KLV up front and let the muxer clock it out (no pacing).** Rejected:
  simpler, but on a long flight the KLV backlog buffered in the muxer grows with
  recording length (potentially large), and a slow consumer could build memory
  pressure. Paced emission bounds it.

- **Derive the clock from libmisbklv's video running time via a live-visible
  callback.** Rejected: the library exposes the file timeline, not a per-frame
  hook; the mett track already *is* the one shared timeline in this project
  (the offline mux uses it), so deriving KLV PTS from it is both sufficient and
  consistent with [[mux-stage]].

- **Loop the recording indefinitely until SIGINT by default.** Rejected as the
  *default*: a broadcast of a pre-recorded flight has a natural end. Looping is
  offered as the explicit `--repeat` opt-in instead of being the default mode.

- **Route the offline `path → path` through the same realtime sink for
  uniformity.** Rejected, as in [[0014-unified-cli-input-output-inference]]: the
  realtime path changes the timing/bytes; the deterministic offline conversion
  must stay byte-identical.

# Consequences

- `main.cpp` no longer errors on `path → {udp:/srt:}`; it routes to the new
  replay function. `help`/README document the usage
  (`-i flight.mp4 -o udp://239.0.0.1:5000 --iface eth1`, `--repeat` to loop).
- A new replay function (likely `src/live/` or alongside `live_ingest`) reusing
  the extractor + transform; `mux_to_ts` and `live_ingest` untouched.
- **Testing** (hermetic, `parrot` + `parrot-sanitize`): a file→udp replay to a
  `udp:` loopback reader asserting KLVA + ST 0604 present and KLV↔video drift
  under the clock-pinning threshold (mirroring libmisbklv's loopback test);
  plus natural-end/EOS and SIGINT-interruption cases leaving no partial output.
- The ROADMAP "file→udp deferred" items and PROGRESS "Next" move once this
  lands.

# Assumptions / open questions

- KLV PTS from the `mett` track's `timestamp_us` anchors to the same zero-based
  running time libmisbklv uses for the video branch (ADR 0021). Whether the two
  agree to within a frame is verified by the hermetic clock-pinning test, not
  asserted blindly — the one place the design could be off.
- `--repeat` replays until SIGINT; the loop boundary resets the KLV PTS origin
  each pass so each repetition is a self-consistent wall clock.
- Simultaneous File→KLV insertion (an MP4 with no video) to a live sink is not
  in scope; this cell requires the video track.

# Citations

- [[0014-unified-cli-input-output-inference]] — the CLI router that exposes this
  cell and currently rejects it.
- [[mux-stage]] — the mett track as the one authoritative timeline.
- libmisbklv ADR 0031 (live streaming surface) item 3 — file + realtime replay
  on the pipeline clock; libmisbklv ADR 0021 (read-path timestamps) — KLV PTS as
  zero-based running time.
- [[0006-inherited-st0604-sei-injection]] / [[0007-h265-vs-st0604-generation]] —
  ST 0604 Generate and the non-H.264 unstamped caveat.
