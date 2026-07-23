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

## Components

_(`type: Component` — modules / subsystems of the project.)_

## Decisions

_(`type: Decision` — ADRs: the *why* of resolved forks.)_

* [Decisions register](./decisions/index.md) — ADRs grouped under `decisions/`.
