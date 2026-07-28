---
type: Decision
title: ADR 0006 — Accept libmisbklv's ST 0604 SEI injection; the video is no longer a pure passthrough
description: The pinned libmisbklv rewrites the H.264 elementary stream unconditionally — stripping Picture Timing SEI and injecting ST 0604 — so keep it, and stop claiming the video passes through unchanged
status: accepted
fork: 6
tags: [decision, mux, video, h264, sei, misb, 0604, phase-3]
timestamp: 2026-07-27T22:00:00Z
---

# Context

From Phase 3 onward this project made one promise about video, in nine places
including the CLI's own success message: the recording's elementary stream is
**passed through unchanged**. Parsed, never decoded, no generation loss, no
codec branching — whatever the drone encoded is what comes out.

That promise is no longer true, and it stopped being true without anyone here
deciding it should.

libmisbklv `f6d52e4` (their fork 21 / ADR 0023) sets `generate_sei = true`
unconditionally on the MP4-video-source path — the path `mux_to_ts()` takes.
Together with `e3c7f4a` the muxer now, on every conversion:

1. inserts SPS/PPS with every IDR frame (`h264parse config-interval=-1`),
2. **strips** Picture Timing SEI (type 1) from the source, and
3. **injects** ST 0604 Precision Time Stamp SEI (type 5), built from the ST 0601
   `sensorTimestamp` and matched to frames by a backward fuzzy PTS lookup.

NALs are removed and NALs are added. Verified on `parrot_v3_video_audio.mp4`:
the string `MISPmicrosectime` appears zero times in the source MP4 and eight
times in the output TS, one per metadata sample.

Two things made this invisible. It arrived inside a **drive-by pin bump** in an
unrelated feature commit, so no commit message named it. And upstream's own
motivation was *this project's* consumer — their ADR 0023 cites parrot-to-klv
and its downstream client needing 0604 for frame-accurate correlation — so the
change was made for us, just never recorded by us.

# Decision

**Accept the injection as the tool's behaviour, and correct every claim that
says otherwise.**

Keeping it is the easy half: Parrot MP4s carry no SEI timestamps at all — they
put timing in a separate `mett` metadata track — so a consumer wanting
frame-accurate time in the elementary stream can only get it if something
generates it. We are the only stage that holds both the video and the decoded
`sensorTimestamp`, so generating it here is the only place it *can* happen.
Refusing it would mean our own downstream client cannot correlate frames.

The substantive half is what we now say. "Unchanged" is retired as a description
of the video path, in the docs and in the CLI. What replaces it is the narrower
claim that is still true and still load-bearing:

> The video is **never decoded and never re-encoded** — no generation loss, no
> codec branching, the same H.264/H.265 the drone recorded. Its **timing NALs
> are rewritten**: Picture Timing SEI is replaced by ST 0604, and parameter sets
> repeat per IDR.

That is the honest line, and it preserves the property users actually cared
about (no transcode) while dropping the one we can no longer support (bit
fidelity).

This also **widens the project's stated scope.** ROADMAP and README both listed
"KLV standards beyond ST 0601" as out of scope while the tool emitted ST 0604 on
every run. Scope now admits ST 0604 SEI as an inherited output — not something
we implement or maintain, but something our output demonstrably contains.

# Alternatives considered

## Keep the behaviour, fix only the docs

**Rejected as insufficient, though it is most of the work.** The CLI prints
"video passed through unchanged" on every successful conversion; leaving that
while correcting the Markdown would fix the documentation and keep lying to the
person actually running the tool. The string is the most-read claim we make.

## Ask upstream for an opt-out and gate it behind a flag

Add `--no-sei` or similar, defaulting to faithful remux.

**Deferred, not rejected.** There is a real use case — an evidentiary or
archival remux that must be bit-faithful — and today this tool cannot produce
one. But the flag has to exist upstream first (the toggle is a libmisbklv
internal), no such consumer has asked, and inventing the requirement to satisfy
a symmetry argument would be speculative. Recorded here so the gap is findable
if it ever bites. See open questions.

## Refuse the pin and hold at f9070fb

**Rejected.** It would forgo the audio-track fix the previous bump landed and
strand us behind upstream to preserve a property no current consumer requires,
while the consumer we do have needs the opposite.

# Consequences

- **The output is not a faithful remux, and cannot currently be made one.**
  Anyone comparing output video bit-for-bit against the source will find
  differences, and that is expected behaviour rather than a defect.
- Our conformance story is unaffected — those reports cover metadata decode, not
  the video branch — but nothing here tests the SEI injection either. It is
  upstream's test surface, and we take it on trust.
- We now depend on upstream's PTS-matching heuristic: a backward-only fuzzy
  lookup with 200 ms tolerance. A frame whose PTS finds no metadata sample
  inside that window gets no 0604 SEI, silently. We neither detect nor report
  that.
- The scope line moves, so "what standards does this tool emit" now has two
  answers with different owners: ST 0601, which we implement, and ST 0604 SEI,
  which we inherit.

# Assumptions / open questions

- Assumes no consumer needs a bit-faithful remux. If one appears, the fix is an
  upstream opt-out, and this ADR should be revisited rather than worked around
  here.
- The 200 ms PTS tolerance and the silent no-match behaviour are upstream's
  choices, unexamined here. Whether real Parrot footage ever trips them is
  unknown — every fixture is synthetic and short.
- Assumes stripping Picture Timing SEI is harmless for our consumers. Upstream
  did it to silence parser warnings when SPS lacks VUI timing; a consumer that
  actually read Picture Timing would now find it gone.

# Citations

- [[mux-stage]] § Video is never decoded — the corrected description
- [[0001-mp4-extraction-approach]] § the GStreamer video path — the original
  "re-muxed unchanged" claim, now annotated
- libmisbklv ADR 0023 (fork 21), commits `e3c7f4a` and `f6d52e4`; pin history in
  [[log]] § 2026-07-27
