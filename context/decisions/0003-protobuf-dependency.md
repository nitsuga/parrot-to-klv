---
type: Decision
title: ADR 0003 — Protobuf dependency for the proto-format parser
description: Hand-roll a minimal proto3 wire reader scoped to the mapped subset of vmeta.proto, rather than taking a protobuf toolchain and runtime dependency
status: accepted
fork: 2
tags: [decision, parsing, protobuf, parrot, phase-1]
timestamp: 2026-07-26T00:37:34Z
---

# Context

Phase 1 owes a parser for Parrot's `proto` format (identifier `0x5062`, ANAFI Ai
and ANAFI UKR, ≥ 2021) alongside the v3 binary parser that already works. The
proto format is proto3-serialized `vmeta.TimedMetadata`, and it is not
redundant with v3: per [[field-mapping-analysis]] it uniquely supplies both
altitude datums (`altitude_egm96amsl` *and* `altitude_wgs84ellipsoid`, so Item
75 needs no geoid model), zoom level (Item 134), LFIC as a first-class
cursor-on-target source with accuracy estimates (Items 40–42, 45–46), and a UTC
timestamp with a validity indicator (Item 2, instead of anchoring a monotonic
clock to `media_date`).

Four facts shape the choice, and three of them were not obvious before looking:

**The schema is already in `references/`, not just cited by it.** The complete
`vmeta.proto` text is embedded in
`references/Embedded Video Metadata - 8.4.2.html` — `syntax = "proto3"`,
`package vmeta`, 35 messages, 18 enums, ~316 fields. Nothing needs to be
fetched from Parrot's SDK, and the schema is immutable-by-convention where it
sits.

**The mapped subset is a fraction of it.** The Parrot → 0601 mapping draws on
about 13 messages totalling ~83 fields — `TimedMetadata`, `DroneMetadata`,
`CameraMetadata`, `Location`, `Quaternion`, `NED`, `LinkMetadata`,
`WifiLinkMetadata`, `LFICMetadata`, `TrackingMetadata`, `BoundingBox`,
`Vector2`, `StarfishLinkMetadata` — and the 0601-relevant fields within those
are fewer still. The remaining ~22 messages are camera models, fisheye
distortion, polynomials, affine matrices, overlays, photo and session metadata,
and the thermal group — all of which [[0002-unmappable-parrot-fields]]
already drops.

**The subset is nearly free of the hard parts of protobuf.** Across the whole
schema there are two `google.protobuf` wrapper fields (`battery_voltage` as
`FloatValue`, `symmetric` as `BoolValue`) and three `oneof`s; of those, only one
`oneof` (`LinkMetadata.protocol`) falls inside the mapped subset, and neither
wrapper does — both sit in fields ADR 0002 drops. There are no `map` fields. The
subset needs: varint, `fixed32`/`fixed64`, length-delimited, two `repeated`
message fields (`links`, `lfic`), one `oneof` (which is just last-wins fields on
the wire), and skip-by-wire-type for everything else. Scalar presence is not
needed, because the schema encodes "unknown" in-band as documented in
[[field-mapping-analysis]] — `altitude_egm96amsl == 0` means unknown,
`utc_timestamp_accuracy == 0` means the timestamp is invalid.

**No real proto footage exists to validate against.** Every real recording this
project has seen is v3 (see the committed conformance reports — both ANAFI, both
`format: v3`). Whichever option is chosen, the proto parser ships validated
against synthetic fixtures only, until an ANAFI Ai recording turns up. That cuts
against paying a large dependency cost, since the cost cannot currently buy
validated correctness either.

# Decision

**Hand-roll a minimal proto3 wire reader, scoped to the mapped subset**, rather
than depending on protobuf's compiler or runtime.

Concretely:

- A small wire-format layer — varint, `fixed32`, `fixed64`, length-delimited,
  and skip-unknown-by-wire-type — over the sample bytes the existing extractor
  already hands us.
- Per-message field dispatch written only for the mapped subset, decoding into a
  `ParrotProtoFrame` shaped like the existing `ParrotV3Frame` so the transform
  stage sees one shape.
- **Unknown fields are skipped, not errors.** This is the forward-compatibility
  property that makes protobuf attractive, and it is a dozen lines: read tag,
  switch on wire type, advance. A future Parrot field addition does not break
  the parser.
- Transcribe the schema subset from the references HTML into a committed
  `.proto` file for *documentation and fixture generation*, not for build-time
  compilation.

**Validation strategy, which is what makes this defensible without footage:**
`test/make_fixture.py` generates the proto fixtures and the C++ reader must
decode them. The intended form is a genuine cross-implementation check — the
reference Python protobuf library encoding against the transcribed schema on one
side, our reader on the other — costing a *developer-time* Python dependency,
not a build or runtime one.

**That check is not in force yet.** The development machine has neither `protoc`
nor Python's `protobuf` package, and no `pip` to install one, so the fixtures
are encoded by a hand-written proto3 encoder in `make_fixture.py`. Encoder and
decoder are then both ours, which still catches asymmetric mistakes — a field
number or wire type wrong on one side only — but is materially weaker than an
independent encoder, and does not catch a shared misreading of the wire format.
Wiring the real library in, where one is available, is tracked as an open
question below. Neither form catches a schema *transcription* error, since both
sides share the transcription; that is checked by eye against `references/` and
cited in the transcribed file.

# Alternatives considered

## Depend on protobuf: `protoc` at build time plus the C++ runtime

