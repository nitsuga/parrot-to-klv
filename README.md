# Parrot to KLV

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

Tagged releases carry two prebuilt deliverables for Ubuntu 22.04 / 24.04,
x86-64, on the [releases page](https://github.com/nitsuga/parrot-to-klv/releases):

- **`…-linux-x86_64.tar.gz`** — extract and run. Self-contained: no GStreamer,
  no root, nothing installed. This is the recommended one.
- **`…-linux-x86_64-docker.tar.gz`** — an exported container image plus a
  launcher, for hosts that already run containers.

Each carries a `USAGE.md`. Why they are shaped this way:
[ADR 0009](context/decisions/0009-release-bundle.md) and
[ADR 0010](context/decisions/0010-container-deliverable.md).

Build from source instead if you want to develop on it:

## Build

C++ with CMake. [libmisbklv](https://github.com/nitsuga/libmisbklv) is pulled in
via `FetchContent`, pinned to an exact commit in `CMakeLists.txt`, so a standard
workflow is all that's needed:

```
cmake -S . -B build && cmake --build build && ctest --test-dir build
```

This builds three tools: `parrot-to-klv` (the converter); `parrot-scrub`, which
replaces a recording's location fields so a real flight can be shared as a test
fixture; and `parrot-conform`, which records what a real encoder produced as a
committable JSON report carrying no position, absolute time or device identity —
so real-footage evidence reaches the repository while the footage never does.

GStreamer development packages are needed, since the mux stage runs through
libmisbklv's GStreamer backend.

## Usage

```
parrot-to-klv flight.mp4 -o flight.ts     # convert
parrot-to-klv flight.mp4 --klv            # inspect: what maps, what doesn't
```

The output carries the recording's own video elementary stream plus one ST 0601
packet per metadata sample. The video is never decoded or re-encoded, so there
is no generation loss — but it is not bit-identical: we ask libmisbklv to rewrite
the timing NALs, replacing Picture Timing SEI with ST 0604 Precision Time Stamps
so consumers can correlate frames without reading the KLV. Conversion shows
progress every 10%.
Without `-o` the tool only reads the file and reports; location and airframe
identity are withheld from that report unless `--with-location` /
`--with-identity` ask for them.

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

## Project status

This README describes what the project *is*. For what's happening right now, read
[`planning/PROGRESS.md`](planning/PROGRESS.md) (present state) and
[`context/log.md`](context/log.md) (what landed when). Status is deliberately not
duplicated here, so it can't drift.

## Working on this project

The docs are maintained by an AI agent following the working rules in
[`CLAUDE.md`](CLAUDE.md) — a pre-commit checklist and a set of planning-hygiene
rules that keep each doc to one job. The reasoning behind those rules is in
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
CLAUDE.md              how-to-work instructions (agent auto-loads)
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
references/            immutable inputs (read-only) — Parrot metadata spec
docs/                  human-facing guides
data/                  test inputs — gitignored, never committed
```

## License

[Apache 2.0](LICENSE).
