# Parrot to KLV — Roadmap

The plan: scope, sequencing, and **open** forks. Living doc — rewrite freely. For
"where are we right now" see [PROGRESS.md](./PROGRESS.md). Resolved forks and
their rationale live in [`../context/`](../context/) as `type: Decision` concepts;
the **decided register** (fork # ↔ ADR ↔ status) is
[`../context/decisions/index.md`](../context/decisions/index.md). This file does
not duplicate it — it tracks scope, phases, and forks still open.

## Scope

Convert Parrot drone telemetry metadata from MP4 files to MPEG-TS files with MISB
0601 KLV streams. Support all Parrot metadata formats (v1, v2, v3, proto). Extract
Parrot metadata from MP4 timed metadata track, map fields to MISB 0601, and remux
video + KLV into MPEG-TS.

**In scope:** Command-line tool and optional API, all Parrot formats, MISB 0601
output.

**Deferred/out of scope:** Real-time streaming (focus on file conversion first),
GUI, other KLV standards beyond ST 0601.

## Phases

- **Phase 0 — Foundation** (in progress): Repo setup, CMake build with libmisbklv
  via FetchContent, reference ingestion, field mapping research.
- **Phase 1 — Parse**: Implement Parrot metadata extraction from MP4 (all formats:
  v1, v2, v3, proto).
- **Phase 2 — Transform**: Implement Parrot → MISB 0601 field mapping and
  conversion logic.
- **Phase 3 — Mux**: Integrate libmisbklv to remux video + KLV into MPEG-TS.

## Open forks

A *fork* is a decision point — a branch in the plan/design that needs a choice
(see [`../context/CONVENTIONS.md`](../context/CONVENTIONS.md) § Decisions).

Status legend: `OPEN` (undiscussed) · `PROPOSED` (Decision concept written,
`status: proposed`) · `DECIDED` / `DEFERRED` (has an ADR — see the register).

- **Tier 2 geometry fidelity** *(candidate)*: flat-earth intersection vs. a DTED
  terrain model for frame center, corner points, and slant/ground range. Weighs
  a data dependency against accuracy on sloped terrain. Opens when the transform
  stage reaches derived items — see [[field-mapping-analysis]] § Tier 2.

Genuinely open (undeliberated) forks get a line here until they have an ADR;
once decided they move to the [register](../context/decisions/index.md) and leave
this list. Candidate future forks (not yet opened) can be listed here too — the
single backlog. Incremental *work* that needs no fork lives in
[PROGRESS](./PROGRESS.md) "Next", not here.

## Fork lifecycle

When a fork is deliberated, write a `type: Decision` concept in `../context/`
(`status: proposed` → `accepted`), add its row to the decided register
([`../context/decisions/index.md`](../context/decisions/index.md)), and update
PROGRESS. A genuinely open (undeliberated) fork gets a line under **Open forks**
above until it has an ADR.
