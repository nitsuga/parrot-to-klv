# Parrot to KLV — Progress

Current status only — **present tense, volatile, rewrite freely**. The
chronological history (what landed when, decision detail) lives in
[`../context/log.md`](../context/log.md); the decided-fork register is
[`../context/decisions/index.md`](../context/decisions/index.md); the plan is
[ROADMAP.md](./ROADMAP.md). This file does not re-narrate the past.

## Now

**Phase 0 (Foundation) complete.** CMake in place, formats researched, field
mapping analyzed. Ready to begin Phase 1 (Parse) implementation.

## In progress

- Nothing actively mid-change. Ready for Phase 1 work.

## Next

1. ~~Set up CMake with FetchContent for libmisbklv~~ ✓ Done
2. ~~Research Parrot metadata binary formats~~ ✓ Done
3. ~~Document field mapping strategy~~ ✓ Done — [[field-mapping-analysis]]: 27
   existing tags map directly, 9 high-priority IMAPB tags needed for Phase 2

**Phase 1 — Parse** (next phase):
1. Implement MP4 metadata track extraction (read `tkhd`, `tref`, `stsd` for MIME
   type, sample data)
2. Implement Parrot v3 binary parser (focus on ANAFI, most common modern format)
3. Test with sample MP4 files from `data/` (when available)

## Known gaps

<!-- Things missing / risks worth remembering, that aren't yet actionable "Next". -->
