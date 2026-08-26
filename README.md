# Parrot to KLV

[![CI](https://github.com/nitsuga/parrot-to-klv/actions/workflows/build.yml/badge.svg)](https://github.com/nitsuga/parrot-to-klv/actions/workflows/build.yml)
[![License: Apache 2.0](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](LICENSE)

Converts Parrot drone telemetry metadata embedded in MP4 files into MPEG-TS files
carrying the video alongside a MISB ST 0601 KLV stream. A command-line tool.

Parrot writes telemetry into a timed-metadata track inside the MP4. This project
supports the **v3 and proto** formats — the ANAFI range and ANAFI Ai / UKR,
every Parrot drone still in production — extracting that track, mapping its
fields onto MISB ST 0601 tags, and remuxing video + KLV into MPEG-TS.

The older v2 format (Bebop 2, Disco, Bluegrass) is a possible add-on but is not
planned; v1 is deprecated by Parrot and out of scope, as are real-time
streaming, a GUI, an integration API, and KLV standards beyond ST 0601 — except
ST 0604 SEI, which the output carries because we ask libmisbklv to generate it
(its default leaves the video untouched). See
[`planning/ROADMAP.md`](planning/ROADMAP.md).

## Getting it

Tagged releases carry two prebuilt deliverables for each supported architecture
on Ubuntu 22.04 / 24.04. Choose `x86_64` or `aarch64` to match `uname -m` on the
target machine, on the [releases page](https://github.com/nitsuga/parrot-to-klv/releases):

- **`…-linux-<architecture>.tar.gz`** — extract and run. Self-contained: no
  GStreamer, no root, nothing installed. This is the recommended one.
- **`…-linux-<architecture>-docker.tar.gz`** — an exported container image plus
  a launcher, for hosts that already run containers.

Each carries a `USAGE.md`. Why they are shaped this way:
[ADR 0009](context/decisions/0009-release-bundle.md) and
[ADR 0010](context/decisions/0010-container-deliverable.md).

Build from source instead if you want to develop on it:

## Build

C++ with CMake. [libmisbklv](https://github.com/nitsuga/libmisbklv) is pulled in
via `FetchContent`, pinned to an exact commit in `CMakeLists.txt`.
`CMakePresets.json` (v3, CMake ≥ 3.21) provides the preferred workflow — the
same presets CI uses:

```
cmake --preset release        # or debug, sanitize, release-bundle — configure to build/<preset>
cmake --build --preset release
ctest --test-dir build/release
```

Preset matrix (all under `build/`): `release`/`default` → `build/release`
(Release), `debug` → `build/debug` (Debug), `sanitize` → `build/sanitize`
(Debug + `PARROT_SANITIZE=ON`, used by `build.yml`'s sanitizers job /
`ctest -L parrot-sanitize`), `release-bundle` → `build/bundle` (Release +
`PARROT_BUNDLE=ON`, used by `release.yml`). Build presets share a hidden `base`
with `jobs=6` (`nproc 7 → 6`, `nproc-1` headroom) so
`cmake --build --preset <preset>` builds in parallel without per-preset
duplication.

Manual fallback without presets is still supported:

```
cmake -S . -B build && cmake --build build && ctest --test-dir build
```

This builds three tools: `parrot-to-klv` (the converter); `parrot-scrub`, which
replaces a recording's location, identity, and flight-date fields — serial,
friendly name, title, copyright, and the absolute flight start time — so a real
flight can be shared as a test fixture; and `parrot-conform`, which records what
a real encoder produced as a
committable JSON report carrying no position, absolute time or device identity —
so real-footage evidence reaches the repository while the footage never does.

The mux stage runs through libmisbklv's GStreamer backend, which needs both the
build-time headers and the runtime plugins the pipeline instantiates. On
Debian/Ubuntu:

```
sudo apt-get install build-essential cmake pkg-config \
  libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev \
  libgstreamer-plugins-bad1.0-dev \
  gstreamer1.0-plugins-base gstreamer1.0-plugins-bad
```

The dev packages satisfy CMake's configure check (`gstreamer-codecparsers` ships
in `libgstreamer-plugins-bad1.0-dev`). The runtime plugin packages are what the
pipeline actually instantiates, and a `*-dev` package does not pull them in:
`appsrc`/`appsink` come from plugins-base, and `mpegtsmux` plus
`h264parse`/`h265parse` from plugins-bad — skip them and the mux test fails with
"media pipeline failed".

## Usage

```
parrot-to-klv -i flight.mp4 -o flight.ts         # convert (positional also works: flight.mp4 -o flight.ts)
parrot-to-klv flight.mp4 --klv                   # inspect: what maps, what doesn't
parrot-to-klv -i rtsp://192.168.0.1/live -o flight.ts                    # live RTSP → file
parrot-to-klv -i rtsp://192.168.0.1/live -o udp://239.255.0.1:5000       # live RTSP → UDP multicast
parrot-to-klv -i rtsp://192.168.0.1/live -o udp:239.255.0.1:5000 --ttl 10 --iface eth0 --no-loop
parrot-to-klv -i flight.mp4 -o udp://239.0.0.1:5000 --iface eth1         # file replay → UDP live
parrot-to-klv -i flight.mp4 -o srt://127.0.0.1:5000 --repeat             # loop until Ctrl-C
```

Input (`-i`/`--input` or a bare positional) is inferred: a filesystem path is
an MP4 file (offline convert / inspection), `rtsp://`/`rtsps://` is a live RTSP
source, and `pipeline:` is a test-only GStreamer pipeline gated by
`PARROT_TEST_ALLOW_PIPELINE`. Output (`-o`/`--output`) is inferred too:
a path is an MPEG-TS file, `udp://`/`udp:` is a UDP multicast sink (the tool
normalizes `udp://host:port` → `udp:host:port` and handles bracketed IPv6), and
`srt:` is passed through to the streaming backend. `file→udp` replay
(`-i file.mp4 -o udp:…`) replays the recording on the pipeline clock with paced
KLV emission and `file→srt` the same; add `--repeat` to loop until `SIGINT` (`--repeat-pts reset|continue`, default `reset` — `continue` advances by the recording's full track duration so the next loop starts cleanly past the previous) —
see [ADR 0015](context/decisions/0015-file-replay-live-output.md).

The offline output carries the recording's own video elementary stream plus one
ST 0601 packet per metadata sample; the live output carries the same KLV
alongside the live video pulled via RTSP and the Parrot RTP header extension.
File replay (`-i file.mp4 -o udp:/srt:`) replays the same file's video on the
pipeline clock with paced KLV emission: one play-through then close, `--repeat`
to loop until `SIGINT`. The video is never decoded or re-encoded, so there is
no generation loss — but it is not bit-identical: we ask libmisbklv to rewrite
the timing NALs, replacing Picture Timing SEI with ST 0604 Precision Time Stamps
so consumers can correlate frames without reading the KLV. Conversion shows
progress every 10%. Live RTSP runs until `SIGINT` (Ctrl-C).

Without `-o` the tool only reads a file input and reports; location and
airframe identity are withheld from that report unless `--with-location` /
`--with-identity` ask for them. UDP sinks accept `--iface <iface>`,
`--ttl <n>` (`0..255`), and `--loop`/`--no-loop` (loopback, default on) — each
only valid with a `udp:` output.

**The output announces the video first, KLV second** — `0:0` is the video and
`0:1` the KLV (`ffmpeg -map 0:0` selects the video). Video is announced first
because its muxer pad is reserved before the KLV appsrc links, giving it the
lower PID. See
[mux stage](context/mux-stage.md) for the mechanism.

**Attitude handling:** Items 6/7 (Platform Pitch/Roll) have narrow spec ranges
(±20°, ±50°) which quadcopters routinely exceed. By default the spec-mandated
`0x8000` "out-of-range" sentinel is emitted; `--clamp-attitude` emits the spec
limit instead, for consumers that treat a missing value as a parse failure.

**Items 90/91 always carry accurate full-range values**, and are where to read
the real attitude. There is no option to put the true angle into Items 6/7:
ST 0601 maps ±limit onto the whole 16-bit range, so a larger value has no
encoding there at all.

**Image Source Sensor:** Item 11 names the sensor (`EO`, `IR`, `EO Wide`). The
proto format states it; **v3 does not**, so the whole ANAFI range omits the item
by default. `--infer-image-source` guesses it from whether the frame carried
thermal metadata — opt-in, never overriding a stated spectrum, and marked
`(inferred)` in the coverage report. It names blended EO/IR footage `IR`, which
is the price of a v3 format that gives nothing to tell the two apart
([ADR 0011](context/decisions/0011-inferred-image-source-sensor.md)).

The tests run against synthetic fixtures in `test/fixtures/`, so `ctest` needs no
drone footage. Real test inputs live in `data/`, which is **gitignored** — it
holds footage with live location data and must never be committed.

Release notes for each version are in [`CHANGELOG.md`](CHANGELOG.md).

## Project status

This README describes what the project *is*. For what's happening right now, read
[`planning/PROGRESS.md`](planning/PROGRESS.md) (present state) and
[`context/log.md`](context/log.md) (what landed when). Status is deliberately not
duplicated here, so it can't drift.

## Working on this project

The docs are maintained by an AI agent following the working rules in
[`AGENTS.md`](AGENTS.md) — a pre-commit checklist and a set of planning-hygiene
rules that keep each doc to one job. `AGENTS.md` is the one canonical,
vendor-neutral copy; [`CLAUDE.md`](CLAUDE.md) is a one-line `@AGENTS.md` import,
since Claude Code auto-loads that filename. Supporting another agent means
adding another thin pointer, never a second copy of the rules. The reasoning
behind those rules is in
[`context/workflow-rationale.md`](context/workflow-rationale.md), and the
directory taxonomy in [`context/CONVENTIONS.md`](context/CONVENTIONS.md)
§ Layout. This workflow came from
[okf-project-template](https://github.com/nitsuga/okf-project-template).

**CI**, both on push and pull request:
[`build.yml`](.github/workflows/build.yml) builds and runs the suite on Ubuntu,
and [`link-check.yml`](.github/workflows/link-check.yml) fails on broken internal
doc links. [`release.yml`](.github/workflows/release.yml) runs on a `v*` tag (or
manually, which builds and tests but does not publish), producing both release
deliverables and smoke-testing each one before anything is attached to a
release. The build job needs one secret, since libmisbklv is a private
dependency — setup in [`.github/workflows/README-ci.md`](.github/workflows/README-ci.md).

## Structure

```
AGENTS.md              how-to-work instructions (canonical, vendor-neutral)
CLAUDE.md              one-line adapter: @AGENTS.md (Claude Code auto-loads)
CHANGELOG.md           hand-written release notes (release.yml publishes these)
CMakeLists.txt         build (FetchContent pulls libmisbklv at a pinned commit)
.github/workflows/
  build.yml            CI — builds and runs the suite on Ubuntu
  release.yml          CI — builds both release deliverables on a tag
  link-check.yml       CI — fails on broken internal doc links
  README-ci.md         the one secret build.yml needs, and why
packaging/             release deliverables: bundle, image, launcher, usage
src/                   C++ source (parrot-to-klv, parrot-scrub, parrot-conform)
test/                  tests + synthetic MP4 fixtures
  make_fixture.py      regenerates test/fixtures/ (ffmpeg for the video ones)
  fixtures/            committed synthetic MP4s — ctest needs no footage
  conformance/         reports from real recordings (no location data)
external/              vendored deps (minimp4, xxHash — both single-header)
context/               agent-owned OKF knowledge bundle
  index.md             bundle catalog (read first)
  CONVENTIONS.md       frontmatter, types, ADR format, ingest/query/lint
  workflow-rationale.md  why each planning-hygiene rule exists
  log.md               durable chronological record
  decisions/
    index.md           decided register: fork ↔ ADR ↔ status
    0001-*.md          ADRs
planning/
  ROADMAP.md           scope, phases, open + candidate forks
  PROGRESS.md          present state: Now / In-progress / Next
references/            source inputs (append-only; snapshots immutable) — Parrot vmeta.proto (BSD-3) + doc link file
docs/                  human-facing guides
data/                  test inputs — gitignored, never committed
```

## License

[Apache 2.0](LICENSE).
