---
type: Component
title: Transform stage — Parrot telemetry to MISB ST 0601
description: How a decoded Parrot frame becomes an ST 0601 packet — the format-neutral snapshot, the Tier 1 mapping and its sign/range traps, and the conversion coverage report
tags: [transform, mapping, misb, 0601, phase-2]
generated:
  by: claude/opus-5
  at: 2026-07-31T16:36:53Z
---

# Transform stage

Turns a decoded Parrot frame into an ST 0601 KLV packet. The second stage of the
pipeline: it consumes what the parsers produce and hands packets to the muxer.

The mapping itself — which Parrot field becomes which item — is specified in
[[field-mapping-analysis]]. This document covers the *implementation*: its shape,
its traps, and what it refuses to do.

## Files

| File | Role |
|------|------|
| `src/telemetry.{hpp,cpp}` | `TelemetrySnapshot` + `from_v3()` / `from_proto()` — the normalization boundary |
| `src/session_metadata.{hpp,cpp}` | Session tags — Items 4/10 and the v3 wall-clock anchor (see [[mp4-extraction]]) |
| `src/transform.{hpp,cpp}` | `to_0601()`, the Euler conversions, and `CoverageReport` |
| `test/transform_test.cpp` | `ctest` target `transform` |
| `test/session_metadata_test.cpp` | `ctest` target `session_metadata` |

## Three layers, one job each

Per [ADR 0004](./decisions/0004-shared-telemetry-snapshot.md):

**parse** (faithful to the format) → **normalize** (resolve units and enums) →
**map** (0601 semantics).

The snapshot is where per-format knowledge stops. It carries one unit convention
— degrees, m/s, meters — so proto's radian FOVs and v3's degree FOVs are the
same thing by the time anything maps them. It carries `CommonPilotingMode`,
which both parsers reach **by name**, because the binary and protobuf schemas
number the same concepts differently. And it uses `std::optional` for anything a
format may not supply, so the mapping's rule for a missing input is uniformly
"omit the item".

The transform never sees a `ParrotV3Frame` or a `ParrotProtoFrame`. A future v2
parser adds one normalizer and touches nothing else.

## What it emits

Tier 1 of the mapping — every direct field-to-field item with a source in the
recording — encoded through libmisbklv's registry codec and assembled with the
UL key, BER length and Item 1 checksum. A proto frame yields more than a v3 one,
since proto carries the ellipsoid height, zoom, LFIC and the camera spectrum
that v3 has no equivalent for. The packet is a real 0601 packet: `transform_test` parses every one it
produces back through `misbklv::parse_packet` and decodes item by item, so
values are checked as a consumer sees them rather than as we staged them.

## Traps this code exists to get right

- **Item 51 is negated.** 0601 §8.51 measures vertical speed toward zenith;
  Parrot NED is positive down. The sign flip lives in exactly one place.
- **Items 6/7 are ±20° and ±50°**, which a quadcopter exceeds in ordinary
  flight. Out of range is signaled with the `0x8000` special value by default,
  not clamped — a clamped attitude is a confident lie — and Items 90/91 (±90°)
  carry the trustworthy value alongside. Two opt-in flags override the default
  for consumers that cannot parse the sentinel (`--clamp-attitude`) or that want
  fidelity over conformance (`--force-emit-attitude`, non-compliant); the
  coverage report labels it, and 90/91 stay accurate either way. Why the
  default survived contact with a decoder that chokes on `0x8000`:
  [[0005-attitude-range-policy]]. A second flag promising the true angle shipped
  briefly and was removed — the encoding has no headroom for one
  ([[0008-force-emit-attitude-cannot-work]]).
- **Item 11 is inferred, or nothing.** v3 states no imaging spectrum, so the
  sensor cannot be *named* from a v3 recording — only guessed at, from whether
  the frame carried the thermal extension. `--infer-image-source` does that,
  off by default, never overriding a spectrum the recording states, and marked
  `(inferred)` in the report. The guess is wrong for blended EO/IR footage and
  wrong in the damaging direction for a thermal recording that wrote no thermal
  metadata: [[0011-inferred-image-source-sensor]].
- **Item 112 is undefined at rest.** Course over ground is `atan2` of two
  numbers that are pure noise when the aircraft is stationary, so it is omitted
  below 0.5 m/s rather than reporting a direction of travel that does not exist.
- **Zero is not a value in proto.** Proto3 gives scalars no presence bit, so
  Parrot signals "unknown" in band: a zero altitude means no fix,
  `utc_timestamp_accuracy == 0` means the UTC stamp is invalid, and zero
  lat/lon (camera and drone both) means no position fix at all — the camera
  location wins when it alone has a fix, but if neither location has one, Items
  13/14 stay empty rather than reporting 0°N 0°E. Zero AGL follows the same
  rule as altitude: it is wire-identical to "not reported," so a landed drone's
  genuine 0 m reading and a no-data frame cannot be told apart, and the project
  takes the same tradeoff it already takes for altitude, zoom and the FOVs
  rather than risk a fabricated ground clearance. All of these become an empty
  optional at normalization; treating any of them as a number would put the
  aircraft at sea level, 0°N 0°E, on the ground, or stamp the packet with a
  confident wrong time.
- **`asin` is clamped** before computing pitch. A v3 quaternion that is barely
  non-unit from fixed-point rounding can push the argument past 1 and yield NaN
  anyway — a NaN pitch/roll that reaches `put_narrow_angle` (Items 6/7) is
  checked for finiteness before the range comparison, not after: `std::abs(NaN)
  <= limit` is false, so an unguarded comparison falls into the same branch as
  a genuinely out-of-range angle and mislabels "not a measurement" as
  "measurement out of range."
