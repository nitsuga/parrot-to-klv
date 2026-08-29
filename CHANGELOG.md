# Changelog

What changed for someone *using* the tool. Written by hand, because the commit
list is not a changelog — it carries reverts, refactors and internal churn that
tell a user nothing.

The release workflow publishes the section matching the tag, so a heading here
must be exactly `## vX.Y.Z`.

## v0.4.0

This release makes live capture practical for unattended and intermittent
sources, and tightens failure handling for malformed recordings and runtime
errors. File-to-file conversion and the MPEG-TS stream layout are unchanged.

### Added

- **Live runs finish when a source goes idle.** An RTSP capture now ends after
  `--idle-timeout` seconds without Parrot telemetry (default 10); set it to `0`
  to wait for `SIGINT`/`SIGTERM` instead. An unattended capture can therefore
  finalize when a sortie ends instead of idling against a dead pipeline.

- **Reconnect across repeated sorties.** `--reconnect` keeps one invocation
  polling for a source every `--reconnect-interval` seconds (default 3),
  finalizes each session, and starts a fresh capture when the source returns.
  File outputs are numbered (`flight_0000.ts`, `flight_0001.ts`, …) without
  overwriting captures already on disk; UDP/SRT outputs reuse their address.
  `--max-empty-sessions` (default 5, or `0` to disable) stops a run after that
  many opened sessions carry no telemetry, while a source that is simply absent
  can remain unavailable until it returns.

### Changed

- **Live failures are no longer hidden.** Terminal pipeline failures, KLV
  emission failures, and parse failures in generated KLV now return their
  original errors and stop the run instead of being treated as skipped data or
  allowing the drive loop to spin.

- **Reconnect retries distinguish absence from broken configuration.** The
  live dependency now reports an unavailable source explicitly, so reconnect
  waits for an absent source but stops on a missing plugin, invalid sink, or
  other startup error rather than retrying it indefinitely.

- **Malformed metadata fails closed.** Track-pairing disagreements, malformed
  v3 extensions, missing v3 timestamps, and invalid session-date anchors are
  rejected or reported as absent instead of being mapped to a plausible target,
  timestamp, or partially read value. Valid recordings and extensions retain
  their existing behavior.

### Fixed

- **Scrubbing no longer trusts hostile extension sizes.** Oversized automation
  payloads, short payloads, and truncated extension tails are reported and
  cleared rather than being allowed to affect the scrubbed output.

## v0.3.0

This release adds native arm64 downloads alongside x86-64 and makes the release
build independent of GitHub's retiring Ubuntu 22.04 runner labels. CLI behavior
and output formats are unchanged.

### Added

- **Native `aarch64` release deliverables.** Every release now carries a
  self-contained bundle and an exported container image for arm64 as well as
  x86-64. Choose the `x86_64` or `aarch64` filename that matches `uname -m`;
  each artifact is built and smoke-tested on a native runner.

### Changed

- **The Ubuntu 22.04 compatibility floor now comes from a pinned build
  container.** Both architectures build inside the same digest-pinned Jammy
  image on Ubuntu 24.04 hosts, with the existing GLIBC symbol check still
  enforcing the 2.35 ceiling. This preserves Ubuntu 22.04 compatibility without
  depending on runner labels scheduled for retirement.
- **Container image files and tags identify their architecture.** The supplied
  launchers handle this automatically. When running an image directly, use
  `parrot-to-klv:0.3.0-x86_64` or `parrot-to-klv:0.3.0-aarch64`.

### Fixed

- **Bundle assembly no longer intermittently exits with status 141.** SONAME
  discovery now consumes the complete `objdump` stream, so a large shared
  object's remaining output cannot turn successful discovery into a SIGPIPE
  failure under `pipefail`.

## v0.2.3

This patch release removes the last crash on live-session teardown. Output
formats and CLI behavior are unchanged.

### Fixed

