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

* [MP4 metadata extraction](./mp4-extraction.md) — locating, reading and scrubbing
  the Parrot track; the minimp4 / box-scan split, minimp4's traps, and how a real
  recording becomes a shareable fixture — plus conformance reports, which commit
  real-encoder evidence without the recording
* [Transform stage](./transform-stage.md) — Parrot telemetry to ST 0601: the
  format-neutral snapshot, the Tier 1 mapping and its sign/range traps, and the
  conversion coverage report
* [Mux stage](./mux-stage.md) — MP4 to MPEG-TS with the source's video never
  decoded, though its timing NALs are rewritten to carry ST 0604: the one
  timeline both branches share, and what a failed conversion must not leave
  behind

## Decisions

_(`type: Decision` — ADRs: the *why* of resolved forks.)_

* [Decisions register](./decisions/index.md) — ADRs grouped under `decisions/`
* [0001 — Direct MP4 Parsing for Metadata Extraction](./decisions/0001-mp4-extraction-approach.md) — Use lightweight box parsing, not GStreamer qtdemux
* [0002 — Disposition of Parrot fields with no ST 0601 item](./decisions/0002-unmappable-parrot-fields.md) — Drop and report, don't repurpose items
* [0003 — Protobuf dependency for the proto-format parser](./decisions/0003-protobuf-dependency.md) — Hand-roll a minimal proto3 reader, no toolchain dependency
* [0004 — A shared telemetry snapshot](./decisions/0004-shared-telemetry-snapshot.md) — Normalize both formats into one struct before mapping
* [0005 — Out-of-range policy for Items 6/7](./decisions/0005-attitude-range-policy.md) — Keep the `0x8000` sentinel as the default; clamping and out-of-spec emission are opt-in flags
* [0006 — Accept libmisbklv's ST 0604 SEI injection](./decisions/0006-inherited-st0604-sei-injection.md) — The video is never decoded but is no longer bit-identical; stop calling it a passthrough
* [0007 — H.265 versus ST 0604 generation](./decisions/0007-h265-vs-st0604-generation.md) — Ask for ST 0604 only on H.264; carry any other codec through unstamped rather than refusing it
* [0008 — `--force-emit-attitude` cannot do what it promises](./decisions/0008-force-emit-attitude-cannot-work.md) — **proposed**: the encoding has no headroom, so the flag emits the clamped value under an out-of-spec label; remove it
