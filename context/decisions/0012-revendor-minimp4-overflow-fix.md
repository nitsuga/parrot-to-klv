---
type: Decision
title: ADR 0012 — Re-vendor minimp4.h onto upstream's overflow fix rather than patch it locally
description: Close the stsz sample_count integer-overflow heap-write (issue #27) and the stts/stsz OOB read (issue #28) by pulling forward to lieff/minimp4's current commit, instead of patching the vendored copy or pre-validating outside MP4D_open
decision_status: accepted
fork: 12
tags: [decision, security, hardening, mp4-extraction, vendored-dependency]
generated:
  by: claude/sonnet-5
  at: 2026-08-19T00:00:00Z
---

# Context

Issue #27 found a heap-buffer-overflow **write** inside vendored
`external/minimp4/minimp4.h`'s `BOX_stsz`/`BOX_stz2` handling: `sample_count *
4` is computed in native 32-bit `unsigned` arithmetic before the allocation
size reaches `MALLOC`, so a `sample_count` near `0x40000000` wraps the product
to a few bytes. The allocation succeeds at that tiny size; the loop that fills
it still runs the full, un-overflowed `sample_count` iterations, writing past
the buffer from the first iteration. Reachable from a few hundred bytes on
disk — the box's real payload can be absent entirely, since `READ()` clamps to
zero-padding at EOF instead of aborting the loop.

This is **not reachable by any wrapper-boundary check.** It happens inside
`MP4D_open()` itself, before `track_sample_tables_valid()`
(`src/mp4_metadata_extractor.cpp`, added for issue #3/#26) or any other
post-open validation this project owns ever runs — that function's own
comment already notes it cannot catch "an 'stts' present but declaring fewer
samples than 'stsz'" (issue #28) for the identical reason: the count that
would prove it is discarded once minimp4 parses the box, not recoverable from
the public struct.

`AGENTS.md` states vendored code — `external/` — is "read it, never restyle
it," the same rule that keeps `references/` append-only. Patching
`minimp4.h`'s logic directly, even narrowly, is an exception to that rule and
was flagged in the issue as needing its own decision rather than a quiet fix.

# Decision

**Re-vendor `minimp4.h` onto upstream's current commit** —
`5a212a18dba7dca09543bbc7d65619274fd2931a` (2026-07-13), up from the
2021-08-20 pin — rather than patch the vendored copy locally or add an
independent pre-`MP4D_open()` validator.

Verified before adopting:

- **The commit is genuinely upstream**, not a spoofed or third-party fork:
  `gh api repos/lieff/minimp4` confirms `lieff/minimp4` is not a fork, and the
  commit's `committer` is `lieff` (the repo owner) merging changes authored by
  a contributor (`claytercek`).
- **It fixes exactly this bug.** `df109a3` ("widen malloc size expressions
  before overflow can occur") casts `sample_count` to `uint64_t` before the
  multiply, so the true size reaches the existing `minimp4_bounded_malloc`
  ceiling (256 MiB) instead of wrapping past it.
- **It separately fixes issue #28.** `8d97af1` adds `tr->timestamp_count`,
  tracking the allocated length of `timestamp`/`duration` independently of
  whatever `sample_count` a malformed `stsz` claims, and bound-checks
  `MP4D_frame_offset()` against it — precisely the "not recoverable from the
  public struct without re-parsing 'stts' independently" gap
  `track_sample_tables_valid()`'s comment names.
- **It brings no unrelated risk.** The five commits between the old pin and
  the new tip are all fuzzing-driven hardening in the same vein (null-track
  derefs at depth 0, `O(n²)` chunk-lookup hangs, an unbounded `stts` growth
  hang, an unbounded `BOX_ctts` spin) — no feature changes, no behavior this
  project depends on removed.
- **The public API is unchanged.** `MP4D_open`, `MP4D_frame_offset`,
  `MP4D_close` keep identical signatures; `MP4D_track_t` only gains new
  *private*-section fields (resume caches, `timestamp_count`) after the ones
  this project reads (`sample_count`, `entry_size`, `sample_to_chunk_count`,
  `sample_to_chunk`, `chunk_count`, `chunk_offset`, `timestamp`, `duration`) —
  source-compatible with `mp4_metadata_extractor.cpp` and
  `track_sample_tables_valid()` unmodified.
- **The fix actually closes the hole.** A standalone repro — a crafted
  593-byte file with a `com.parrot.videometadata3` track whose `stsz`
  declares `sample_count = 0x40000000` and no real per-sample payload,
  compiled against the extractor under ASan/UBSan — reproduces a genuine
  `heap-buffer-overflow WRITE` at `MP4D_open` against the old pin, and cleanly
  returns "Failed to parse MP4 file" (no crash, extractor `open()` returns
  `nullopt`) against the new one. That fixture is now a permanent regression
  test (`parrot_stsz_count_overflow.mp4`,
  `test_stsz_count_overflow_is_rejected` in `mp4_extraction_test.cpp`).
- **License unchanged** (Unlicense/public domain) and the full suite —
  including the ASan/UBSan build — passes unmodified against the new header.

# Alternatives considered

## Patch `minimp4.h` directly

Smallest, most surgical diff: cast the multiply to a wider type at this one
call site.

**Rejected.** Crosses "never restyle vendored code" for a hand-patch that
upstream had already written, reviewed, and shipped more thoroughly (it also
covers the DSI and tag-string `MALLOC` call sites this project's wrapper never
exercised, and the `timestamp_count` fix for issue #28) — re-vendoring gets a
better fix for less ongoing maintenance burden, since a local patch would need
manual re-application on every future pin bump.

## Independent pre-`MP4D_open()` validation (a narrow `stsz` pre-scanner)

Sanity-check `sample_count` via `mp4_box_scan.cpp` or a new scanner, before
`MP4D_open()` ever runs, keeping `minimp4.h` untouched.

**Rejected.** `mp4_box_scan.cpp` is documented as track-level metadata only,
not sample tables (see [mp4-extraction](../mp4-extraction.md)); this would
mean partially duplicating minimp4's own `stsz`/`stsc`/`stco`/`stts` parsing
in a second, independently-maintained implementation, just to bound-check
values the real parser then reads again. More code, a new place for the two
parses to disagree, and it still would not close issue #28 the way upstream's
`timestamp_count` field does.

## Accept the residual risk, documented

Reasoning: Parrot recordings are not adversarial in this project's actual
threat model.

**Rejected**, and was already the weakest option in the issue that raised
this fork — a `security`-labeled, `priority: high` finding with a concrete,
cheap repro is exactly the case this project's stated hardening posture (see
issue #3/#26's `track_sample_tables_valid()`, the 1 MiB sample-size cap) does
not leave undone once a fix is available and free.

# Consequences

- `external/minimp4/README.md`'s provenance now reads
  `5a212a18dba7dca09543bbc7d65619274fd2931a` (2026-07-13), current tip at
  decision time — the pin is not tracking upstream `master` on an ongoing
  basis; a future overflow-class finding still gets triaged the same way.
- Issues #27 and #28 both close: #28's stts/stsz-mismatch OOB read turned out
  to be a side effect of the same upstream hardening pass, not a separate fix.
- The vendored-code "never restyle" rule's exception is now precedented for a
  clean version bump onto an upstream security fix — not for a local edit to
  vendored logic, which this decision did not do and remains off the table
  absent a case where no upstream fix exists.
- `test/make_fixture.py` gained `build_stsz_count_overflow()`, following the
  existing hostile-fixture pattern from issue #3
  (`build_missing_stsc`/`build_oversized_sample_size`).

# Citations

- [mp4-extraction](../mp4-extraction.md) — `mp4_box_scan.cpp`'s documented
  single purpose, the alternative this decision weighed against
- `external/minimp4/README.md` — updated provenance
- `src/mp4_metadata_extractor.cpp` § `track_sample_tables_valid()` — the
  wrapper-boundary check this fix's limits are documented against
- Issue #27 (this decision's trigger) and #28 (closed by the same fix)
- Issue #3 / [PR #26] — the wrapper-boundary hardening this fork could not
  reach, which is why this fork exists
