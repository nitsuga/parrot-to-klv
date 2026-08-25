---
type: Decision
title: ADR 0007 — H.265 sources versus ST 0604 generation, which now excludes them
description: Asking libmisbklv for ST 0604 SEI refuses any non-H.264 source, so ask for it only on H.264 and carry anything else through unstamped rather than refusing it
decision_status: accepted
fork: 7
tags: [decision, mux, video, h264, h265, sei, misb, 0604, phase-3]
generated:
  by: claude/opus-5
  at: 2026-07-28T00:00:00Z
sources:
  - id: libmisbklv-f8cc8c5
    resource: https://github.com/nitsuga/libmisbklv/commit/f8cc8c5cd94d6cda8a6648fcf73e9edc79d1df53
    title: "libmisbklv — Derive Time Status bits 6/5; refuse Generate on non-H.264"
---

# Context

[[0006-inherited-st0604-sei-injection]] settled that we want ST 0604 in the
video, and its 2026-07-28 revision made that explicit: `mux_to_ts()` now passes
`misbklv::Sei0604::Generate` rather than inheriting generation from upstream's
default.

Upstream then made `Generate` **H.264-only**.[^libmisbklv-f8cc8c5] In
`on_video_pad_added`, a non-H.264 video pad under `Generate` is dropped rather
than linked. Since `mux_to_ts()` asked for `Generate` unconditionally, **an
H.265 recording did not convert at all**. Reproduced on
`parrot_v3_video_h265.mp4` by forcing `Generate`:

```
WARNING: misbklv: Sei0604::Generate needs H.264 video; source carries video/x-h265
Conversion failed: could not mux frame 0: media pipeline failed
```

Note where that lands — the failure surfaces on the *first emit* as
`Error::Backend`, not at open as `Error::Unsupported`. The reason reaches only
stderr, as a `g_warning`; the caller gets "media pipeline failed", which names
neither the codec nor the cause.

That collides with a capability this project has claimed since Phase 3, in four
places:

| Where | Claim |
|---|---|
| `planning/PROGRESS.md` § Now | "parsed, never decoded, so H.264 and H.265 both work" |
| [[mux-stage]] § Video is never decoded | "H.264 from the ANAFI range, **H.265 from an Ai**" |
| `src/mux.hpp` § `mux_to_ts` doc comment | "holds the same H.264/H.265 the drone recorded" |
| [[0006-inherited-st0604-sei-injection]] § Decision | "the same H.264/H.265 the drone recorded" |

The pin-bump log entry that brought this in reasoned "Parrot records H.264, so
this never fires for us in practice." That cannot be reconciled with
[[mux-stage]] § Video is never decoded — one of the two is wrong, and which one
decides how much this matters. The ANAFI Ai is squarely in scope: ROADMAP names "ANAFI Ai / UKR", the
proto parser exists for it, and Parrot's Embedded Video Metadata documentation
(version 8.4.2, cited via `references/parrot-video-metadata.md`)
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

**Ask for `Generate` only when the source is H.264, and `Sei0604::Preserve`
otherwise.** H.265 keeps converting and loses only the ST 0604 stamps that
cannot be generated for it anyway — the guarantee degrades exactly where it
cannot be met, and nowhere else.

`scan_mp4_tracks()` already returns `sample_entry_format` per track alongside
`handler_type`, so the video track's `avc1` / `hvc1` is in hand before the sink
is built: no new parsing, one lookup and a conditional at the call site.

**Unrecognized codecs take the `Preserve` path too.** The test is "do we know
this is H.264", not "do we know this is not". A codec we have never seen
converts unstamped rather than erroring on a guess, which is the same trade this
ADR makes for H.265.

**The conversion says which path it took.** `MuxReport::st0604_generated` and
`video_sample_entry` carry it, and the CLI prints it on success:

```
… 8 metadata samples, video carried unchanged — no ST 0604: generation needs H.264, source is hvc1
```

That is the answer to the cost below: the ST 0604 guarantee is now conditional
on the input codec, which is a weaker promise than "always". A conditional
guarantee that announces itself per conversion is tolerable; a silent one would
not be, because the failure mode is a client finding no timestamps and having no
idea whether that is a bug or the design.

**Settled by scope, not only by argument.** The ANAFI models this project needs
to support record H.264 only, so the H.265 path is defensive rather than load-
bearing — it exists so an out-of-scope input degrades instead of failing. That
also disposes of the open question this ADR was written around.

# Alternatives considered

