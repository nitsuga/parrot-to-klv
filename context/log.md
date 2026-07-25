# Knowledge Bundle Log

<!-- Durable chronological record, newest first, ISO YYYY-MM-DD day headings.
     One thin line per decision (chronology + link — the ADR owns the rationale);
     a fuller entry per significant implementation. Routine ingests/lint go here
     too. This is the ONLY home for history — PROGRESS.md is present-tense only. -->

## 2026-07-25

* **MP4 metadata extraction works end to end.** The extractor did not compile;
  fixing it turned up seven distinct defects, most of them fabricated API:
  - `MP4D_frame_size()` and `MP4D_frame_duration()` do not exist in minimp4.
    `MP4D_frame_offset()` takes six arguments, was called with four, and returns
    an *unsigned* `MP4D_file_offset_t`, so the `offset < 0` guard was dead. Its
    out-params supply size and timestamp together, which also deleted an O(n²)
    loop that re-summed durations from zero for every sample (~162M iterations
    on a 10-minute 30 fps track).
  - `MP4D_HANDLER_TYPE_META`/`_NONE` do not exist — minimp4 defines only `VIDE`
    and `SOUN`. `track.duration_units` does not exist either (`duration_hi`/`lo`).
  - The read callback returned the byte count, but minimp4 treats **any truthy
    return as failure** (see `minimp4_fgets`), so a successful read read as an
    error. `MP4D_open` returns 1 on success and the caller tested `!= 0`.
  - `MP4D_open(..., 0)` passed a zero file size; minimp4 treats
    `read_pos >= read_size` as EOF, so parsing aborted at the first skip.
  - `MP4D_close()` was never called — the per-track index arrays leaked.
* **Format detection is real, and no longer guesses.** `extract_mime_type()` was
  a stub returning `""`, and the caller then *defaulted to v3* — which would have
  fed a proto-format ANAFI Ai file to the v3 binary parser and produced confident
  garbage. minimp4 cannot supply it: `MP4D_track_t::SampleDescription` is a union
  covering `soun` and `vide` only, so a metadata track's sample entry is parsed
  past and dropped. New `src/mp4_box_scan.{hpp,cpp}` walks the box tree directly
  (consistent with [ADR 0001](./decisions/0001-mp4-extraction-approach.md)) and
  recovers `tkhd` track IDs, `hdlr` handler types, the `stsd` sample entry, and
  `tref`/`cdsc` links. The MIME type now yields a typed `ParrotFormat`, and an
  unrecognised one is a hard error listing what was found.
* **Test fixtures are synthetic and committed.** `test/make_fixture.py` builds
  MP4s with Parrot's documented track layout and invented payloads, so extraction
  is testable without committing footage that carries live location data. Covers
  v3, proto, a writer that fills in `content_encoding` (moving the MIME type to
  the second string), and a non-Parrot `meta` track that must be refused. Wired
  into `ctest` as `mp4_extraction`; the suite passes, and the box walker survives
  6701 malformed inputs (every truncation plus every single-byte corruption at
  three values) clean under ASan+UBSan.
* **Field mapping rewritten against the complete ST 0601.19 registry.** libmisbklv
  commit `a981a1a` registers §8.1–8.143 (all but deprecated Item 66), validated
  against the standard's worked examples, so the mapping could finally be settled
  item by item instead of guessed.
  - [[field-mapping-analysis]] was **substantially wrong above tag 65** and is
    replaced, not amended. Its "missing tags" tables carried tag numbers written
    from assumption: 3, 53, 54, 92, 93, 103, 106, 107, 108, 114, 134 and 135 are
    all different items than it claimed, and the "Sensor Lat/Lon/Alt Extended" and
    "Platform Groundspeed Extended" items it proposed adding do not exist in 0601
    at all. It also premised a precision problem that isn't real — Items 13/14 are
    int32 mapped over ±90/±180, ~4.7 mm.
  - Its four-phase plan is gone with it. The old Phase 2/3 existed to extend
    `uas0601.toml`; **no registry work is required for this project.** Replaced by
    three tiers: direct mappings, derived geometry, VMTI.
  - Real items the full registry unlocked: 90/91 Platform Pitch/Roll (Full), which
    matter because Items 6/7 cap at ±20°/±50° and a quadcopter exceeds that
    routinely; 79/80 Sensor North/East Velocity with 51 Platform Vertical Speed
    (negated — §8.51 is zenith-positive, Parrot NED is down-positive); 75 Sensor
    Ellipsoid Height; 123 NAVSATs in View ← `sv_count`; 124/125/126 positioning
    and status enums; 134 Zoom Percentage; 40/41/42 Target Location ← the LFIC
    extension, which the old doc had written off as unavailable.
  - Re-read Parrot's `vmeta.proto` and the v2/v3 structs verbatim from
    `references/`, correcting two things the format doc obscured: proto `hfov`/
    `vfov` are **radians** while v2/v3 `picture_[hv]fov` are **degrees**, and
    proto `Location` carries **both** `altitude_egm96amsl` and
    `altitude_wgs84ellipsoid` — so the geoid conversion the old doc called for is
    unnecessary on proto.
