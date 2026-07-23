# Parrot to KLV — Progress

Current status only — **present tense, volatile, rewrite freely**. The
chronological history (what landed when, decision detail) lives in
[`../context/log.md`](../context/log.md); the decided-fork register is
[`../context/decisions/index.md`](../context/decisions/index.md); the plan is
[ROADMAP.md](./ROADMAP.md). This file does not re-narrate the past.

## Now

Project initialized from `okf-project-template`. Scope defined: convert Parrot
telemetry from MP4 to MPEG-TS with MISB 0601 KLV. Ready to begin Phase 0
(Foundation).

## In progress

- Running project setup (filling in docs, ingesting references, removing examples).

## Next

1. Set up CMake build with FetchContent for libmisbklv, basic project structure.
2. Research Parrot metadata binary formats (v1/v2/v3/proto) from libvideo-metadata
   reference and documentation.
3. Document Parrot → MISB 0601 field mapping strategy (which Parrot fields map to
   which MISB tags).

## Known gaps

<!-- Things missing / risks worth remembering, that aren't yet actionable "Next". -->
