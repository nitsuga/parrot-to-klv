# Parrot to KLV — Progress

Current status only — **present tense, volatile, rewrite freely**. The
chronological history (what landed when, decision detail) lives in
[`../context/log.md`](../context/log.md); the decided-fork register is
[`../context/decisions/index.md`](../context/decisions/index.md); the plan is
[ROADMAP.md](./ROADMAP.md). This file does not re-narrate the past.

## Now

**Phase 1 (Parse) started.** MP4 metadata extractor implemented (minimp4
integration, pimpl pattern). Build configuration clean (FetchContent for
libmisbklv). Status: WIP - minimp4 API usage needs corrections before testing.

The field mapping is settled against the complete ST 0601.19 registry:
[[field-mapping-analysis]] is the implementation spec for the transform stage.
libmisbklv needs no registry work for this project. Fork 1 (unmappable Parrot
fields) is `PROPOSED` in [ADR 0002](../context/decisions/0002-unmappable-parrot-fields.md),
awaiting acceptance.

## In progress

- Fixing minimp4 API usage in `mp4_metadata_extractor.cpp` (handler type detection,
  frame offset/size/duration signatures)

## Next

1. Test MP4 extraction with `data/P0330036.MP4`
2. Implement Parrot v3 binary parser (fixed-point decoding, extensions)
3. Implement Parrot proto parser (protobuf)

## Known gaps

- Deriving Items 21/22/23/24/25/57/78/82–89 needs a boresight-to-terrain
  intersection — the substantive remaining work in the transform stage, and the
  one part of the mapping with no direct Parrot source. Fidelity choice is a
  candidate fork in [ROADMAP](./ROADMAP.md).
- Item 2 for v2/v3 recordings: the frame clock is monotonic, so wall-clock
  anchoring depends on session `media_date` (second granularity).
