---
type: Decision
title: ADR 0007 — H.265 sources versus ST 0604 generation, which now excludes them
description: Asking libmisbklv for ST 0604 SEI refuses any non-H.264 source, so H.265 recordings no longer convert at all — decide whether to keep H.265 support, drop it, or push the gap upstream
status: proposed
fork: 7
tags: [decision, mux, video, h264, h265, sei, misb, 0604, phase-3]
timestamp: 2026-07-28T00:00:00Z
---

# Context

[[0006-inherited-st0604-sei-injection]] settled that we want ST 0604 in the
video, and its 2026-07-28 revision made that explicit: `mux_to_ts()` now passes
`misbklv::Sei0604::Generate` rather than inheriting generation from upstream's
default.

Upstream then made `Generate` **H.264-only**. In `gst_backend.cpp:569`, a
non-H.264 video pad under `Generate` is dropped rather than linked; `linked`
stays false and `open_insert` returns `Error::Unsupported` (line 1003). Since
`mux_to_ts()` asks for `Generate` unconditionally, **an H.265 recording no
longer converts at all** — it fails at open.

That collides with a capability this project has claimed since Phase 3, in four
places:

| Where | Claim |
|---|---|
| `planning/PROGRESS.md:34` | "parsed, never decoded, so H.264 and H.265 both work" |
| `context/mux-stage.md:33` | "H.264 from the ANAFI range, **H.265 from an Ai**" |
| `src/mux.hpp:31` | "holds the same H.264/H.265 the drone recorded" |
| [[0006-inherited-st0604-sei-injection]] § Decision | "the same H.264/H.265 the drone recorded" |

The pin-bump log entry that brought this in reasoned "Parrot records H.264, so
this never fires for us in practice." That cannot be reconciled with
`mux-stage.md:33` — one of the two is wrong, and which one decides how much this
matters. The ANAFI Ai is squarely in scope: ROADMAP names "ANAFI Ai / UKR", the
proto parser exists for it, and `references/Embedded Video Metadata` § 8.4.2
describes Parrot timing as living in "H.264 picture timing SEI or H.265 time
code SEI" — which reads as Parrot shipping both codecs.

Two things make this hard to see. Upstream carries H.265 perfectly well
(`parser_for_media_type` returns `h265parse`); it is only the `Generate` opt-in
that excludes it, so each upstream change is reasonable alone and they interact
badly here. And **no test covers it** — every fixture is H.264, so the suite is
green on a capability that no longer works.

Not in question: the failure is loud, not silent. It errors at open rather than
writing timestamp-free video. But the error is generic `Error::Unsupported`,
indistinguishable from "unparseable source, or no video stream in it"; the
specific reason reaches only stderr, as a `g_warning`. (Upstream's
`sei_codec_unsupported` flag, which presumably meant to carry it, is set and
never read.)

# Decision

**Proposed, not settled** — this ADR opens fork 7 rather than closing it.

The proposal is to **ask for `Generate` only when the source is H.264, and
`Sei0604::Preserve` otherwise**: H.265 keeps converting, and loses only the ST
0604 stamps that cannot be generated for it anyway. It degrades exactly the
guarantee that cannot be met, where it cannot be met, and nothing else.

This is cheap. `scan_mp4_tracks()` already returns `sample_entry_format` per
track alongside `handler_type`, so the video track's `avc1` / `hvc1` is in hand
before the sink is built — no new parsing, one lookup and a conditional at the
call site.

The cost is that the ST 0604 guarantee becomes conditional on the input codec,
which is a worse promise than "always". The coverage report should say so per
conversion rather than leaving a client to discover it by finding no SEI, and
the alternative below argues that a quieter conditional guarantee is worse than
an honest narrower one.

# Alternatives considered

## Accept H.264-only conversion

Keep `Generate` unconditional, correct the four claims, and narrow ROADMAP's
scope so H.265 is explicitly unsupported.

The honest version of the status quo, and cheapest by far — documentation only.
It also keeps one unconditional promise ("our output always carries ST 0604")
instead of trading it for two conditional ones, which has real value to a client
that would otherwise need to branch on codec.

Against it: it silently drops a capability the tool had, for in-scope hardware,
because of an upstream implementation limit rather than anything we decided. If
the ANAFI Ai does record H.265, this makes ANAFI Ai footage unconvertible — a
strange outcome for a project whose proto parser exists to serve exactly that
airframe.

## Push H.265 ST 0604 upstream

ST 0604.6 defines an H.265 path; libmisbklv's own ROADMAP lists "H.265 Nano" as
remaining Phase 4 work.

The right long-term answer, and it removes the fork entirely rather than
managing it. But it is the most work, it is in someone else's repo, and no
consumer has asked for H.265 output yet. Better sequenced after this fork is
resolved, not instead of it — whichever option lands here stays correct if
upstream later gains H.265 support.

## Leave it as-is

Rejected — but named, because it is what currently ships. The four claims are
false today, and the suite cannot see it. Whatever else is decided, the docs
cannot keep promising H.265 while the tool refuses it at open.

# Consequences

Under the proposal:

- H.265 conversions produce video with no ST 0604 and no error, so "did this
  output get stamped?" becomes a question with a per-file answer. The conversion
  report should state it explicitly.
- `mux_to_ts()` gains a codec dependency it does not have today — a small
  widening of what the mux stage needs to know about its input.
- A consumer requiring ST 0604 must either constrain its inputs to H.264 or
  check per file. That is strictly more than they must do today, where the
  answer is "always" for the inputs that work at all.

Under any option:

- The four claims above need correcting, in wording that matches the resolution.
- **An H.265 fixture is needed either way** — to prove conversion works
  (proposal), or to prove it is refused cleanly and for the stated reason
  (H.264-only). `make_fixture.py` already builds real-H.264 fixtures with
  ffmpeg, so an `hvc1` variant is a small extension of existing machinery.

# Assumptions / open questions

- **Does the ANAFI Ai actually record H.265?** The whole weight of this fork
  rests on it, and the claim traces to `mux-stage.md` rather than to a datasheet.
  `references/` mentions H.265 SEI for Parrot generally. Worth confirming from a
  real Ai recording before choosing — if the Ai records H.264, this collapses to
  a documentation fix.
- Assumes upstream will not make `Generate` H.265-capable soon. If that lands,
  the proposal degrades to a redundant conditional rather than to something
  wrong, which is the right way round.
- Whether a client would rather have unstamped H.265 video or a hard refusal is
  unknown; nobody has been asked. The proposal guesses that a converted file
  beats no file.

# Citations

- [[0006-inherited-st0604-sei-injection]] § Revision — why we ask for `Generate`
- [[mux-stage]] § Video is never decoded — the H.265 claim under review
- libmisbklv `f8cc8c5`, `gst_backend.cpp:569` (the refusal) and `:1003`
  (`Error::Unsupported`); `parser_for_media_type` for the H.265 passthrough that
  still works
- `src/mp4_box_scan.hpp` — `Mp4TrackDescription::sample_entry_format`, the codec
  the proposal would branch on
