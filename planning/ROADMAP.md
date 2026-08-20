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
streaming, GUI, KLV standards beyond ST 0601 — with one exception: the output
carries **ST 0604** SEI, which we ask libmisbklv to generate rather than
implement here
([ADR 0006](../context/decisions/0006-inherited-st0604-sei-injection.md)).

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
  libmisbklv's `KlvSink`, the source's elementary stream never decoded (its
  timing NALs are rewritten to carry ST 0604 — see
  [ADR 0006](../context/decisions/0006-inherited-st0604-sei-injection.md)). See
  [mux stage](../context/mux-stage.md).

- **Phase 4 — Release** — *done*: two deliverables for Ubuntu 22.04/24.04
  x86-64, both built and smoke-tested by `release.yml` on tag — a relocatable
  bundle ([ADR 0009](../context/decisions/0009-release-bundle.md)) and an
  exported container image built from it
  ([ADR 0010](../context/decisions/0010-container-deliverable.md)). The bundle
  is the default; the image is for hosts already running containers.

## Open forks

A *fork* is a decision point — a branch in the plan/design that needs a choice
(see [`../context/CONVENTIONS.md`](../context/CONVENTIONS.md) § Decisions).

Status legend: `OPEN` (undiscussed) · `PROPOSED` (Decision concept written,
`decision_status: proposed`) · `DECIDED` / `DEFERRED` (has an ADR — see the
register).

- **Fork 4 — Tier 2 geometry fidelity** `OPEN`: flat-earth intersection vs. a
  DTED terrain model for frame center, corner points, and slant/ground range.
  Weighs a data dependency against accuracy on sloped terrain. **Now open** —
  its stated trigger was the transform stage reaching the derived items, which
  it has: Tier 1 is complete and Items 21/22/23/24/25/57/78/82–89 are the
  remainder. See [field mapping](../context/field-mapping-analysis.md) § Tier 2.

- **Fork 13 — live streaming (ingest + multicast output)** `PROPOSED`
  ([ADR 0013](../context/decisions/0013-live-streaming-ingest.md)): placement
  only. The generic live mux/pacing/multicast surface comes from libmisbklv
  (their fork 28 / ADR 0031, proposed); parrot-to-klv owns the RTSP pull, the
  Parrot RTP header-extension capture, the existing v3/proto decode, and the
  transform. No new shared repo; the "real-time streaming out of scope" line in
  § Scope changes once implemented.

Genuinely open (undeliberated) forks get a line here until they have an ADR;
once decided they move to the [register](../context/decisions/index.md) and leave
this list. Candidate future forks (not yet opened) can be listed here too — the
single backlog. Incremental *work* that needs no fork lives in
[PROGRESS](./PROGRESS.md) "Next", not here.

## Fork lifecycle

When a fork is deliberated, write a `type: Decision` concept in `../context/`
(`decision_status: proposed` → `accepted`), add its row to the decided register
([`../context/decisions/index.md`](../context/decisions/index.md)), and update
PROGRESS. A genuinely open (undeliberated) fork gets a line under **Open forks**
above until it has an ADR.
