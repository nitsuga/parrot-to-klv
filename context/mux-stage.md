---
type: Component
title: Mux stage — MP4 in, MPEG-TS with video + KLV out
description: How a Parrot MP4 becomes an MPEG-TS carrying its own video — never decoded, but with its timing NALs rewritten to carry ST 0604 — plus a synchronous ST 0601 KLV stream; the one timeline both branches share, and what a failed conversion must not leave behind
tags: [mux, mpegts, klv, gstreamer, libmisbklv, phase-3]
generated:
  by: claude/opus-5
  at: 2026-08-12T00:00:00Z
---

# Mux stage

The last stage: a Parrot MP4 in, an MPEG-TS out carrying the recording's own
video elementary stream plus the ST 0601 packets [[transform-stage]] produced.
`mux_to_ts(input, output)` is the whole surface, and the CLI's `-o` is its only
caller.

Almost none of the container work is ours. libmisbklv's `KlvSink` takes a
`video_source` and builds `filesrc ! demuxer ! parser` alongside its KLV appsrc
into one `mpegtsmux`, so this stage is a loop that pushes packets with
timestamps. What this document covers is the part that *is* ours: the timeline
the packets go on, and the failure behavior.

## Files

| File | Role |
|------|------|
| `src/mux.{hpp,cpp}` | `mux_to_ts()` and `MuxReport` |
| `test/mux_test.cpp` | `ctest` target `mux` |
| `test/make_fixture.py` | Also generates the real-encode fixtures this stage needs (H.264 and one HEVC) |

## Video is never decoded — but it is not bit-identical either

`h264parse`/`h265parse` parse the elementary stream and hand it to the muxer, so
whatever the drone encoded is what comes out, with no generation loss. No decoder
is involved, and — since libmisbklv's ADR 0025 — none needs to be *installed*
either, which is what lets the release bundle ship without one
([[0009-release-bundle]]). The source's other tracks are dropped, including its
Parrot metadata track: the KLV in the output is the KLV this tool generated, not
a copy of the input's.

**"Dropped" means each one gets its own queue to a fakesink, and the queue is
load-bearing.** A demuxer pushes every stream from a single thread and a sink
prerolling in PAUSED blocks that thread, so without a queue in between, a stream
we discard stalls the video behind it and the pipeline never reaches PLAYING.
That is what hung libmisbklv's CI, invisibly on gstreamer 1.20. Recorded here
because it looks like tidiness from this side of the boundary; libmisbklv's
ADR 0020 owns the decision.

**There is one codec branch, and it is about timestamps, not video.** The
airframes in scope record H.264, and ST 0604 SEI generation is H.264-only
upstream — so `mux_to_ts()` asks for generation on H.264 and carries anything
else through untouched and unstamped. Asking on H.265 does not degrade, it
refuses the conversion, which is fork 7
([[0007-h265-vs-st0604-generation]]).

**For H.264 the timing NALs are rewritten**, so do not describe that path as a
passthrough. libmisbklv strips Picture Timing SEI (type 1), injects ST 0604
Precision Time Stamp SEI (type 5) built from the ST 0601 `sensorTimestamp`, and
repeats SPS/PPS with every IDR frame. Parrot MP4s carry no SEI timestamps at all
— timing lives in the `mett` track — so this is generated, not preserved, and it
is what lets a consumer correlate frames without reading the KLV branch.

It is ours now: libmisbklv's default (`Sei0604::Preserve`) leaves the elementary
stream byte-identical, and `mux_to_ts()` passes `Sei0604::Generate` to get this
behavior. It arrived as an unconditional upstream change with no opt-out, which
is why the ADR reads as it does; the opt-out landed later, and we kept the
behavior deliberately. Why, and what it costs:
[[0006-inherited-st0604-sei-injection]]. The claim that survives is **never
decoded, never re-encoded** — not "unchanged". On a non-H.264 source
"unchanged" *is* accurate, which is the one place the older wording still holds.

Under `Generate` the KLV is the stream's single timestamp authority: any ST 0604
the source already carried is *replaced* rather than added to. Moot for Parrot
recordings, which carry none, but it matters for an input that arrives stamped.

## One timeline, taken from the metadata track

