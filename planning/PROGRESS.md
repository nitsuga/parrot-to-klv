# Parrot to KLV — Progress

Current status only — **present tense, volatile, rewrite freely**. The
chronological history (what landed when, decision detail) lives in
[`../context/log.md`](../context/log.md); the decided-fork register is
[`../context/decisions/index.md`](../context/decisions/index.md); the plan is
[ROADMAP.md](./ROADMAP.md). This file does not re-narrate the past.

## Now

**Scrub altitude bounds validation (#88):** `parrot-scrub` range-checks `--alt`
against `[-32768, 32767.99609375]` meters matching the Q16.8 base frame upper bound,
rejecting out-of-range or non-finite altitudes with a clear diagnostic instead of
silently emitting 0 m via `std::lround()` undefined behavior. `scrub_file()`
validates coordinates and finiteness fail-closed at the API boundary, with full
CLI, fractional (`32767.5`), and boundary unit test coverage.

**Proto absent-vs-zero policy extended (#115):** absent protobuf attitude defaults
to NaN rather than identity, omitting Items 5/6/7/90/91/18/19/20 as `InvalidSourceValue`
instead of emitting 0° straight-and-level; pixel-only LFIC entries without coordinates
omit target Items 40/41/42/45/46 via optional field 3 presence tracking instead of
emitting `0,0,0`; and unusable proto UTC timestamps report Item 2 omission as
`InvalidSourceValue` rather than `NeedsSessionData`.

**Upstream libmisbklv pin bumped to `e5f0ccd`:** brings in stalled-video
timestamp handling hardening with queue eviction and drop diagnostics (libmisbklv #75)
and truncated long-form BER error reporting.

**Live Compose setup input and dotenv quote escaping hardened (#131):**
interactive `setup-live` prompts reject control characters and newlines fail-closed,
preventing key injection into `.env`, while `quote_env` corrects sed escaping so single
quotes within RTSP credentials cleanly escape for Docker Compose.

**MP4 container scanning bounds and 64-bit file offsets hardened (#129):**
subtraction-based bounds checks eliminate signed integer overflow UB on hostile
64-bit largesize boxes and prevent backward seek loops, while project-wide
`_FILE_OFFSET_BITS=64` and `fseek64`/`ftell64` enable seeking and extraction on
media files larger than 2 GiB.

**CLI disclosure and file-output routing are fail-closed (#128):** takeoff
geotags are location data, disclosed only with `--with-location`, while serials
remain identity data. `-o file:<path>` or `file://<path>` normalizes to the
requested filesystem path before routing to mux or live output; an empty URI
is rejected. CLI integration coverage pins both.

**Live test waits and replay cancellation are event-driven (#130):**
millisecond test timing seams cut the slow idleness/reconnect waits while live
defaults remain 10 seconds and 3 seconds. The RTP FIFO notifies the ingest
loop on arrival, and replay's cancellation bridge blocks on a signal-safe,
close-on-exec pipe rather than waking every 10 ms. The remaining live-test time
is real codec and pipeline validation.

**Long-running live sessions ship guided Compose setup (#125):** the container
deliverable stages `compose.yml`, `.env.example`, and `setup-live` through
`make-image.sh` (tag baked in as the `IMAGE_TAG` default), an `ingest` service
capturing RTSP with reconnect/idle-timeout/empty-session bounds from the
environment into a mounted captures directory. The service runs as the caller (`user:
HOST_UID:HOST_GID`, required with no fallback) and relaunches after any exit
(`restart: unless-stopped`) so it runs at boot as a service — the internal
`--reconnect` loop owns stream recovery between sessions while Compose owns
availability across runs, and the empty-session bound covers one process
lifetime; the
operator pre-creates `OUTPUT_DIR`; Compose refuses to create it as root.
`RTSP_URI` is required at render time, and the service requires Linux host
networking so `--iface` can select a real host NIC for multicast egress.
`setup-live` is the quick path: it explains the network/restart/credential
tradeoffs, loads the image, collects the settings, records caller IDs, creates
the directory, writes a mode-0600 `.env`, validates Compose, checks daemon
reachability, and can start it.
Re-running it handles upgrades: it can retain existing `.env` settings while
clearing a confirmed stale image pin, reload the bundled image, confirm and
gracefully replace a matching existing service, optionally remove older
same-architecture image tags, and leave captures untouched.
`compose stop` (SIGTERM) drains like Ctrl-C — both signals share one handler,
so no signal change was needed — with a 330-second grace covering the
300-second upstream finish-drain bound. Validated by a staged render check in
`smoke-image` (no `@VAR@` left, image identity, local-only pull policy, required
values, output-path refusal, restart/user/host networking asserted) and a
daemon-free guided-setup test; usage lives in
the docker USAGE.

**v3 no-fix frames omit their position (#114):** `from_v3` gates the sensor
latitude, longitude, and altitude on `sv_count > 0`, so the pre-fix opening of a
flight reports no Items 13/14/15 rather than a confident 0°N 0°E at sea level.
Altitude gates with the pair because v3's is GPS AMSL/EGM96 carried in the same
word as the count, not a barometric reading. This extends to v3 the
absent-vs-zero policy the proto sensor path already applied; the remaining
proto-side gaps are #115.

**Dual Parrot metadata tracks now warn (#116):** `Mp4MetadataExtractor::open` counts Parrot `mett` tracks in one traversal and warns when more than one is present — `Warning: found N Parrot metadata tracks; using track X and ignoring Y other(s)` — while still selecting the first match. A synthetic `parrot_v3_dual_mett.mp4` fixture and hardened test (RAII stderr capture, negative single-mett case, `ignoring` clause) cover it; converting both tracks remains out of scope.

**v3 sentinel sv_count 0xFF is absent, not 255 NAVSATs (#118):** `VMETA_LOCATION_INVALID_SV_COUNT` in `references/vmeta.h` at `22f715a` decouples count from `valid` — `0xFF` means count unavailable, not "no fix". v3 offers no validity signal other than `sv_count`, so `0xFF` leaves nothing to trust — fail closed: `from_v3` gates position on `sv_count != 0 && sv_count != 0xFF` and leaves `sv_count` absent, so Items 13/14/15 and 123/124 are `SourceAbsent` instead of 255/0x02. Cost: a real fix whose count is unreported loses position. Covered alongside 0 and 300.

**MP4 track pairing fails closed (#87):** `Mp4MetadataExtractor::open()` rejects
disagreement between the first-`moov` box scan and minimp4's track count, then
verifies every indexed handler type before using the metadata index. A synthetic
foreign-top-level-`trak` fixture covers the shifted-index case that could make
`--zero-media` clear the wrong track.

**Live KLV emission failures are returned (#86):** a runtime `KlvSink::emit`
failure now terminates `live_ingest` with the original backend error instead of
being treated as a skipped packet and leaving the drive loop to spin until its
deadline or SIGINT.

**Live reconnect mode (#101):** `--reconnect` finalizes each session and polls
the source until it returns, one numbered capture per sortie for a file sink and
a reused address for a live one
([ADR 0018](../context/decisions/0018-live-eos-modes.md) mode 2). Whether to
retry is decided by libmisbklv's `SourceUnavailable` rather than inferred from
the source scheme, so a configuration that cannot run stops the run instead of
spinning.

**Live runs end when the source goes idle (#100):** `live_ingest` ends the
session when no video has been delivered for `--idle-timeout` seconds
(default 10, `0` disables), the primary end-of-source signal under
[ADR 0018](../context/decisions/0018-live-eos-modes.md). The signal is
`KlvSink::last_video_delivery`, so a video-only stream stays up as long as
its video flows, and a reconnect session counts as non-empty when it delivers
video even with no telemetry.

**Live pipeline terminal failures are returned (#94):** `live_ingest` polls
libmisbklv's nonblocking `KlvSink::poll()` and returns a backend error
promptly, including when no telemetry has arrived; timed and explicit
indefinite modes remain available. The contract is failure-only — a live insert
cannot observe a clean EOS before `close()` sends it (libmisbklv ADR 0035).

**Generated KLV parse failures are returned (#95):** live v3 and proto telemetry,
as well as file-to-live replay, now terminate with the original
`Message::parse` error when a packet produced by `to_0601` cannot be parsed.
Ordinary decode misses and truncated proto coalescing remain skips.

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

**Ctrl-C now cuts the file→live replay drain short (#63):** `replay_file_to_live` bridges the SIGINT flag to a `stop_source` and passes its token to every `KlvSink::close()`; the libmisbklv pin carries the cancellable `close()` (upstream ADR 0032), the Generate encoder-shift fix (ADR 0033), and both halves of the live-RTSP Generate teardown use-after-free (this repo's #57): the SEI-probe fix (upstream #38) and the request-pad drain that closed the residual (upstream #40). The pipeline runs end to end — `parrot-to-klv in.mp4 -o out.ts` writes an MPEG-TS carrying the recording's own video plus a synchronous ST 0601 KLV stream.

What each stage does, and why, is owned by its concept rather than restated
here: [MP4 metadata extraction](../context/mp4-extraction.md),
[transform stage](../context/transform-stage.md),
[mux stage](../context/mux-stage.md), with the choices behind them in the
[decisions register](../context/decisions/index.md). The three tools' user-facing
surface — flags, output, the scrub and conform workflows — is in
[`README.md`](../README.md).

**`v0.5.0` is the current published release.** Native x86-64 and `aarch64`
bundles and container images ship together, each built and smoke-tested on its own
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