- **Ending a live RTSP session with ST 0604 SEI generation no longer crashes,
  in the remaining rare case.** `v0.2.2` fixed one use-after-free on this path
  but left a rarer one behind: shutting down still tripped heap corruption in
  roughly 1 in 60 live sessions under stress. The cause was upstream in
  libmisbklv, which released the muxer's request pad while the pipeline was
  still running, so the muxer thread could walk a freed list node. It now keeps
  that pad in place and ends an unbounded live branch by draining it, so
  teardown is deterministic. As before, sessions that finished normally were
  already writing complete output; this removes the crash itself.

  Verified against the new pin with 560 live runs under memory-checking
  allocators and full CPU load: no failures and no core files, against a
  pre-fix rate of 1.6-5.2%.

## v0.2.2

This patch release fixes a rare crash when a live session ends, makes the
release bundle work for live streaming, and lets Ctrl-C interrupt a replay
promptly. Output formats and CLI behavior are unchanged.

### Fixed

- **No more crash when tearing down a live RTSP session with ST 0604 SEI
  generation.** Ending such a session could occasionally crash on shutdown (a
  use-after-free in the media backend's SEI-injection path). The underlying
  libmisbklv fix is now pinned in, so teardown is deterministic. Sessions that
  finished normally were already writing complete output; this removes the ugly
  shutdown crash.

- **The release bundle now works for live streaming.** The bundled wrapper was
  missing the GStreamer plugins for UDP/SRT output and RTSP input, so
  `-o udp://…`, `-o srt:…`, and `-i rtsp://…` failed from the bundle with
  `file replay failed: 7`. Those plugins are now bundled, and a release smoke
  test asserts each live path loads and runs end to end.

- **Ctrl-C interrupts a file→live replay promptly.** During a paced replay
  (`-i file.mp4 -o udp://…` or `srt:…`), an interrupt arriving after the
  metadata ran out used to wait for the full remaining video duration to drain;
  it now stops promptly.

## v0.2.1

This patch release reduces copying and long-running overhead in the KLV/video
output path. Output formats and CLI behavior are unchanged.

### Changed

- **Lower overhead while generating ST 0604 timestamps.** H.264 negotiation is
  latched rather than queried for every video frame, timestamp lookup state is
  retired as video consumes it without dropping timestamps when KLV runs ahead,
  and reading Item 2 no longer copies the whole packet.

- **Fewer packet copies in streaming output.** The high-level read/edit/write
  pipeline now adopts already-owned packet buffers and transfers edited packets
  into GStreamer without redundant copies.

## v0.2.0

The tool now streams. Alongside converting an MP4 to a file, `parrot-to-klv`
can read a live source and write a live sink — and it works out which is which
from the value you pass, so there are no new mode flags to learn.

### Added

- **Live RTSP ingest.** `parrot-to-klv -i rtsp://<host>/<path> -o <output>`
  reads a live RTSP stream and emits an ST 0601 KLV stream alongside its video,
  running until you stop it with `Ctrl-C`. The output can be a file, or a live
  UDP/SRT sink (below).

- **Replay a recording as a live stream.**
  `parrot-to-klv -i flight.mp4 -o udp://239.0.0.1:5000` (or `-o srt://<host>:<port>`)
  replays the recording's video paced on its own timeline and multicasts it with
  a synchronous KLV stream — the same conversion, delivered live instead of to a
  file. `--repeat` loops the recording until `Ctrl-C`; `--repeat-pts reset|continue`
  selects whether each loop restarts the timeline (default `reset`) or continues
  it. `--iface`, `--ttl`, and `--loop`/`--no-loop` tune the multicast output.

- **Unified `-i`/`--input` and `-o`/`--output` with type inference.** The tool
  infers the kind from the value: a plain path is an MP4 input or an MPEG-TS
  output; `rtsp://`/`rtsps://` is a live source; `udp://`/`udp:`/`srt:` is a live
  sink. A bare input path still works exactly as before, and **file-to-file
  conversion is unchanged and byte-for-byte identical** — it never routes
  through the live path.

### Fixed

- **Stricter, more trustworthy metadata decoding.** Decoded v3 quaternions must
  be unit-norm, an extension's length must match what the frame declares, and a
  short or malformed extension is recorded as skipped rather than half-read. The
  MP4's metadata format is now identified by strictly parsing the track's
  MIME-type parameter instead of a loose match.

- **Output-write reliability.** Writes across all three tools are checked, a
  failed write removes the partial output instead of leaving a truncated file,
  and the muxer preflights its destination before it starts.

## v0.1.6

### Fixed

- **`parrot-scrub` now scrubs session identity and the absolute flight date,
  not just the geotag.** The `©day` flight timestamp, `©too` serial, `©ART`
  friendly name, and `©nam` title all used to survive a scrub untouched —
  a scrubbed video still said who and when. All are now blanked alongside the
  `©xyz` geotag, and the end-of-run warning no longer overclaims what it left
  behind. The `--lat`/`--lon`/`--alt` options are also strictly parsed now:
  an unknown flag, a missing value, an extra positional, a non-numeric value,
  or an out-of-range latitude/longitude is an error instead of a silently
  wrong (or garbage) coordinate baked into every sample.

- **All three tools now refuse malformed command lines instead of quietly
  doing the wrong thing.** Misspelled flags, an option left without its value,
  and unexpected extra positionals are errors everywhere (`parrot-to-klv`,
  `parrot-scrub`, `parrot-conform`). In particular a blank `-o ""` no longer
  silently degrades to inspection mode, and `parrot-conform --label`/`--out`
  no longer silently ignore those flags in `--scan` mode. **If you script any
  of the three, a misspelled flag that used to run to completion now exits
  non-zero.**

- **A conversion no longer leaves a half-written output file.** Interrupting
  (`Ctrl-C`) or a recording that converts to zero frames discards the output
  instead of leaving a truncated `.ts` indistinguishable from a finished one.
  `parrot-to-klv` now handles `SIGINT`/`SIGTERM` so the file is cleaned up on
  interruption.

- **Invalid telemetry no longer becomes a plausible measurement.** A proto
  frame with no position fix no longer reports a confident 0°N 0°E; a
  no-fix frame's ground distance is no longer invented; Item 124 is omitted
  rather than emitted at its forbidden all-zeros encoding; and a NaN attitude
  is reported invalid rather than out-of-range.

- **`parrot-to-klv`'s coverage report now covers the whole recording, not
  just the first frames.** It used to keep the first emitted frame's (or, in
  inspection mode, the first three samples') coverage, so mid-flight drift —
  an attitude sentinel appearing later, a thermal extension starting mid-shot,
  a field dropping in and out — was invisible. It now aggregates every frame
  and calls out any ST 0601 item that was sometimes present and sometimes
  absent across the conversion.

- **Both `parrot-conform --scan` and `parrot-to-klv` reject malformed
  recordings instead of mis-parsing them.** A recording with a bad or
  out-of-range timezone offset, a calendar-impossible date, a hostile
  duration, or an unreadable scan entry is reported by name with the real
  error (in `--scan`, a file that vanishes mid-scan does not stop the rest of
  the directory, and is no longer shown with a multi-terabyte bogus "file
  size"). A recording whose telemetry timestamp declared its own UTC invalid
  is no longer stamped with an invented wall time.

## v0.1.5

### Changed

- **In the output MPEG-TS, the video is now stream `0:0` and the KLV is `0:1`** —
  the reverse of every earlier release. A consumer selecting by index now gets
  the video: `ffmpeg -map 0:0`, where it previously got the metadata stream and
  needed `-map 0:v`. The change comes from the updated libmisbklv dependency,
  which reserves the video muxer pad before the KLV appsrc links so the video
  takes the lower PID.

  This removes a long-standing known limitation rather than introducing a new
  one, but it is the kind of change that breaks a script that hard-coded
  `-map 0:0` expecting metadata. Select what you want by stream type if your
  pipeline must keep working across this release.

## v0.1.4

### Changed

- **Fresh source and CI builds fetch libmisbklv without its purged historical
  data corpus.** The dependency pin now names the latest upstream revision, so
  clean builds download substantially less unused data and start faster. The
  shipped converter's runtime behavior is unchanged.

## v0.1.3

### Fixed

- **Conversion and scrubbing now refuse an output that is the input file.** The
  check recognizes ordinary spelling differences, `.` / `..`, symlinks, and
  hard links, and fails before touching the output. This prevents conversion
  from truncating its source and prevents an accidental in-place scrub.

  **If you script either tool, give `-o` a distinct destination.** A workflow
  that intentionally reused the input pathname, or an alias to it, now exits
  non-zero instead of modifying or destroying the source.

- **Invalid telemetry no longer becomes a plausible measurement.** Non-finite
  source values are omitted as invalid; finite values outside an ST 0601 item's
  encodable range are omitted rather than silently saturated. In particular, an
  out-of-range ground speed no longer becomes a false 255 m/s measurement, and
  malformed airspeed and satellite counts are not clamped into valid-looking
  values. Course over ground at rest is reported as undefined.

  The `--klv` coverage report distinguishes these cases from a field that was
  simply absent, using `source value is invalid`, `value is not encodable as
  this item`, or `undefined for this frame` as appropriate.

- **Media-backend failures retain their specific reason.** Type mismatches and
  resource-limit failures from the updated libmisbklv dependency no longer fall
  through to the generic `unknown error` text.

### Packaging

- **The release packaging scripts refuse unsafe staging directories before
  clearing them.** This affects people who run `packaging/make-bundle.sh` or
  `packaging/make-image.sh` themselves: use a dedicated child under `staging/`
  or a nested temporary directory, never the checkout, an input, your home
  directory, or a broad system directory.

## v0.1.2

### Added

- **`--infer-image-source`** fills ST 0601 **Item 11 (Image Source Sensor)** for
  recordings whose format states no imaging spectrum — which is every v3
  recording, so the whole ANAFI range. It names the sensor `IR` when the frame
  carried thermal metadata and `EO` when it did not.

  This is a **guess from a proxy, not a converted field**, so it is off by
  default, it never overrides a spectrum the recording actually states, and the
  `--klv` coverage report marks the value `(inferred)`. Two things it gets
  wrong and cannot do better on: blended EO/IR footage is named `IR`, and a
  thermal recording that wrote no thermal metadata would be named `EO`. No
  subtype is guessed — the value is always a bare `EO` or `IR`, never `EO Wide`.

  Without the flag, Item 11 is omitted exactly as before and the report says the
  format does not carry it.

- **`--version`** and **`-h` / `--help`**. `--help` previously fell through and
  was treated as a filename.

### Changed

- **Unknown options are now refused instead of ignored.** A mistyped flag —
  `--klvv`, `--with-locations` — used to be silently dropped: the tool ran to
  completion, exited 0, and produced output missing whatever you asked for. It
  now prints the offending option and exits non-zero. Likewise `-o` with no
  path, and passing two input files.

  **If you script this tool, this is the change to check.** A pipeline that has
  been quietly passing a typo will now fail instead of silently doing the wrong
  thing.

- Spelling normalized to American English throughout, which touches two strings
  you might match on: the error `Unrecognised Parrot MIME type` is now
  `Unrecognized …`, and `parrot-conform --help` says `analyze` rather than
  `analyse`.

### Known limitations

- **In the output MPEG-TS the KLV is stream `0:0` and the video is `0:1`.**
  Select streams by *type*, not index — `ffmpeg -map 0:v`, not `-map 0:0`. The
  muxer orders the PMT by internal pad order, and reversing it was tried and
  reverted because every approach cost either the KLV stream or ST 0604
  timestamp accuracy. PIDs and stream types are correct either way; only the
  announcement order is surprising.

- The `IR` half of `--infer-image-source` has never been exercised against a
  real thermal recording. The `EO` half is covered by fixtures and real flights.

## v0.1.1

- The `--klv` coverage report names ST 0601 items instead of numbering them
  (`Item 11  Image Source Sensor`), with names and units taken from the ST 0601
  registry, one item per line.
- A container image as a second deliverable, built from the same bundle, with a
  launcher that mounts only the directories in play and runs as the calling
  user.

## v0.1.0

First release. Converts Parrot v3 and proto metadata from an MP4 into an
MPEG-TS carrying the recording's own video plus a MISB ST 0601 KLV stream, with
a coverage report naming everything that could not be carried. Shipped as a
relocatable bundle that runs with nothing installed.
