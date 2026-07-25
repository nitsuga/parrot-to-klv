# Parrot to KLV — Progress

Current status only — **present tense, volatile, rewrite freely**. The
chronological history (what landed when, decision detail) lives in
[`../context/log.md`](../context/log.md); the decided-fork register is
[`../context/decisions/index.md`](../context/decisions/index.md); the plan is
[ROADMAP.md](./ROADMAP.md). This file does not re-narrate the past.

## Now

**Phase 1 (Parse) in progress.** MP4 metadata extraction works end to end: the
track is located, its format is read from the stsd MIME type, and samples come
out with timestamps. Verified against both synthetic fixtures (`test/fixtures/`)
and real Parrot footage (`data/P0330036.MP4` — 699 samples detected, extraction
successful). Design and minimp4's traps:
[MP4 metadata extraction](../context/mp4-extraction.md).

The field mapping is settled against the complete ST 0601.19 registry —
[field mapping](../context/field-mapping-analysis.md) is the implementation spec
for the transform stage, and libmisbklv needs no registry work for this project.
Parrot fields with no 0601 item are dropped and reported, per
[ADR 0002](../context/decisions/0002-unmappable-parrot-fields.md) — so the
transform stage owes a conversion coverage report alongside the KLV output.

## In progress

- Nothing mid-flight.

## Next

1. Implement Parrot v3 binary parser (fixed-point decoding, extensions)
2. Implement Parrot proto parser (protobuf)
3. Begin transform stage: direct field mappings (Parrot → ST 0601 items)

## Known gaps

- Deriving Items 21/22/23/24/25/57/78/82–89 needs a boresight-to-terrain
  intersection — the substantive remaining work in the transform stage, and the
  one part of the mapping with no direct Parrot source. Fidelity choice is a
  candidate fork in [ROADMAP](./ROADMAP.md).
- Item 2 for v2/v3 recordings: the frame clock is monotonic, so wall-clock
  anchoring depends on session `media_date` (second granularity).
