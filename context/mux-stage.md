---
type: Component
title: Mux stage — MP4 in, MPEG-TS with video + KLV out
description: How a Parrot MP4 becomes an MPEG-TS carrying its own video unchanged plus a synchronous ST 0601 KLV stream — the one timeline both branches share, and what a failed conversion must not leave behind
tags: [mux, mpegts, klv, gstreamer, libmisbklv, phase-3]
timestamp: 2026-07-26T05:30:00Z
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

## Video is passed through, never decoded

`parsebin` parses the elementary stream and hands it to the muxer as it was, so
whatever the drone encoded — H.264 from the ANAFI range, H.265 from an Ai — is
what comes out, with no codec branching here and no generation loss. The
source's other tracks are dropped, including its Parrot metadata track: the KLV
in the output is the KLV this tool generated, not a copy of the input's.

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

libmisbklv removes the output for a failure while *opening* the pipeline. A
failure that surfaces later does not get that treatment, and there is a real
case: a source whose video track is declared but does not actually parse (our
own pre-mux fixtures, whose "video" is five bytes of `0xDEADBEEF`) opens fine and
fails at EOS, leaving the empty file the sink created. So this stage removes the
output itself on any error — but only a file it created, checking for existence
before the sink is built, so a pre-existing file at that path is never deleted
by a failed run.

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
3. the packets sit where the metadata track put them: PES timestamp intervals
   match the sample spacing to 90 kHz rounding.

The KLV is read back with libmisbklv's `KlvStream` (a consumer's view rather
than our encoder's), but the PMT and the PES timestamps are parsed by the test
itself — those are claims about the *container*, and checking them through the
same library that wrote it would prove nothing.

**The presentation times cannot come from `KlvStream`**: libmisbklv's extract
path reports every packet as `kNoPts` (its reassembly drains framed packets
without carrying the buffer timestamp), whatever the PES headers actually say.
That is why the test reads the timestamps off the container directly. It is an
upstream gap, not a defect in what this stage writes — the timestamps are there
in the output.

## The fixtures this needed

The extraction fixtures carry five bytes of dummy video per frame, which is
plenty for a parser that reads the *other* track. gstreamer will not accept
them, so `make_fixture.py` also emits `parrot_v3_video.mp4` and
`parrot_proto_video.mp4`, whose video track is a genuine encode — 64x64, 8
all-IDR frames, packaged as AVCC samples with an `avcC` in the sample entry.

ffmpeg is needed to *regenerate* those fixtures, never to use them: the
committed `.mp4` is what the suite reads, so the rule that no test may require
anything from `data/` still holds, and a machine with no encoder still runs
every test.
