# Decisions — ADR register

Architecture Decision Records: the *why* of resolved forks. This table is the
**register of decided forks** — the roadmap fork #, the ADR that resolved it, and
status. `decision_status` vocabulary and lifecycle in
[`../CONVENTIONS.md`](../CONVENTIONS.md) § Decisions. Forks *without* an ADR yet
(genuinely open) live in
[`../../planning/ROADMAP.md`](../../planning/ROADMAP.md); it points here for the
decided ones rather than duplicating them.

`Fork` is the roadmap fork number (ADR numbering is by creation order, so they
diverge; one fork can span several ADRs).

| Fork | ADR | Status |
|------|-----|--------|
| — | [0001 — Direct MP4 Parsing for Metadata Extraction](./0001-mp4-extraction-approach.md) | accepted |
| 1 | [0002 — Disposition of Parrot fields with no ST 0601 item](./0002-unmappable-parrot-fields.md) | accepted |
| 2 | [0003 — Protobuf dependency for the proto-format parser](./0003-protobuf-dependency.md) | accepted |
| 3 | [0004 — A shared telemetry snapshot between the parsers and the transform](./0004-shared-telemetry-snapshot.md) | accepted |
| 5 | [0005 — Out-of-range policy for Items 6/7, and the two escape hatches](./0005-attitude-range-policy.md) | accepted |
| 6 | [0006 — Accept libmisbklv's ST 0604 SEI injection; the video is no longer a pure passthrough](./0006-inherited-st0604-sei-injection.md) | accepted |
| 7 | [0007 — H.265 sources versus ST 0604 generation, which now excludes them](./0007-h265-vs-st0604-generation.md) | accepted |
| 8 | [0008 — `--force-emit-attitude` cannot do what it promises](./0008-force-emit-attitude-cannot-work.md) | accepted |
| 9 | [0009 — Ship a relocatable bundle, not a container and not a static binary](./0009-release-bundle.md) | accepted |
| 10 | [0010 — A container image as a second deliverable, built from the bundle](./0010-container-deliverable.md) | accepted |
| 11 | [0011 — Inferring Item 11 for formats that state no spectrum](./0011-inferred-image-source-sensor.md) | accepted |
| 12 | [0012 — Re-vendor minimp4.h onto upstream's overflow fix rather than patch it locally](./0012-revendor-minimp4-overflow-fix.md) | accepted |
| 13 | [0013 — Live ingest and multicast output — vendor extraction stays here, generic mux comes from libmisbklv](./0013-live-streaming-ingest.md) | accepted |
| 13 | [0014 — Unified -i/-o CLI with input/output type inference](./0014-unified-cli-input-output-inference.md) | accepted |

_ADR 0001 predates formal fork numbering (no fork #): chosen direct MP4 parsing
over GStreamer qtdemux based on investigation._
