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

Test inputs live in `data/`, which is **gitignored** — it holds real drone footage
with live location data and must never be committed.

## Project status

This README describes what the project *is*. For what's happening right now, read
[`planning/PROGRESS.md`](planning/PROGRESS.md) (present state) and
[`context/log.md`](context/log.md) (what landed when). Status is deliberately not
duplicated here — see § "Why these rules" below.

## The doc taxonomy

> This project uses an agent documentation workflow generated from
> [okf-project-template](https://github.com/nitsuga/okf-project-template). The two
> sections below explain that workflow; the rules an agent follows live in
> [`CLAUDE.md`](CLAUDE.md).

Four top-level doc directories, each with one job:

| Dir | Role | Audience |
|-----|------|----------|
| `references/` | **Immutable** source-of-truth inputs (specs, standards, prior art). Read, never modify. | inputs |
| `context/` | **Agent-owned** knowledge bundle (OKF): synthesized concepts + the decision register (ADRs). | agent |
| `planning/` | **Volatile** plan + present state — `ROADMAP.md` (scope/phases/open forks) and `PROGRESS.md` (Now/Next). | both |
| `docs/` | **Human-facing** authored guides (terse). | humans |

Plus [`CLAUDE.md`](CLAUDE.md) at the root — the *how-to-work* instructions an
agent auto-loads: the doc roles above and the planning-hygiene rules below.

**Doc-link CI:** [`.github/workflows/link-check.yml`](.github/workflows/link-check.yml)
runs on push and pull request, and fails on broken internal doc links.

## Why these rules

The hygiene rules in `CLAUDE.md` look fussy until you've watched docs rot. Each
earned its place by a failure mode:

- **One job per doc.** When two docs narrate the same thing, one copy drifts and
  you can't tell which is right. So: history lives only in `log.md`, the decided
  register only in `decisions/index.md`, present state only in `PROGRESS.md`.
- **History → `log.md`, present-state → `PROGRESS.md`.** A curated knowledge base
  must not carry volatile session history, and a status doc must not accrete a
  "Done" pile. Keeping them separate keeps each trustworthy.
- **Docs land in the *same commit* as the code.** A doc updated "later" is a doc
  that is wrong in between — and agents, reviewers, and your future self all read
  the repo *at a commit*, not at "later". Deferring also means reconstructing what
  changed from memory, so the entry arrives thin or never arrives at all. So the
  commit that changes behaviour carries its own `log.md` + `PROGRESS.md` update;
  the checklist at the top of `CLAUDE.md` is the gate that enforces it.
- **No live tallies.** A count ("19 tests", "forks 1–15") is wrong the moment the
  next change lands, and nobody remembers to update prose numbers. Describe the
  *state* ("all tests green", "none open — see the register"); a number is only
  ever a frozen snapshot in a dated `log.md` line.
- **Closing scrubs the future / one home for open work.** Forward-looking content
  drifts exactly like history: resolve an item but leave it listed under "Next" /
  "candidate" elsewhere, and the docs now lie. So a candidate lives in *one* place
  (ROADMAP's backlog), and resolving it means deleting its forward-looking
  mentions too — not just updating the present-state bullet.
- **Lint is the backstop.** Nothing fires it automatically, so run it periodically
  (closing a fork, before a release). But the real lever is reducing what *can*
  drift (the rules above), so the lint has little to catch.

The meta-lesson: **duplication rots — in both tenses.** Most of these rules are
one idea (don't say the same thing in two places) applied to the past (history,
decisions) and the future (open/candidate work).

## Structure

```
CLAUDE.md              how-to-work instructions (agent auto-loads)
CMakeLists.txt         build (FetchContent pulls libmisbklv)
.github/workflows/
  link-check.yml       CI — fails on broken internal doc links
src/                   C++ source
external/              vendored deps (minimp4 single-header MP4 parser)
context/               agent-owned OKF knowledge bundle
  index.md             bundle catalog (read first)
  CONVENTIONS.md       frontmatter, types, ADR format, ingest/query/lint
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