* **Fork 1 opened, deliberated, and accepted: Parrot fields with no ST 0601 item**
  — [ADR 0002](./decisions/0002-unmappable-parrot-fields.md).

* **README made human-facing; rule rationale moved into the bundle.** The
  top-level README carried two agent-facing sections inherited from the template
  ("The doc taxonomy", "Why these rules"), which cluttered what a human sees and
  put workflow directives outside the docs agents read.
  - New [[workflow-rationale]] (`context/workflow-rationale.md`) is now the single
    home for the per-rule reasoning; added to `index.md` § Bundle guide, and
    `CLAUDE.md` § Planning hygiene points there instead of at the README.
  - "The doc taxonomy" was **deleted, not moved** — it duplicated
    [`CONVENTIONS.md`](./CONVENTIONS.md) § Layout, which already carries the same
    four-directory table.
  - README now runs: what it is → build → status pointer → a short "Working on
    this project" pointer → structure → license. It states no rules itself.
  - Same change upstream in `okf-project-template`, where the README keeps a short
    pitch that links to the rationale rather than hosting it, and `SETUP.md` now
    tells setup to delete both sections rather than keep them.
* **Doc-workflow consistency pass.** Three fixes to the docs describing the
  workflow itself:
  - `CLAUDE.md` — promoted "Before every commit checklist" from `###` to `##`.
    As a subsection it structurally swallowed the doc-role bullets and the "So:"
    rules that belong to § Planning hygiene, orphaning them from their intro
    sentence. Now a sibling section, still ahead of the hygiene rules.
  - `README.md` § "Why these rules" — added the missing rationale for the
    same-commit rule. The rule landed without one, which § Planning hygiene
    itself warns against ("a rule without its reason gets deleted the first time
    it's inconvenient").
  - `README.md` — completed the setup step that was skipped when this project was
    created: the file was still `okf-project-template`'s README verbatim (template
    title/intro, Quick Start, Manual setup, a dead `SETUP.md` link, 0BSD license
    line, template Structure block). Replaced with this project's identity, build
    instructions, real structure, and the Apache 2.0 license line. The doc taxonomy
    and "Why these rules" sections were kept, per `SETUP.md` § 2.
* The checklist and the same-commit rationale are being ported back upstream to
  `okf-project-template`; the README fix is local (the template's own README is
  correct as-is).

## 2026-07-23

* **Project initialized** from `okf-project-template`. Defined scope: convert
  Parrot telemetry from MP4 to MPEG-TS with MISB 0601 KLV. C++ with CMake,
  FetchContent for libmisbklv. License: Apache 2.0.
* **Domain types added**: `DataFormat` and `Standard` to the type vocabulary in
  CONVENTIONS.md.
* **Reference sources identified**: Parrot video metadata documentation (HTML),
  libvideo-metadata repo (GitHub, reference only), libmisbklv MISB specs
  (ST 0601.19, ST 0903.6, ST 1201.5).
* **Phase plan established**: Phase 0 (Foundation), Phase 1 (Parse), Phase 2
  (Transform), Phase 3 (Mux). First tasks: CMake setup, Parrot format research,
  field mapping documentation.
* **Parrot metadata formats researched**: Analyzed v1, v2, v3, and proto formats
  from documentation and libvideo-metadata source. Documented binary structures,
  fixed-point encoding, extension mechanism, and field categories. Created
  [[parrot-metadata-formats]] concept, [[parrot-video-metadata-docs]] and
  [[libvideo-metadata]] references.
* **Field mapping analysis completed**: Compared Parrot telemetry fields against
  ST 0601.19 tags in libmisbklv's registry. Identified 27 currently implemented
  tags, mapped Parrot fields to existing tags (Phase 1 viable with current impl),
  and documented high-priority missing tags (IMAPB extended items: 92-93, 103,
  106-108, 112-114) for Phase 2+. Created [[field-mapping-analysis]] with 3-phase
  implementation plan. Phase 0 Foundation tasks 1-3 complete.
* **Decision (accepted)**: ADR 0001 — Use direct MP4 box parsing for metadata
  extraction rather than GStreamer qtdemux. Investigation showed qtdemux doesn't
  expose metadata tracks as pads. Simple sequential parsing clearer for Phase 1.
  Will use lightweight library (minimp4) or custom minimal reader.
* **minimp4 vendored**: Added single-header MP4 parser (commit 4575afb, ~3.5K LOC,
  Unlicense/public domain) to `external/minimp4/`. Updated CMakeLists.txt to
  include vendored header. Standard practice for single-header libs (no
  build-time fetch, clear provenance via README).
* **Phase 1 started — MP4 extractor WIP**: Created `Mp4MetadataExtractor` class
  with minimp4 integration (pimpl pattern). Implements track finding, sample
  extraction API. Build integration with local libmisbklv via CMAKE_PREFIX_PATH.
  Status: Compiles with API corrections needed (minimp4 API differs from initial
  assumptions). Next: Fix API usage, test extraction with P0330036.MP4.
