# Parrot to KLV

Converts Parrot drone telemetry metadata embedded in MP4 files into MPEG-TS files
carrying the video alongside a MISB ST 0601 KLV stream. Provides a command-line
tool and an optional API for integration into other applications.

Parrot writes telemetry into a timed-metadata track inside the MP4 (formats v1,
v2, v3, and protobuf). This project extracts that track, maps its fields onto
MISB ST 0601 tags, and remuxes video + KLV into MPEG-TS. Real-time streaming, a
GUI, and KLV standards beyond ST 0601 are out of scope — see
[`planning/ROADMAP.md`](planning/ROADMAP.md).

## Build

C++ with CMake. [libmisbklv](https://github.com/nitsuga/libmisbklv) is pulled in
via `FetchContent`, so a standard workflow is all that's needed:

```
cmake -S . -B build && cmake --build build && ctest --test-dir build
```

This builds three tools: `parrot-to-klv` (the converter); `parrot-scrub`, which
replaces a recording's location fields so a real flight can be shared as a test
fixture; and `parrot-conform`, which records what a real encoder produced as a
committable JSON report carrying no position, absolute time or device identity —
so real-footage evidence reaches the repository while the footage never does.

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

**Doc-link CI:** [`.github/workflows/link-check.yml`](.github/workflows/link-check.yml)
runs on push and pull request, and fails on broken internal doc links.

## Structure

```
CLAUDE.md              how-to-work instructions (agent auto-loads)
CMakeLists.txt         build (FetchContent pulls libmisbklv)
.github/workflows/
  link-check.yml       CI — fails on broken internal doc links
src/                   C++ source (parrot-to-klv + parrot-scrub)
test/                  tests + synthetic MP4 fixtures
  make_fixture.py      regenerates test/fixtures/
  fixtures/            committed synthetic MP4s — ctest needs no footage
  conformance/         reports from real recordings (no location data)
external/              vendored deps (minimp4 single-header MP4 parser)
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
