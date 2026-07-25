---
okf_version: "0.1"
---

# Parrot to KLV Knowledge Bundle

Agent-facing knowledge base for this project, in
[OKF](https://github.com/GoogleCloudPlatform/knowledge-catalog/blob/main/okf/SPEC.md)
v0.1. The agent owns this directory; humans curate sources and direct the work.

Read [`CONVENTIONS.md`](./CONVENTIONS.md) before ingesting or editing.

## Bundle guide

* [Conventions](./CONVENTIONS.md) — frontmatter, types, linking, ingest/query/lint rules.
* [Why these rules](./workflow-rationale.md) — the failure mode behind each
  planning-hygiene rule in `CLAUDE.md`. Read before relaxing one.

## References

_(`type: Reference` — synthesis of an external source, citing the immutable original in `../references/`.)_

* [Parrot Video Metadata Documentation](./parrot-video-metadata-docs.md) — Official format specs (HTML in `../references/`)
* [libvideo-metadata](./libvideo-metadata.md) — Parrot's C reference implementation (GitHub, not built as dependency)

## Data Formats

_(`type: DataFormat` — specification of data formats.)_

* [Parrot Video Metadata Formats](./parrot-metadata-formats.md) — Binary specifications for v1, v2, v3, proto formats

## Standards

_(`type: Standard` — formal standard documents.)_

## Concepts

_(`type: Concept` — durable domain / design knowledge.)_

* [Parrot to MISB 0601 Field Mapping Analysis](./field-mapping-analysis.md) — Which Parrot fields map to which ST 0601.19 items, what must be derived geometrically, what has no home in 0601

## Components

_(`type: Component` — modules / subsystems of the project.)_

## Decisions

_(`type: Decision` — ADRs: the *why* of resolved forks.)_

* [Decisions register](./decisions/index.md) — ADRs grouped under `decisions/`
* [0001 — Direct MP4 Parsing for Metadata Extraction](./decisions/0001-mp4-extraction-approach.md) — Use lightweight box parsing, not GStreamer qtdemux
* [0002 — Disposition of Parrot fields with no ST 0601 item](./decisions/0002-unmappable-parrot-fields.md) — *(proposed)* Drop and report, don't repurpose items
