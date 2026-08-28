# Parrot to KLV — Progress

Current status only — **present tense, volatile, rewrite freely**. The
chronological history (what landed when, decision detail) lives in
[`../context/log.md`](../context/log.md); the decided-fork register is
[`../context/decisions/index.md`](../context/decisions/index.md); the plan is
[ROADMAP.md](./ROADMAP.md). This file does not re-narrate the past.

## Now

**MP4 track pairing fails closed (#87):** `Mp4MetadataExtractor::open()` rejects
disagreement between the first-`moov` box scan and minimp4's track count, then
verifies every indexed handler type before using the metadata index. A synthetic
foreign-top-level-`trak` fixture covers the shifted-index case that could make
`--zero-media` clear the wrong track.

**Live KLV emission failures are returned (#86):** a runtime `KlvSink::emit`
failure now terminates `live_ingest` with the original backend error instead of
being treated as a skipped packet and leaving the drive loop to spin until its
deadline or SIGINT. Pipeline EOS/ERROR is still not independently observed;
the no-telemetry failure path is tracked in #94.

**Generated KLV parse failures are returned (#95):** live v3 and proto telemetry
now terminate `live_ingest` with the original `Message::parse` error when a
packet produced by `to_0601` cannot be parsed. Ordinary decode misses and
truncated proto coalescing remain skips.

**Absent v3 data remains absent (#84):** zero-sentinel automation targets and
missing per-frame timestamps are omitted rather than turned into target or
session-start measurements; coverage distinguishes absent frame data from
missing anchor origin and missing session metadata, and a mixed first-frame
fixture pins the no-origin behavior.

**Item 2 session anchoring fails closed (#85):** pre-epoch dates are rejected,
legal basic and hour-only ISO 8601 offsets are accepted, and regression coverage
keeps malformed dates from becoming wrapped timestamps.

**Default v3 scrubbing fails closed on malformed extensions (#83):** short and
oversized automation payloads plus truncated extension tails are reported and
cleared, with synthetic regression fixtures; valid extension behavior is
unchanged.

**Ctrl-C now cuts the file→live replay drain short (#63):** `replay_file_to_live` bridges the SIGINT flag to a `stop_source` and passes its token to every `KlvSink::close()`; the libmisbklv pin carries the cancellable `close()` (upstream ADR 0032), the Generate encoder-shift fix (ADR 0033), and both halves of the live-RTSP Generate teardown use-after-free (this repo's #57): the SEI-probe fix (upstream #38) and the request-pad drain that closed the residual (upstream #40). The pipeline runs end to end — `parrot-to-klv in.mp4 -o out.ts` writes an MPEG-TS carrying the recording's own video plus a synchronous ST 0601 KLV stream. What remains is depth, not plumbing.

What each stage does, and why, is owned by its concept rather than restated
here: [MP4 metadata extraction](../context/mp4-extraction.md),
[transform stage](../context/transform-stage.md),
[mux stage](../context/mux-stage.md), with the choices behind them in the
[decisions register](../context/decisions/index.md). The three tools' user-facing
surface — flags, output, the scrub and conform workflows — is in
[`README.md`](../README.md).

**`v0.3.0` is the current release.** Native x86-64 and `aarch64` bundles and
container images ship together, each built and smoke-tested on its own
architecture. The Ubuntu 22.04 compatibility floor now comes from the shared
digest-pinned Jammy build container rather than a retiring runner label, while
the GLIBC symbol check continues to enforce 2.35. Artifact filenames, inner
image archives, and image tags carry the `uname -m` architecture spelling. CLI
behavior and output formats are unchanged.

**`v0.2.2` delivered** the libmisbklv deterministic SEI-probe teardown fix
(the first half of the live-RTSP Generate use-after-free, #57 / upstream #38),
the release bundle's live-path GStreamer plugins so `-o udp://…`/`srt:…` and
`-i rtsp://…` work from the wrapper (#62), and prompt Ctrl-C interruption of a
file→live replay (#63). User-facing formats and CLI remain unchanged; the
live-streaming surface introduced in `v0.2.0` is as documented in the README.

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

Build presets are consolidated under `build/` via `CMakePresets.json` (v3, CMake ≥3.21 for the `ubuntu-22.04` release runner) — `release` → `build/release` (Release), `debug` → `build/debug` (Debug), `sanitize` → `build/sanitize` (Debug + `PARROT_SANITIZE=ON`, used by `build.yml`'s sanitizers job / `ctest -L parrot-sanitize`), `release-bundle` → `build/bundle` (Release + `PARROT_BUNDLE=ON`, used by `release.yml`); parallel builds via a hidden `base` (`jobs=6`, `nproc 7 → 6`, `nproc-1` headroom). `build.yml` and `release.yml` configure/build through these same presets rather than ad hoc flags.

The one substantive feature piece left is **fork 4, Tier 2 geometry fidelity**
— see [ROADMAP](./ROADMAP.md) § Open forks.

## In progress

- **Preparing to make the repository public.** The driver is multi-architecture
  releases: GitHub's free arm64 hosted runners and unmetered Actions minutes are
  public-repo benefits — though **not** arm64 itself, which GitHub made
  available to private repositories in January 2026. libmisbklv went public on
  2026-08-25, so `FetchContent` now clones it over anonymous HTTPS and the
  private-dependency deploy key both CI workflows carried is retired; neither
  workflow reads a secret. Parrot's documentation snapshot was not
  redistributable and has been withdrawn from `references/` in favor of a link
  file plus the schema vendored under BSD-3-Clause
  ([ADR 0016](../context/decisions/0016-references-licensing-withdrawal.md)), and
  the history behind it has been rewritten and force-pushed, so no branch or tag
  reaches the withdrawn objects. **The one remaining blocker is not ours to
  clear**: GitHub's `refs/pull/*` refs still resolve to pre-rewrite commits, and
  only GitHub Support can purge them along with the cached objects and the
  generated tag archives. The flip waits on that purge being confirmed.
- **The over-engineering audit cleanup is complete (#81, #82).** The live drive
  loops and in-memory MP4 box reader are deduplicated; C++20 string prefix
  checks replace three private helpers; one bounded-FIFO insertion owns the
  live RTP queue invariant; coverage reports share their item-label formatter;
  and unused preset and GStreamer compatibility aliases are gone. Behavior is
  unchanged.
- The live surface is complete: file replay (`path→udp`/`srt:`) is landed and the
  unified CLI covers all four `input × output` cells.

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
