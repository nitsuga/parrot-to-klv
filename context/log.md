# Knowledge Bundle Log

<!-- Durable chronological record, newest first, ISO YYYY-MM-DD day headings.
     One thin line per decision (chronology + link — the ADR owns the rationale);
     a fuller entry per significant implementation. Routine ingests/lint go here
     too. This is the ONLY home for history — PROGRESS.md is present-tense only. -->

## 2026-07-25

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