libmisbklv times the video branch from the start of the source file and requires
the KLV to land on that same timeline — presentation time from the start of the
file, in nanoseconds. A `Message` with no PTS is rejected rather than given a
synthesized one, which is the right call: a synthetic ~30 fps counter drifts
silently against real frame timing.

The metadata track's own sample times are already on that timeline — both tracks
are timed from the start of the same MP4 — so `MetadataSample::timestamp_us`
converts straight to nanoseconds and that is the whole timing story. Note what
this is *not*: it is not the frame's monotonic clock reading, and not the UTC
time in Item 2. Those describe when the drone sampled telemetry; the PTS
describes where the packet sits in the file, and the two are anchored
differently (see [[transform-stage]] § Item 2).

**One metadata sample becomes one KLV packet.** Parrot writes one metadata
sample per video frame, so this yields per-frame KLV without any resampling.

## Push in order, and let the muxer push back

The muxer waits on the slower pad and the KLV appsrc blocks, so `emit()` applies
backpressure as the video branch flows. That makes the natural implementation —
walk samples in order, emit each — the correct one, and rules out the tempting
alternative of converting everything up front and then draining it into the
sink.

## Input and output must be different files

`mux_to_ts()` rejects an input/output alias by **file identity** before it opens
or otherwise touches the output. Textual path comparison is not enough: a
relative spelling, a symlink, or a hard link can all name the same file. This
is the public-operation boundary, not merely a CLI check, so a future library
caller receives the same protection.

This is a confirmed data-loss guard for muxing. The sink opens its output while
the video branch still reads the input; if both names identify the same file,
opening the output truncates the source before the pipeline can consume it.
The operation fails, but the recording is already zero bytes. Refuse the alias
before creating the sink instead.

## An error must leave no output file

A conversion that fails has to leave nothing behind, because a zero-byte `.ts`
next to an error message reads like a run that half-worked and will be picked up
by whatever runs next.

**libmisbklv guarantees this for the whole insert session** (its ADR 0022): a
failure in the open, a push, or an unsuccessful finish all leave no file, and
it never deletes a file the run did not create. `KlvSink`'s destructor is where
this actually happens — an abandoned or unfinished session has produced only an
unfinalized file, and the destructor discards it unless `close()`/`finish()`
already succeeded and cleared that state. That guarantee originally covered
only the open, which left a real gap — a source whose video track is declared
but does not actually parse (our own pre-mux fixtures, whose "video" is five
bytes of `0xDEADBEEF`) opens fine and fails at EOS. `mux_test` still asserts
the property from this side, since it is this tool's promise to its user
whoever implements it.

This stage still has two things of its own to get right, because they are
outside anything libmisbklv can see from inside one push or one finish:

- **Deciding failure *before* calling `close()`, not after.** A run that
  converts zero frames is a error this project defines — libmisbklv sees a
  video branch that drained cleanly and calls that a successful finish, which
  clears its own "discard on destruction" state and keeps the file. `mux_to_ts`
  computes `report.error` for the zero-frame case (and for any error a sample
  loop already raised) *before* it calls `close()`, and skips the call
  entirely when an error is already set — so the sink is destroyed unfinished,
  and its own destructor discards the file, exactly the same path a mid-loop
  push failure already took. Calling `close()` unconditionally and only
  checking `report.error` afterward would leave the video-only, KLV-empty
  output on disk with a "Conversion failed" message next to it — a
  complete-looking file next to a lie about it.
- **SIGINT/SIGTERM.** No process-level signal has any relationship to
  libmisbklv's destructor-based cleanup unless something turns it into a
  normal, stack-unwinding return — the default disposition for both signals is
  immediate termination, which runs no destructors at all and leaves whatever
  the sink had written so far. `main.cpp` installs a handler that only sets a
  flag (`request_mux_interrupt()`, async-signal-safe: a bare write to a
  `sig_atomic_t`); `convert_all()`'s per-sample loop checks it once per
  iteration and, if set, reports the run as interrupted and stops — the same
  "decide before `close()`" path above then applies. This is cooperative, not
  preemptive: a signal that arrives while a single sample is blocked inside
  the sink's push is not observed until that call returns.

