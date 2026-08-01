---
type: Decision
title: ADR 0006 — Accept libmisbklv's ST 0604 SEI injection; the video is no longer a pure passthrough
description: Keep the ST 0604 SEI injection libmisbklv arrived with, and stop claiming the video passes through unchanged — since made opt-in upstream, so we now request it explicitly rather than inherit it
decision_status: accepted
fork: 6
tags: [decision, mux, video, h264, sei, misb, 0604, phase-3]
generated:
  by: claude/opus-5
  at: 2026-07-27T22:00:00Z
sources:
  - resource: https://github.com/nitsuga/libmisbklv/commit/e3c7f4ad9a170aa66304f0acbec3c3e3326b0c07
    title: "libmisbklv — Fix H.264 SEI/SPS association for ST 0604 passthrough"
  - resource: https://github.com/nitsuga/libmisbklv/commit/032db105595997e257b9127b8725873d1d943048
    title: "libmisbklv ADR 0023 (fork 21) — ST 0604 SEI generation for video passthrough"
---

# Context

From Phase 3 onward this project made one promise about video, in nine places
including the CLI's own success message: the recording's elementary stream is
**passed through unchanged**. Parsed, never decoded, no generation loss, no
codec branching — whatever the drone encoded is what comes out.

That promise is no longer true, and it stopped being true without anyone here
deciding it should.

libmisbklv `032db10` (their fork 21 / ADR 0023) sets `generate_sei = true`
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

**Accept the injection as the tool's behavior, and correct every claim that
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

> **Narrowed in one detail (2026-07-28):** "no codec branching" and the rewritten
> timing NALs describe the **H.264** path. Generation turned out to be H.264-only
> upstream, so there is now exactly one codec branch and any other source is
> carried through unstamped — [[0007-h265-vs-st0604-generation]]. Everything else
> here stands.

That is the honest line, and it preserves the property users actually cared
about (no transcode) while dropping the one we can no longer support (bit
fidelity).

This also **widens the project's stated scope.** ROADMAP and README both listed
"KLV standards beyond ST 0601" as out of scope while the tool emitted ST 0604 on
every run. Scope now admits ST 0604 SEI as an inherited output — not something
we implement or maintain, but something our output demonstrably contains.

# Alternatives considered

## Keep the behavior, fix only the docs

**Rejected as insufficient, though it is most of the work.** The CLI prints
"video passed through unchanged" on every successful conversion; leaving that
while correcting the Markdown would fix the documentation and keep lying to the
person actually running the tool. The string is the most-read claim we make.

## Ask upstream for an opt-out and gate it behind a flag

Add `--no-sei` or similar, defaulting to faithful remux.

**Deferred, not rejected** — and the upstream half has since happened. See the
2026-07-28 revision below: libmisbklv made generation opt-in, so a bit-faithful
remux is now producible. Exposing it as a CLI flag is still deferred, and still
for the same reason: no consumer has asked.

## Refuse the pin and hold at f9070fb

**Rejected.** It would forgo the audio-track fix the previous bump landed and
strand us behind upstream to preserve a property no current consumer requires,
while the consumer we do have needs the opposite.

# Consequences

- **The output is not a faithful remux** — anyone comparing output video
  bit-for-bit against the source will find differences, and that is expected
  behavior rather than a defect. As of the revision below it *could* be made
  one, by not asking for generation; we choose not to.
- Our conformance story is unaffected — those reports cover metadata decode, not
  the video branch. As of the revision below, `mux_test` does check the SEI:
  that it exists, and that every timestamp in it is an item 2 value we emitted.
- We now depend on upstream's PTS-matching heuristic: a backward-only fuzzy
  lookup with 200 ms tolerance. A frame whose PTS finds no metadata sample
  inside that window gets no 0604 SEI, silently. We neither detect nor report
  that.
- The scope line moves, so "what standards does this tool emit" now has two
  answers with different owners: ST 0601, which we implement, and ST 0604 SEI,
  which we inherit.

# Revision — 2026-07-28: the opt-out arrived, and we opt in

The alternative deferred above is no longer hypothetical. libmisbklv's ADR 0024
(their fork 22) makes SEI generation opt-in — `Sei0604::Preserve` is now their
default and leaves the elementary stream byte-identical; `Sei0604::Generate` is
what this project's `mux_to_ts()` now passes explicitly.

**The decision here is unchanged, and is now a decision rather than an
inheritance.** We still want ST 0604 in the video, for exactly the reason above:
Parrot MP4s carry no SEI timestamps, we are the only stage holding both the video
and the decoded `sensorTimestamp`, and our downstream client reads time from the
elementary stream. What changed is that it is now *ours to state*, in one
argument at the call site, instead of something upstream did to every caller.

Three consequences of the upstream change land here:

- **A bit-faithful remux is now producible** — drop the argument and the video
  comes out byte-identical. So the evidentiary/archival use case is unblocked
  whenever a consumer asks; only the CLI flag is missing, which stays deferred.
- **The silent no-match gap is now testable and tested.** `mux_test` property 4
  reads the output's video elementary stream, decodes every `MISPmicrosectime`
  SEI, and requires each timestamp to be an ST 0601 item 2 value we emitted.
  Verified to fail without the `Sei0604::Generate` argument — which it did on
  the first run, catching a real hole: before this, dropping the argument would
  have produced an output correct in every other respect and useless to the
  client.
- **Upstream now replaces a source's ST 0604 rather than adding to it.** Moot
  for Parrot MP4s, which carry none, but it means an input that arrived already
  stamped would come out stamped by us, not by whoever made it.

# Assumptions / open questions

- Assumes no consumer needs a bit-faithful remux. One is now available upstream
  (see the revision); if a consumer asks, the work is a CLI flag here, not a
  change upstream.
- The 200 ms PTS tolerance and the silent no-match behavior are upstream's
  choices, unexamined here. Whether real Parrot footage ever trips them is
  unknown — every fixture is synthetic and short.
- Assumes stripping Picture Timing SEI is harmless for our consumers. Upstream
  did it to silence parser warnings when SPS lacks VUI timing; a consumer that
  actually read Picture Timing would now find it gone. As of their ADR 0024 this
  only happens because we asked for generation — it is our stripping now.

# Citations

- [[mux-stage]] § Video is never decoded — the corrected description
- [[0001-mp4-extraction-approach]] § the GStreamer video path — the original
  "re-muxed unchanged" claim, now annotated
- libmisbklv ADR 0023 (fork 21), commits `e3c7f4a` and `032db10`; pin history in
  [[log]] § 2026-07-27. Upstream rewrote history on 2026-07-27, so this ADR's
  original citation (`f6d52e4`) is orphaned — `032db10` is the same change under
  its post-rewrite hash.
