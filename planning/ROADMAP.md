# Parrot to KLV — Roadmap

The plan: scope, sequencing, and **open** forks. Living doc — rewrite freely. For
"where are we right now" see [PROGRESS.md](./PROGRESS.md). Resolved forks and
their rationale live in [`../context/`](../context/) as `type: Decision` concepts;
the **decided register** (fork # ↔ ADR ↔ status) is
[`../context/decisions/index.md`](../context/decisions/index.md). This file does
not duplicate it — it tracks scope, phases, and forks still open.

## Scope

Convert Parrot drone telemetry metadata from MP4 files to MPEG-TS files with MISB
0601 KLV streams. Extract Parrot metadata from the MP4 timed metadata track, map
fields to MISB 0601, and remux video + KLV into MPEG-TS.

**In scope:** Command-line tool, MISB 0601 output, and the **v3 and proto**
metadata formats — the ANAFI range and ANAFI Ai / UKR, i.e. every Parrot drone
still in production. Both are implemented.

**Deferred**, in rough order of likelihood:

- **v2** (Bebop 2, Disco, Bluegrass) — a plausible add-on. Substantively a
  subset of v3, and [ADR 0004](../context/decisions/0004-shared-telemetry-snapshot.md)
  is shaped so it would add one normalizer and touch nothing else. Not planned.
- **The open mapping questions** — Item 134 zoom percentage, Items 45/46
  CE90/LE90, Item 48 Security LS, and Tier 3 VMTI (Item 74). Each is enumerated
  with its difficulty in
  [field mapping](../context/field-mapping-analysis.md) § Open questions, and
  each is currently *reported* as an omission rather than guessed at, so
  deferring them costs correctness nothing.
- **The integration API** — the headers are usable, but no API surface is
  designed or committed to. The CLI is the product for now.

**Out of scope:** v1 (deprecated by Parrot; the Bebop 1 era), real-time
streaming, GUI, KLV standards beyond ST 0601.

## Phases

- **Phase 0 — Foundation** — *done*: repo, CMake build with libmisbklv via
  FetchContent, reference ingestion, field-mapping research.
- **Phase 1 — Parse** — *done for the supported formats*: MP4 metadata
  extraction, the v3 and proto parsers, and session metadata. v2 is deferred
  (see Scope), so nothing here is outstanding.
- **Phase 2 — Transform** — *Tier 1 done*: Parrot → ST 0601 mapping with a
  conversion coverage report. Tier 2 derived geometry remains, gated on the
  open fork below.
- **Phase 3 — Mux** — *done*: video + KLV remuxed into MPEG-TS through
  libmisbklv's `KlvSink`, the source's elementary stream passed through
  unchanged. See [mux stage](../context/mux-stage.md).

## Open forks

A *fork* is a decision point — a branch in the plan/design that needs a choice
(see [`../context/CONVENTIONS.md`](../context/CONVENTIONS.md) § Decisions).

Status legend: `OPEN` (undiscussed) · `PROPOSED` (Decision concept written,
`status: proposed`) · `DECIDED` / `DEFERRED` (has an ADR — see the register).

- **Fork 4 — Tier 2 geometry fidelity** `OPEN`: flat-earth intersection vs. a
  DTED terrain model for frame centre, corner points, and slant/ground range.
  Weighs a data dependency against accuracy on sloped terrain. **Now open** —
  its stated trigger was the transform stage reaching the derived items, which
  it has: Tier 1 is complete and Items 21/22/23/24/25/57/78/82–89 are the
  remainder. See [field mapping](../context/field-mapping-analysis.md) § Tier 2.

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
