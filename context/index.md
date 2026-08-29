---
okf_version: "0.2"
---

# Parrot to KLV Knowledge Bundle

Agent-facing knowledge base for this project, in
[OKF](https://github.com/GoogleCloudPlatform/knowledge-catalog/blob/main/okf/SPEC.md)
v0.2. The agent owns this directory; humans curate sources and direct the work.

Read [`CONVENTIONS.md`](./CONVENTIONS.md) before ingesting or editing.

## Bundle guide

* [Conventions](./CONVENTIONS.md) — frontmatter, types, linking, ingest/query/lint rules.
* [Why these rules](./workflow-rationale.md) — the failure mode behind each
  planning-hygiene rule in `AGENTS.md`. Read before relaxing one.

## References

_(`type: Reference` — synthesis of an external source, citing the immutable snapshot in `../references/`.)_

* [Parrot Video Metadata Documentation](./parrot-video-metadata-docs.md) — Official format specs (snapshot withdrawn per ADR 0016; link file + BSD-3 `vmeta.proto` in `../references/`)
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
* [0005 — Out-of-range policy for Items 6/7](./decisions/0005-attitude-range-policy.md) — Keep the `0x8000` sentinel as the default; `--clamp-attitude` is the opt-in escape hatch (a second one was removed, see 0008)
* [0006 — Accept libmisbklv's ST 0604 SEI injection](./decisions/0006-inherited-st0604-sei-injection.md) — The video is never decoded but is no longer bit-identical; stop calling it a passthrough
* [0007 — H.265 versus ST 0604 generation](./decisions/0007-h265-vs-st0604-generation.md) — Ask for ST 0604 only on H.264; carry any other codec through unstamped rather than refusing it
* [0008 — `--force-emit-attitude` cannot do what it promises](./decisions/0008-force-emit-attitude-cannot-work.md) — The encoding has no headroom past ±limit, so the flag only ever emitted the clamped value; removed
* [0009 — Ship a relocatable bundle](./decisions/0009-release-bundle.md) — Build on the oldest target and bundle GStreamer + the plugins used, so a user extracts and runs with nothing installed
* [0010 — A container image as a second deliverable](./decisions/0010-container-deliverable.md) — Exported distroless image built from the bundle, with a launcher that mounts only what is in play and runs as the caller
* [0011 — Inferring Item 11 for formats that state no spectrum](./decisions/0011-inferred-image-source-sensor.md) — v3 states no spectrum; `--infer-image-source` guesses EO/IR from thermal-metadata presence, opt-in and labeled `(inferred)`
* [0012 — Re-vendor minimp4.h onto upstream's overflow fix](./decisions/0012-revendor-minimp4-overflow-fix.md) — Pull forward to a current upstream commit rather than patch the vendored file directly, closing a `stsz` heap-overflow write and an stts/stsz OOB read
* [0013 — Live ingest and multicast output — vendor extraction stays here, generic mux comes from libmisbklv](./decisions/0013-live-streaming-ingest.md) — Split on the dependency line — libmisbklv owns generic live mux/pacing/multicast, parrot-to-klv owns RTSP pull, RTP header-extension capture, and v3/proto decode + transform
* [0014 — Unified -i/-o CLI with input/output type inference](./decisions/0014-unified-cli-input-output-inference.md) — Replace the positional/live-flag split with inferred -i/-o (rtsp://, rtsps://, pipeline: → live; udp:, udp://, srt:, path → output), remove --live-rtsp/--live-udp with a loud error, and defer file→udp replay
* [0015 — File replay to a live sink — MP4 -> UDP/SRT multicast](./decisions/0015-file-replay-live-output.md) — Replay an MP4's video on the pipeline clock and emit its mett-track telemetry as KLV (PTS = zero-based file running time) to reach `-i file.mp4 -o udp:/srt:`, paced emission, play-through-close by default with `--repeat` to loop
* [0019 — Share a two-slot capacity gate across self-hosted CI](./decisions/0019-shared-ci-capacity-gate.md) — Bound combined self-hosted workload while allowing the parrot build and sanitizer jobs to run concurrently
