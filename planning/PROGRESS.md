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

## In progress

- Fixing minimp4 API usage in `mp4_metadata_extractor.cpp` (handler type detection,
  frame offset/size/duration signatures)

## Next

1. Test MP4 extraction with `data/P0330036.MP4`
2. Implement Parrot v3 binary parser (fixed-point decoding, extensions)
3. Implement Parrot proto parser (protobuf)
4. Wire up field mapping to libmisbklv Message API

## Known gaps

<!-- Things missing / risks worth remembering, that aren't yet actionable "Next". -->