The guarantee is deliberately limited to a file this run created. If the output
already existed, the sink must not delete somebody else's file; a failed run may
therefore leave that pre-existing output truncated or partial. The same-file
guard protects the input recording, not an old distinct output. Preserving an
old output until its replacement is complete requires a separate transactional
write policy.

## A skipped sample costs a frame, not the file

A sample that does not decode, or that maps to no packet, is counted in
`MuxReport::frames_skipped` and the conversion carries on. One corrupt sample in
a flight should cost that frame's telemetry, nothing more. A run that converts
*nothing* is an error, not a successful empty file.

## What the test asserts, and why not through our own code

`mux_test` checks three properties, for both supported formats:

1. the output's PMT lists **two** elementary streams — H.264 and KLV (stream
   type 0x06 with a "KLVA" registration descriptor) — **with the video first**. A
   KLV-only output is the failure mode the video branch exists to prevent, so it
   is asserted rather than assumed. The *order* is asserted for the same reason:
   `mpegtsmux` orders the PMT by elementary-stream PID and allocates PIDs in
   sink-pad request order, so the video pad is *reserved* while the pipeline is
   still NULL — before the KLV appsrc links — and takes the lower PID, making
   the video `0:0` and the KLV `0:1`. A consumer selecting by index
   (`ffmpeg -map 0:0`) now gets the video. The mechanism, and the two earlier
   attempts that failed with `GST_PAD_LINK_NOFORMAT`, are in
   libmisbklv ADR 0020 § Stream order (revised 2026-08-12). The order is pinned
   because the old metadata-first order once emptied our KLV stream; a
   regression here is a deliberate change, not a silent one.
2. every KLV packet read back is byte-identical to what `to_0601()` produced for
   that sample, in sample order.
3. each packet comes back at its own sample's presentation time — the same
   timeline, origin included, to 90 kHz rounding.

The KLV is read back with libmisbklv's `KlvStream`, a consumer's view rather
than our encoder's. Only the PMT is parsed by the test itself: that is a claim
about the *container*, and checking it through the same library that wrote it
would prove nothing.

Property 3 was originally asserted by parsing PES headers here, because
libmisbklv's read path reported every packet as `kNoPts` whatever the container
said. That gap is closed upstream (its ADR 0021: both extractors now report
nanoseconds from the start of the source, the same timeline `push()` writes on),
which is what lets the test check absolute placement rather than just spacing —
a stronger assertion, and one that would catch an origin error the interval
check could not see.

## The fixtures this needed

The extraction fixtures carry five bytes of dummy video per frame, which is
plenty for a parser that reads the *other* track. gstreamer will not accept
them, so `make_fixture.py` also emits `parrot_v3_video.mp4` and
`parrot_proto_video.mp4`, whose video track is a genuine encode — 64x64, 8
all-IDR frames, packaged as AVCC samples with an `avcC` in the sample entry.

`parrot_v3_video_audio.mp4` adds the third track a real recording has: AAC in an
`mp4a`/`esds` sample entry, on its own timescale. **A fixture that omits audio
is not the shape of the input this tool is for** — the muxer must link a pad it
then throws away, and real footage found a bug there (upstream's pad-linking and
H.264-format fix) while every fixture here stayed green. For that fixture, the
"PMT lists exactly two streams" assertion earns a second meaning: the audio was
dropped, not forwarded.

`parrot_v3_video_h265.mp4` is the same recording in HEVC — `hvc1` with an
`hvcC`, built the same way and needing libx265 only to regenerate. It exists to
hold the other branch of [[0007-h265-vs-st0604-generation]]: it must convert,
and it must come out with **no** ST 0604. That "no" is asserted rather than
assumed, because the regression it guards against is one where the video is
refused outright, and an absent assertion looks identical to a passing one.

Worth knowing when reading that as a regression guard: the bug it is modeled on
does **not** reproduce on gstreamer 1.20, where `h264parse` already emits
`byte-stream` and the pipeline links regardless. It is CI's newer stack that
would catch a recurrence, which is the same lesson as the transitive-include
break — a green suite says nothing about a stack you did not run on.

ffmpeg is needed to *regenerate* those fixtures, never to use them: the
committed `.mp4` is what the suite reads, so the rule that no test may require
anything from `data/` still holds, and a machine with no encoder still runs
every test.
