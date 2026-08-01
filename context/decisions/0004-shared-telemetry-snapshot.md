---
type: Decision
title: ADR 0004 — A shared telemetry snapshot between the parsers and the transform
description: Normalize v3 and proto frames into one format-neutral snapshot before mapping to ST 0601, rather than giving the transform stage two inputs
decision_status: accepted
fork: 3
tags: [decision, transform, mapping, misb, 0601, phase-2]
generated:
  by: claude/opus-5
  at: 2026-07-26T02:10:00Z
---

# Context

[[0003-protobuf-dependency]] left this explicitly unsettled: "whether v3 and
proto converge on a shared intermediate frame struct before the transform stage,
or the transform stage takes two inputs. That is a Phase 2 shape question."
Phase 2 has started, so it needs an answer.

The two parsers produce deliberately distinct types — `ParrotV3Frame` and
`ParrotProtoFrame` — and that distinction is load-bearing, because the formats
genuinely disagree:

- **Units.** Proto carries the FOVs in radians, v3 in degrees.
- **Enum numbering.** Proto `PM_MANUAL` is 1, binary `Manual` is 0, and the two
  disagree on the last pair. [[field-mapping-analysis]] is emphatic: map through
  the *names*, never the numbers.
- **Coverage.** Proto supplies both altitude datums, zoom and LFIC; v3 supplies
  one datum, an automation extension, and air speed, which proto lacks.
- **Time.** Proto has a UTC stamp with a validity flag; v3 has a monotonic clock
  that needs anchoring to session metadata.

Meanwhile the ST 0601 mapping is *mostly* common: Items 5/6/7/90/91 are the same
quaternion-to-Euler conversion, 13/14/15 the same position, 79/80/51/56/112 the
same NED velocity arithmetic. Writing that twice would mean two places to get
the sign of Item 51 wrong.

# Decision

**Normalize both frame types into one format-neutral `TelemetrySnapshot`, then
map that to ST 0601 once.**

The snapshot is where per-format knowledge stops:

- It carries **one unit convention** — degrees for angles, m/s for velocity,
  meters for distance — so the radians/degrees asymmetry is resolved on the way
  in and cannot reach the mapping.
- It carries **common enums** (`CommonPilotingMode`) that both parsers map into
  *by name*, so the numbering divergence is resolved once, at the two places
  that know their own format's numbering.
- It uses `std::optional` for everything a format may not supply, so "v3 has no
  ellipsoid height" and "proto's altitude was the unknown sentinel" are the same
  shape to the mapping, and Item 75 is simply omitted rather than special-cased.

The transform stage then reads only the snapshot. It never sees a
`ParrotV3Frame` or a `ParrotProtoFrame`, and cannot accidentally depend on
either format's quirks.

# Alternatives considered

## Two mapping paths, one per format

The transform stage takes either frame type, overloaded or templated.

**Rejected.** It duplicates the arithmetic that is genuinely shared — most of
Tier 1 — and the duplication is the dangerous kind: sign conventions, wrap
ranges, and special-value handling that must agree between the two paths but
have no mechanism forcing them to. The v2 parser, when it arrives, would make it
three.

## Make the parsers emit the snapshot directly

Skip the intermediate types; `parse_v3` returns a `TelemetrySnapshot`.

**Rejected.** It would discard the fields 0601 has no home for — battery, link
telemetry, thermal, exposure — which [[0002-unmappable-parrot-fields]] requires
the tool to *report* rather than silently drop. The parsers' job is to represent
the recording faithfully; narrowing to what 0601 can carry is the transform's
job, and the coverage report depends on being able to see the difference.

## One frame type for both formats

Collapse `ParrotV3Frame` and `ParrotProtoFrame` into a union-ish type.

**Rejected** for the reason ADR 0003 kept them apart: a shared type invites
sharing code paths that must not be shared, and makes the units and enum
divergences invisible at the call site rather than obvious.

# Consequences

- Three layers, each with one job: parse (faithful to the format) → normalize
  (resolve units and enums) → map (0601 semantics). A future v2 parser adds one
  normalizer and touches nothing else.
- The snapshot is a third structure to keep in step with the two frame types.
  Where a field exists in exactly one format it still has to be threaded
  through, so this is not free — it buys single-sourcing of the shared
  arithmetic, not less total code.
- The coverage report is naturally produced at the normalize/map boundary, since
  that is exactly where a Parrot field either becomes an item or is dropped.
- Optionality is uniform, so the mapping's rule for a missing input is "omit the
  item", once, rather than per format.

# Assumptions / open questions

- Assumes v1/v2 will normalize into the same snapshot. v2 is a subset of v3 in
  substance, so this looks safe; v1 is deprecated and may never be implemented.
- Session-level metadata (platform designation, tail number — Items 4/10) is
  *not* in the snapshot; it is per-file rather than per-frame, so `to_0601()`
  takes it as a separate argument. **Since resolved**: `src/session_metadata.*`
  parses it, and Items 4 and 10 are emitted where the recording carries the
  tags. Item 11 came from proto's `camera.spectrum` and `camera.subtype`
  instead, which *are* per-frame and did join the snapshot.

# Citations

- [[0003-protobuf-dependency]] § Assumptions — where this question was deferred
- [[field-mapping-analysis]] § Enum maps — "map through the names, never the
  numbers", the constraint that shapes the common enums
- [[0002-unmappable-parrot-fields]] — why the parsers must keep what 0601 drops
