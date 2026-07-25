# Decisions — ADR register

Architecture Decision Records: the *why* of resolved forks. This table is the
**register of decided forks** — the roadmap fork #, the ADR that resolved it, and
status. `status` vocabulary and lifecycle in [`../CONVENTIONS.md`](../CONVENTIONS.md)
§ Decisions. Forks *without* an ADR yet (genuinely open) live in
[`../../planning/ROADMAP.md`](../../planning/ROADMAP.md); it points here for the
decided ones rather than duplicating them.

`Fork` is the roadmap fork number (ADR numbering is by creation order, so they
diverge; one fork can span several ADRs).

| Fork | ADR | Status |
|------|-----|--------|
| — | [0001 — Direct MP4 Parsing for Metadata Extraction](./0001-mp4-extraction-approach.md) | accepted |
| 1 | [0002 — Disposition of Parrot fields with no ST 0601 item](./0002-unmappable-parrot-fields.md) | accepted |

_ADR 0001 predates formal fork numbering (no fork #): chosen direct MP4 parsing
over GStreamer qtdemux based on investigation._
