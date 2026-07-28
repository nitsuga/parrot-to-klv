---
type: Component
title: Mux stage — MP4 in, MPEG-TS with video + KLV out
description: How a Parrot MP4 becomes an MPEG-TS carrying its own video — never decoded, but with its timing NALs rewritten to carry ST 0604 — plus a synchronous ST 0601 KLV stream; the one timeline both branches share, and what a failed conversion must not leave behind
tags: [mux, mpegts, klv, gstreamer, libmisbklv, phase-3]
timestamp: 2026-07-26T06:00:00Z
---

# Mux stage

The last stage: a Parrot MP4 in, an MPEG-TS out carrying the recording's own
video elementary stream plus the ST 0601 packets [[transform-stage]] produced.
`mux_to_ts(input, output)` is the whole surface, and the CLI's `-o` is its only
caller.

Almost none of the container work is ours. libmisbklv's `KlvSink` takes a
`video_source` and builds `filesrc ! parsebin` alongside its KLV appsrc into one
`mpegtsmux`, so this stage is a loop that pushes packets with timestamps. What
this document covers is the part that *is* ours: the timeline the packets go on,
and the failure behaviour.

## Files

| File | Role |
|------|------|
| `src/mux.{hpp,cpp}` | `mux_to_ts()` and `MuxReport` |
| `test/mux_test.cpp` | `ctest` target `mux` |
| `test/make_fixture.py` | Also generates the two real-H.264 fixtures this stage needs |

## Video is never decoded — but it is not bit-identical either

`parsebin` parses the elementary stream and hands it to the muxer, so whatever
the drone encoded — H.264 from the ANAFI range, H.265 from an Ai — is what comes
out, with no codec branching here and no generation loss. The source's other
tracks are dropped, including its Parrot metadata track: the KLV in the output
is the KLV this tool generated, not a copy of the input's.

**The timing NALs are rewritten, though**, so do not describe this as a
passthrough. libmisbklv strips Picture Timing SEI (type 1), injects ST 0604
Precision Time Stamp SEI (type 5) built from the ST 0601 `sensorTimestamp`, and
repeats SPS/PPS with every IDR frame. Parrot MP4s carry no SEI timestamps at all
— timing lives in the `mett` track — so this is generated, not preserved, and it
is what lets a consumer correlate frames without reading the KLV branch.

None of that is ours: it is unconditional in the pinned libmisbklv, arrived in a
drive-by pin bump, and has no opt-out. Why we keep it and what it costs:
[[0006-inherited-st0604-sei-injection]]. The claim that survives is **never
decoded, never re-encoded** — not "unchanged".

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

## An error must leave no output file

A conversion that fails has to leave nothing behind, because a zero-byte `.ts`
next to an error message reads like a run that half-worked and will be picked up
by whatever runs next.

**libmisbklv guarantees this for the whole insert session** (its ADR 0022), so
this stage does no cleanup of its own: a failure in the open, a push or the
finish all leave no file, and never delete a file the run did not create. That
guarantee originally covered only the open, which left a real gap — a source
whose video track is declared but does not actually parse (our own pre-mux
fixtures, whose "video" is five bytes of `0xDEADBEEF`) opens fine and fails at
EOS. `mux_test` still asserts the property from this side, since it is this
tool's promise to its user whoever implements it.

## A skipped sample costs a frame, not the file

A sample that does not decode, or that maps to no packet, is counted in
`MuxReport::frames_skipped` and the conversion carries on. One corrupt sample in
a flight should cost that frame's telemetry, nothing more. A run that converts
*nothing* is an error, not a successful empty file.

## What the test asserts, and why not through our own code

`mux_test` checks three properties, for both supported formats:

1. the output's PMT lists **two** elementary streams — H.264 and KLV (stream
   type 0x06 with a "KLVA" registration descriptor). A KLV-only output is the
   failure mode the video branch exists to prevent, so it is asserted rather
   than assumed.
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

Worth knowing when reading that as a regression guard: the bug it is modelled on
does **not** reproduce on gstreamer 1.20, where `h264parse` already emits
`byte-stream` and the pipeline links regardless. It is CI's newer stack that
would catch a recurrence, which is the same lesson as the transitive-include
break — a green suite says nothing about a stack you did not run on.

ffmpeg is needed to *regenerate* those fixtures, never to use them: the
committed `.mp4` is what the suite reads, so the rule that no test may require
anything from `data/` still holds, and a machine with no encoder still runs
every test.
