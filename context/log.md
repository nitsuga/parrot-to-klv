# Knowledge Bundle Log

<!-- Durable chronological record, newest first, ISO YYYY-MM-DD day headings.
     One thin line per decision (chronology + link — the ADR owns the rationale);
     a fuller entry per significant implementation. Routine ingests/lint go here
     too. This is the ONLY home for history — PROGRESS.md is present-tense only. -->

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
