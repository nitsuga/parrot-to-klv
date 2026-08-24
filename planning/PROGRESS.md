# Parrot to KLV — Progress

Current status only — **present tense, volatile, rewrite freely**. The
chronological history (what landed when, decision detail) lives in
[`../context/log.md`](../context/log.md); the decided-fork register is
[`../context/decisions/index.md`](../context/decisions/index.md); the plan is
[ROADMAP.md](./ROADMAP.md). This file does not re-narrate the past.

## Now

**Ctrl-C now cuts the file→live replay drain short (#63):** `replay_file_to_live` bridges the SIGINT flag to a `stop_source` and passes its token to every `KlvSink::close()`; the libmisbklv pin carries the cancellable `close()` (upstream ADR 0032), the Generate encoder-shift fix (ADR 0033), and the SEI-probe teardown fix (upstream #38 — closes the live-RTSP Generate teardown use-after-free, this repo's #57). The pipeline runs end to end — `parrot-to-klv in.mp4 -o out.ts` writes an MPEG-TS carrying the recording's own video plus a synchronous ST 0601 KLV stream. What remains is depth, not plumbing.

What each stage does, and why, is owned by its concept rather than restated
here: [MP4 metadata extraction](../context/mp4-extraction.md),
[transform stage](../context/transform-stage.md),
[mux stage](../context/mux-stage.md), with the choices behind them in the
[decisions register](../context/decisions/index.md). The three tools' user-facing
surface — flags, output, the scrub and conform workflows — is in
[`README.md`](../README.md).

**`v0.2.2` is the current release.** This patch pins libmisbklv's deterministic
SEI-probe teardown fix (closes the live-RTSP Generate teardown use-after-free,
#57 / upstream #38), ships the release bundle with its live-path GStreamer
plugins so `-o udp://…`/`srt:…` and `-i rtsp://…` work from the wrapper (#62),
and makes Ctrl-C interrupt a file→live replay promptly (#63). User-facing
formats and CLI remain unchanged; the live-streaming surface introduced in
`v0.2.0` is as documented in the README. The teardown heap-corruption residual that
`v0.2.2` left open on #57 is now attributed and fixed upstream
(libmisbklv#40 — `GstInserter::finish` released the `mpegtsmux` request pad
mid-`PLAYING`); #57 is closed and the pin bump carrying that fix is in flight.

**`v0.1.6` delivered** every fix merged since
`v0.1.5` — an automated review round (issues #3, #4, #6, #7, #10, #12, #13,
#14, #15) plus the re-vendored `minimp4.h` overflow fix (issues #27/#28, ADR
0012):

- **Truthfulness in the transform**: no-fix frames no longer report a confident
  0°N 0°E, AGL and Item 124 are not fabricated, NaN attitude is invalid not
  out-of-range, and a proto frame that declared its own UTC invalid is no
  longer stamped with an invented Item 2.
- **Strict, fail-closed CLIs**: unknown flags, missing values, and extra
  positionals are errors across all three tools; `parrot-scrub`'s coordinate
  options are strictly parsed and range-checked; `parrot-conform --scan`
  reports filesystem errors by name instead of a bogus "No recordings" or a
  multi-terabyte size.
- **A clean, complete output**: an interrupted or zero-frame conversion leaves
  no half-written `.ts`.
- **Extraction hardening**: malformed ISO 8601 offsets/dates, hostile `mdhd`
  durations, unknown `tkhd` versions, wrapping box sizes, and missing/invalid
  sample tables are rejected rather than mis-parsed.

The post-`v0.1.6` hardening (including the CI work from PR #39), now shipped in
`v0.2.0`: v3 decode
validity (unit-norm quaternions, exact declared length, short extensions
recorded as skipped — issues #8/#9), strict `stsd` MIME-type parameter parsing
(issue #5), output-write reliability (scrub/conform writes are
`fwrite`-checked and a failed write unlinks the output; the mux preflights its
destination — issues #11/#19/#17), and a CLI end-to-end suite that runs the
real binary and asserts the `KLVA` descriptor and ST 0604 SEI in its output.
The CI half (PR #39): the mux and CLI now run under ASan/UBSan, the
conformance decode-failure report path is pinned, the packaging guards now
cover the staging directory itself and protected-input overlap, the
`parrot-sanitize` label means compiled-C++ tests only, and the audio fixture
is pinned to ffmpeg 6.1.1 (byte-reproducible). Detail — files, tests, errors —
in the 2026-08-20 [log](../context/log.md) entry.

The output MPEG-TS keeps the video-first PMT stream order introduced in
`v0.1.5`: the video is stream `0:0` and the KLV `0:1`, so `ffmpeg -map 0:0`
selects the video. (The mechanism is upstream's, libmisbklv ADR 0020 § Stream
order; `mux_test` pins it.)

Live streaming (fork 13 / [ADR 0013](../context/decisions/0013-live-streaming-ingest.md)) is **accepted** and now exposed via a unified `-i`/`-o` CLI ([ADR 0014](../context/decisions/0014-unified-cli-input-output-inference.md)): `-i rtsp://… -o <path|udp://…|srt:…>` reaches `live_ingest` with file or UDP/SRT, `path→path` stays deterministic `mux_to_ts`, and `pipeline:` remains a `PARROT_TEST_ALLOW_PIPELINE`-gated test escape hatch behind `-i`. The `path→udp`/`srt:` file-replay bridge is **landed per [ADR 0015](../context/decisions/0015-file-replay-live-output.md)** (paced realtime replay, one play-through or `--repeat` loop) — `-i file.mp4 -o udp://…` and `-i file.mp4 -o srt:…` now replay the recording on the pipeline clock.

Build presets are consolidated under `build/` via `CMakePresets.json` (v3, CMake ≥3.21 for the `ubuntu-22.04` release runner) — `release`/`default` → `build/release` (Release), `debug` → `build/debug` (Debug), `sanitize` → `build/sanitize` (Debug + `PARROT_SANITIZE=ON`, used by `build.yml`'s sanitizers job / `ctest -L parrot-sanitize`), `release-bundle` → `build/bundle` (Release + `PARROT_BUNDLE=ON`, used by `release.yml`); parallel builds via a hidden `base` (`jobs=6`, `nproc 7 → 6`, `nproc-1` headroom). `build.yml` and `release.yml` configure/build through these same presets rather than ad hoc flags.

The one substantive feature piece left is **fork 4, Tier 2 geometry fidelity**
— see [ROADMAP](./ROADMAP.md) § Open forks.

## In progress

- None — the live surface is complete. File replay (`path→udp`/`srt:`) is landed; the unified CLI covers all four `input × output` cells.

## Next

1. Fork 4, Tier 2 geometry fidelity, in [ROADMAP](./ROADMAP.md).

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