The orthodox answer: vendor the `.proto`, run `protoc` from CMake, link
`libprotobuf`.

**Rejected as disproportionate.** It is the heaviest dependency in the project
by a wide margin — protobuf ≥ 22 additionally requires Abseil, so a FetchContent
build pulls and compiles two substantial libraries, and `find_package` instead
makes a system protobuf a prerequisite for a tool whose only other dependencies
are two vendored single headers and libmisbklv. It also couples us to a
protoc/runtime version pair and complicates cross-compilation. All of that to
decode ~13 messages whose hard features we do not use, for a format we cannot
yet validate against a real recording.

The genuine advantage — generated code is derived from the schema and therefore
cannot misread it — is real but narrower than it looks, since a transcription
error in the `.proto` would be common-mode across generated code and fixtures
alike.

## Vendor pre-generated `.pb.cc` / `.pb.h`

Removes `protoc` from the build while keeping the generated sources.

**Rejected.** Strictly worse than the above: it keeps the `libprotobuf` runtime
dependency *and* the version coupling (generated code must match the runtime),
while adding thousands of lines of machine-generated source to the repository
and losing the one thing the toolchain gives you — regeneration from the schema.

## nanopb

A small proto3 implementation with a tiny runtime and a Python generator.

**Rejected, but the closest call.** It genuinely addresses the size objection.
It still introduces a code-generation step and a third-party runtime, plus
nanopb's own callback idioms for `repeated` and unbounded fields, which is
comparable in explanatory cost to the reader this ADR proposes — while being
harder to reason about than code we wrote. Reconsider if the mapped subset grows
substantially, or if a second proto-based format appears.

## Decode the whole schema by hand

Hand-roll, but cover all 35 messages for completeness.

**Rejected.** ~22 of them decode into fields ADR 0002 already drops. Writing and
maintaining decoders for thermal calibration, fisheye distortion and overlay
metadata buys nothing the tool can emit, and every such decoder is code we
cannot validate.

# Consequences

- The project keeps its dependency profile: libmisbklv via FetchContent, two
  vendored single headers, no toolchain requirements beyond CMake and a C++
  compiler. Building stays a `cmake -S . -B build && cmake --build build`.
- We own a wire-format decoder. Proto3's wire format is small, stable and
  fully specified, so this is bounded work — but it is our bug surface, not
  Google's, and the reader must be written defensively against malformed input
  (truncated varints, lengths that overrun the sample, nesting depth).
- Adding a proto field later means editing the dispatch by hand. Cheap per
  field; the cost is that it will silently *not* happen if someone forgets — the
  parser will skip the new field rather than fail. That is the right failure mode
  for a converter, but it means new-field support is a deliberate act.
- The transcribed `.proto` is a second copy of a schema that lives in
  `references/`. It can drift from the reference if Parrot publishes 8.5. Its
  provenance must be cited in-file, and re-checked when a new reference lands.
- Fixture generation would gain a developer-time dependency on Python's protobuf
  package, if one becomes available. `make_fixture.py` already exists and is
  developer-time only; the generated fixtures stay committed, so building and
  testing the project never needs it. Until then the hand-written encoder in
  that script is a second thing to maintain, and a second place a wire-format
  misunderstanding can hide.
- If an ANAFI Ai recording ever arrives, `parrot-conform` extends to it and the
  same real-encoder evidence becomes available for proto that v3 now has. The
  reader should be written expecting that scrutiny.

# Assumptions / open questions

- Assumes the reference HTML's embedded schema is the authoritative 8.4.2
  `vmeta.proto`. It is presented as the file's contents and is internally
  consistent, but it has not been diffed against Parrot's SDK repository.
- Assumes the mapped subset is stable. If Tier 2 geometry work (the other
  candidate fork) or a future consumer pulls in thermal or camera-model data,
  the subset grows and the nanopb trade-off should be revisited.
- The `0x4536` extension undocumented in v3 may be a binary LFIC
  ([[field-mapping-analysis]] Items 40–42). If that is confirmed, the proto
  `LFICMetadata` decode is the reference implementation to check it against —
  which is an argument for decoding LFIC carefully even though v3 is the format
  we have footage for.
- **Open:** re-establish the independent-encoder fixture check when a machine
  with `protoc` or Python `protobuf` is available. Until then the cross-check is
  between two implementations we wrote.
- Unsettled, and deliberately not decided here: whether v3 and proto converge on
  a shared intermediate frame struct before the transform stage, or the
  transform stage takes two inputs. That is a Phase 2 shape question.

# Citations

- `references/Embedded Video Metadata - 8.4.2.html` — the embedded
  `vmeta.proto` schema (`syntax = "proto3"`, `package vmeta`, `TimedMetadata`)
- [[parrot-metadata-formats]] § Protobuf Format (proto) — format identity and
  the enum-numbering divergence between binary and protobuf
- [[field-mapping-analysis]] — what proto uniquely supplies (Items 2, 15, 45/46,
  75, 134) and the in-band "unknown" conventions
- [[0002-unmappable-parrot-fields]] — why most of the schema is out of scope
- [[0001-mp4-extraction-approach]] — the precedent: a narrow hand-written
  parser preferred over a general-purpose framework for a bounded format
- [[libvideo-metadata]] — Parrot's C reference implementation, read for
  behavior but already established as not a build dependency; its
  `proto/vmeta.proto` is the schema this ADR transcribes a subset of