- **Item 124 has no all-zero encoding.** ST 0601.19 §8.124.1 forbids it
  outright. A frame with `sv_count == 0` still reports Item 123 truthfully —
  zero satellites in use is a real, valid count — but Item 124 (which
  constellation) is omitted rather than encoded as the forbidden `0x00`.

## Invalid values are omissions, not approximations

The transform treats a non-finite source value as invalid and omits every item
that depends on it. In particular, it rejects non-finite north/east velocity
before deriving ground speed or course. Horizontal components remain independent:
a bad north component does not discard a valid east component (or vice versa),
but a derived value requiring both is absent. Course at rest remains explicitly
undefined, rather than an arbitrary `atan2(0, 0)` direction.

Finite values can still be outside a target codec's representable range. That is
an encode rejection, not a missing source field: the coverage report says the
item is not encodable and omits it rather than saturating it. Item 56 therefore
does not turn a ground speed above 255 m/s into a false 255 m/s measurement;
Items 8 and 123 apply the same rule to airspeed and satellite count. The only
output clamp is the documented attitude opt-in above, which labels the altered
value and preserves the truthful full-range value alongside it.

This extends the same truthfulness rule already used for unavailable inputs and
does not alter Item 2's existing mandatory-item handling: where its time source
is unavailable or invalid, it remains absent with its own reported reason.

## Item 2 is mandatory, and sometimes absent anyway

ST 0601 requires the Precision Time Stamp. A v3 recording carries only a
monotonic frame clock, and a proto frame may declare its UTC stamp invalid.

Where the file has session metadata, `anchor_to_session()` closes the v3 gap:
the `©day` media date is the first sample's wall time, and each frame adds its
elapsed monotonic microseconds. The anchor inherits the media date's *second*
granularity — see [[mp4-extraction]] § Session metadata — so absolute time is
good to a second while frame spacing stays exact.

Where it does not (no session tags, no parsable date, or a proto frame whose
`utc_timestamp_accuracy` is zero), the packet is assembled **without**
mandatory-item enforcement, Item 2 is absent, and the coverage report says why.
Emitting a fabricated timestamp would be worse: a consumer cannot tell a wrong
time from a right one, but it can tell a missing item from a present one.

## The coverage report

[ADR 0002](./decisions/0002-unmappable-parrot-fields.md) requires conversion
loss to be reported rather than implied. `CoverageReport` is that obligation
made concrete, and it is a first-class output — `parrot-to-klv --klv` prints it
per frame:

- **emitted** — every item that made it, one per line, with its value. Number,
  name and units all come from libmisbklv's ST 0601 registry rather than a
  second copy of the standard kept here, so the report cannot drift from what
  was encoded. The number stays alongside the name because that is how ST 0601
  indexes an item and how these docs refer to it
- **omitted** — items the mapping knows about but did not emit, each with a
  reason: the format does not carry it, this frame did not, it needs terrain
  intersection, it needs session metadata, the source value is invalid, the
  finite value is not encodable, or the mapping itself is an open question
- **dropped** — Parrot fields with no 0601 item at all (battery, link telemetry,
  exposure, thermal), listed only when the frame actually carried them, so the
  report describes the recording rather than reciting the schema

**Sensitive values are withheld from the report by default**, exactly as they
are from the frame summaries. A coverage report is precisely the kind of output
someone pastes into an issue. Two separate opt-ins, because they are separate
risks: `Disclosure::location` covers Items 13/14 and 40/41 (where the flight
was), `Disclosure::identity` covers Item 4 (which airframe flew it). The CLI
spells them `--with-location` and `--with-identity`.

## What it deliberately does not do

- **Tier 2 derived geometry** (Items 21/22/23/24/25/57/78/82–89) needs a
  boresight-to-terrain intersection whose fidelity — flat-earth vs DTED — is
  still an open fork in [ROADMAP](../planning/ROADMAP.md). Reported as
  `needs terrain intersection`, not approximated silently.
- **Item 134 zoom.** Parrot's `zoom_level` is a multiplier; 0601 wants a
  percentage. No defined correspondence exists, so inventing one would put a
  fabricated number in a standard field.
- **Items 45/46 CE90/LE90.** Parrot's radial and tangential horizontal
  accuracies are not literally CE90/LE90. Withheld rather than relabeled.
Items 4 and 10 (tail number, platform designation) come from session metadata
when the recording has it, and are reported as `needs MP4 session metadata`
when it does not. Item 11 (image source sensor) comes from proto's
`camera.spectrum`; v3 carries nothing equivalent, so it is reported against the
*format* there rather than against the frame.

### Item 11 names the sensor

Item 11 is free-form utf8 — "name of currently active sensor" — not a
two-valued enum, and ST 0601 §8.11.1's own example configuration names sensors
`EO Narrow`, `EO Wide` and `IR Sensor`. So the name is composed from two proto
fields:

- **`camera.spectrum` gives the stem**: `EO`, `IR`, or `EO/IR Blended` for a
  fused image, named for what it is rather than forced into one of its halves.
- **`camera.subtype` qualifies it**: `EO Wide`, `IR Tele`, `EO Left`.

An unknown **spectrum** names nothing, even when the subtype is known — a bare
`Wide` does not say whether the imagery is visible or thermal, which is the one
thing Item 11 exists to convey, and thermal footage reported as `EO` is exactly
the confident wrong value this mapping avoids elsewhere. An unknown **subtype**
simply leaves the stem unqualified.

**Why:** the mapping's hard parts are not the arithmetic but the places where
0601 and Parrot disagree about sign, range, or what zero means — and the places
where the honest answer is to emit nothing.

**How to apply:** add a mapping by editing `to_0601()` and its coverage entry
together; the `Staging` helper makes emitting an item without reporting it
impossible. Verify any tag against libmisbklv's `uas0601.toml` first.
