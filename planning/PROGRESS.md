# Parrot to KLV — Progress

Current status only — **present tense, volatile, rewrite freely**. The
chronological history (what landed when, decision detail) lives in
[`../context/log.md`](../context/log.md); the decided-fork register is
[`../context/decisions/index.md`](../context/decisions/index.md); the plan is
[ROADMAP.md](./ROADMAP.md). This file does not re-narrate the past.

## Now

Phase 0 (Foundation) in progress. CMake scaffolding in place, Parrot metadata
formats researched and documented. Next: define field mapping strategy.

## In progress

- Documenting Parrot → MISB 0601 field mapping strategy.

## Next

1. ~~Set up CMake with FetchContent for libmisbklv~~ ✓ Done
2. ~~Research Parrot metadata binary formats~~ ✓ Done (v1/v2/v3/proto documented
   in [[parrot-metadata-formats]])
3. Document Parrot → MISB 0601 field mapping strategy (which Parrot fields map to
   which MISB 0601 tags, handling unit conversions and coordinate transforms)

## Known gaps

<!-- Things missing / risks worth remembering, that aren't yet actionable "Next". -->
