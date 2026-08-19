# Parrot to KLV — Progress

Current status only — **present tense, volatile, rewrite freely**. The
chronological history (what landed when, decision detail) lives in
[`../context/log.md`](../context/log.md); the decided-fork register is
[`../context/decisions/index.md`](../context/decisions/index.md); the plan is
[ROADMAP.md](./ROADMAP.md). This file does not re-narrate the past.

## Now

**Every phase has landed and the pipeline runs end to end** — `parrot-to-klv
in.mp4 -o out.ts` writes an MPEG-TS carrying the recording's own video plus a
synchronous ST 0601 KLV stream. What remains is depth, not plumbing.

What each stage does, and why, is owned by its concept rather than restated
here: [MP4 metadata extraction](../context/mp4-extraction.md),
[transform stage](../context/transform-stage.md),
[mux stage](../context/mux-stage.md), with the choices behind them in the
[decisions register](../context/decisions/index.md). The three tools' user-facing
surface — flags, output, the scrub and conform workflows — is in
[`README.md`](../README.md).

**`v0.1.5` is the current release.** It distributes the video-first PMT stream
order: the output announces the video as stream `0:0` and the KLV as `0:1`, so
`ffmpeg -map 0:0` selects the video. The mechanism is upstream's (libmisbklv
ADR 0020 § Stream order, revised 2026-08-12); `mux_test` pins the order.

Main additionally carries a build-only libmisbklv pin update (`84c7e366`,
upstream's 2026-08-17 review-issue wave — gst error-path hygiene, core codec
hardening) and five merged fixes from an automated review round (issues #3,
#6, #10, #12; see `context/log.md`'s 2026-08-18/19 entries for detail): a
scrub identity/date gap, extractor hardening against hostile sample-index
tables (two PRs — a wrapped bounds check, then NULL-table validation and
allocation caps), transform fabrication/spec-violation fixes, and a mux
invariant restore (signal handling, zero-frame conversions). All are
runtime-behavior changes and together are release-worthy; no release has
been cut for them yet.

Extractor hardening (issue #3) found a further, unfixed heap-overflow-write
risk inside vendored `external/minimp4/minimp4.h` itself (`stsz`'s sample
count overflows a 32-bit multiplication before `malloc()`), out of reach of
any wrapper-boundary check. Patching vendored code is a real decision, not
a routine fix — filed as its own issue rather than folded in quietly.

The one substantive feature piece left is **fork 4, Tier 2 geometry fidelity**
— see [ROADMAP](./ROADMAP.md) § Open forks.

## In progress

- Nothing mid-flight.

## Next

1. Return to fork 4, Tier 2 geometry fidelity, in [ROADMAP](./ROADMAP.md).

## Known gaps

- **Non-H.264 output carries no ST 0604.** Generation is H.264-only upstream, so
  an H.265 source converts but is unstamped
  ([ADR 0007](../context/decisions/0007-h265-vs-st0604-generation.md)). Moot for
  the airframes in scope, which record H.264; it matters only to a consumer
  reading time from the video of an out-of-scope recording, and both the report
  and the CLI say when it happened.
- Deriving Items 21/22/23/24/25/57/78/82–89 needs a boresight-to-terrain
  intersection — the substantive remaining work in the transform stage, and the
  one part of the mapping with no direct Parrot source. Its fidelity is fork 4,
  now open in [ROADMAP](./ROADMAP.md).
- **The bundle is x86-64 only, and CI can only prove 24.04.** 22.04 rests on the
  glibc floor — a sound argument, not a test. It also pins a build-time
  GStreamer: host security updates do not reach it, a rebuild does
  ([ADR 0009](../context/decisions/0009-release-bundle.md) § Consequences).
- **The `IR` half of `--infer-image-source` has never met a thermal recording.**
  The `EO` half is exercised by fixtures and by real EO flights; that thermal
  imagery always writes the thermal extension is reasoning about the format, not
  evidence from it
  ([ADR 0011](../context/decisions/0011-inferred-image-source-sensor.md)
  § Assumptions). Blended EO/IR footage is knowingly named `IR`.
- Item 2 for v3 recordings is anchored to session `media_date`, so it inherits
  that field's **second** granularity. Frame-to-frame spacing stays exact.
- The mux has run on real footage on an authorized machine — that is what found
  the audio-track gap the fixtures now model. The suite here still only ever
  converts synthetic input, and the fixture cannot reproduce that bug on
  gstreamer 1.20; CI's newer stack is the guard. See
  [mux stage](../context/mux-stage.md) § The fixtures this needed.
- The proto parser has never seen a real recording, and its fixtures are encoded
  by our own hand-written encoder rather than a reference implementation — see
  [ADR 0003](../context/decisions/0003-protobuf-dependency.md) § Validation.
