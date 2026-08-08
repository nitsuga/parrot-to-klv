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

**`v0.1.3` is the current release.** Main additionally carries a build-only
libmisbklv pin update; it does not change the tool's runtime behavior and does
not warrant a release by itself.

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
- **The output TS announces the KLV as stream 0 and the video as stream 1**, so
  a consumer selecting by index (`ffmpeg -map 0:0`) gets the metadata stream.
  Upstream's muxer orders the PMT by pad-request order and cannot reverse it
  without dropping the KLV or breaking ST 0604 timing (libmisbklv ADR 0020
  § Stream order), so this stands. **Select by stream type.** `mux_test` pins it,
  because an attempt to change it silently emptied our output once.
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