## Accept H.264-only conversion

Keep `Generate` unconditional, correct the four claims, and narrow ROADMAP's
scope so H.265 is explicitly unsupported.

The honest version of the status quo, and cheapest by far — documentation only.
It also keeps one unconditional promise ("our output always carries ST 0604")
instead of trading it for two conditional ones, which has real value to a client
that would otherwise need to branch on codec.

**Rejected**, but narrowly, and the margin moved once the in-scope airframes
turned out to be H.264-only: the promise this preserves is worth something, and
the capability it drops is currently hypothetical. What decides it is the shape
of the failure. Under this option an H.265 input dies with "media pipeline
failed" — a message that names neither the codec nor the reason — and the docs
would have to tell people not to try. Under the chosen option it converts, and
the tool says exactly what it did and did not do. Degrading beats refusing when
refusing is this inarticulate.

## Push H.265 ST 0604 upstream

ST 0604.6 defines an H.265 path; libmisbklv's own ROADMAP lists "H.265 Nano" as
remaining Phase 4 work.

The right long-term answer, and it removes the fork entirely rather than
managing it. **Deferred, not rejected**, and explicitly punted: it is the most
work, it is in someone else's repo, no consumer has asked, and the in-scope
airframes do not need it. It also has its own ADR upstream. This decision stays
correct if that work ever lands — the conditional simply stops having a false
branch.

## Leave it as-is

Rejected, but named, because it is what shipped for one pin cycle. The four
claims were false and the suite could not see it. No option that keeps the docs
promising H.265 while the tool refuses it was ever available.

# Consequences

- H.265 conversions produce video with no ST 0604 and no error, so "did this
  output get stamped?" has a per-file answer. `MuxReport::st0604_generated`
  carries it and the CLI prints it, so the answer is never something a consumer
  has to infer from missing bytes.
- `mux_to_ts()` now reads the video track's sample entry, a small widening of
  what the mux stage knows about its input. It reuses the handle already opened
  for session metadata, so it costs no extra I/O.
- A consumer requiring ST 0604 must constrain inputs to H.264 or check per file
  — strictly more than before, when the answer was "always" for anything that
  converted at all.
- **The suite now covers both branches.** `parrot_v3_video_h265.mp4` converts
  and is asserted to carry *no* ST 0604, which is what stops a well-meaning
  change putting `Generate` back unconditionally. Property 4's video-PID guard
  was also made to fail rather than skip when no video stream is found — as
  written it would have passed silently on exactly this regression.
- The H.265 fixture needs ffmpeg with libx265 to *regenerate*, never to use,
  matching the existing H.264 fixtures.

# Assumptions / open questions

- **Resolved: the airframes in scope record H.264 only.** The open question this
  ADR was written around is answered, which is why the H.265 path is defensive
  rather than load-bearing. The `mux-stage.md` claim that an Ai records H.265 is
  therefore not something to rely on; it has been softened to describe what the
  code does (any non-H.264 source is carried unstamped) rather than to assert a
  codec for a specific airframe.
- Assumes upstream will not make `Generate` H.265-capable soon — deliberately
  punted, with its own ADR upstream. If it lands, this decision degrades to a
  redundant conditional rather than to something wrong, which is the right way
  round.
- Whether a client would rather have unstamped H.265 video or a hard refusal is
  still unasked. This guesses that a converted file with an explicit caveat
  beats no file with an opaque error — untested against a real consumer, and
  cheap to revisit, since the branch is one conditional.
- The H.265 fixture is synthetic `testsrc` output, not drone footage. It proves
  the codec path, not that any real Parrot HEVC recording muxes cleanly.

# Citations

- [[0006-inherited-st0604-sei-injection]] § Revision — why we ask for `Generate`
- [[mux-stage]] § Video is never decoded — the corrected description
- libmisbklv `f8cc8c5`, `src/gst/gst_backend.cpp` § `on_video_pad_added` — the
  refusal;
  `parser_for_media_type` for the H.265 passthrough that still works
- `src/mp4_box_scan.hpp` — `Mp4TrackDescription::sample_entry_format`, the codec
  this branches on
- `test/mux_test.cpp` § `check_conversion` — both branches asserted;
  `test/make_fixture.py` § `encode_h265` for the fixture

[^libmisbklv-f8cc8c5]: libmisbklv `f8cc8c5`, `src/gst/gst_backend.cpp` §
    `on_video_pad_added` — refuses `Sei0604::Generate` on any non-H.264 video pad.
