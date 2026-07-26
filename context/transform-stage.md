---
type: Component
title: Transform stage — Parrot telemetry to MISB ST 0601
description: How a decoded Parrot frame becomes an ST 0601 packet — the format-neutral snapshot, the Tier 1 mapping and its sign/range traps, and the conversion coverage report
tags: [transform, mapping, misb, 0601, phase-2]
timestamp: 2026-07-26T02:40:00Z
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
— degrees, m/s, metres — so proto's radian FOVs and v3's degree FOVs are the
same thing by the time anything maps them. It carries `CommonPilotingMode`,
which both parsers reach **by name**, because the binary and protobuf schemas
number the same concepts differently. And it uses `std::optional` for anything a
format may not supply, so the mapping's rule for a missing input is uniformly
"omit the item".

The transform never sees a `ParrotV3Frame` or a `ParrotProtoFrame`. A future v2
parser adds one normalizer and touches nothing else.

## What it emits

Tier 1 of the mapping — roughly 29 items from a v3 frame with session metadata — encoded through
libmisbklv's registry codec, assembled with the UL key, BER length and Item 1
checksum. The packet is a real 0601 packet: `transform_test` parses every one it
produces back through `misbklv::parse_packet` and decodes item by item, so
values are checked as a consumer sees them rather than as we staged them.

## Traps this code exists to get right

- **Item 51 is negated.** 0601 §8.51 measures vertical speed toward zenith;
  Parrot NED is positive down. The sign flip lives in exactly one place.
- **Items 6/7 are ±20° and ±50°**, which a quadcopter exceeds in ordinary
  flight. Out of range is signalled with the `0x8000` special value, never
  clamped — a clamped attitude is a confident lie — and Items 90/91 (±90°)
  carry the trustworthy value alongside.
- **Item 112 is undefined at rest.** Course over ground is `atan2` of two
  numbers that are pure noise when the aircraft is stationary, so it is omitted
  below 0.5 m/s rather than reporting a direction of travel that does not exist.
- **Zero is not a value in proto.** Proto3 gives scalars no presence bit, so
  Parrot signals "unknown" in band: a zero altitude means no fix, and
  `utc_timestamp_accuracy == 0` means the UTC stamp is invalid. Both become an
  empty optional at normalization; treating either as a number would put the
  aircraft at sea level or stamp the packet with a confident wrong time.
- **`asin` is clamped** before computing pitch. A v3 quaternion that is barely
  non-unit from fixed-point rounding can push the argument past 1 and yield NaN.

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

- **emitted** — every item that made it, with its value
- **omitted** — items the mapping knows about but did not emit, each with a
  reason: the format does not carry it, this frame did not, it needs terrain
  intersection, it needs session metadata, or the mapping itself is an open
  question
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
  accuracies are not literally CE90/LE90. Withheld rather than relabelled.
- **Item 11** (image source sensor, "EO"/"IR") needs the camera spectrum.
  Nothing supplies it: the session tags do not carry it, and proto's
  `camera.spectrum` is one of the fields the reader drops. Omitted rather than
  assumed to be EO.

Items 4 and 10 (tail number, platform designation) come from session metadata
when the recording has it, and are reported as `needs MP4 session metadata`
when it does not.

**Why:** the mapping's hard parts are not the arithmetic but the places where
0601 and Parrot disagree about sign, range, or what zero means — and the places
where the honest answer is to emit nothing.

**How to apply:** add a mapping by editing `to_0601()` and its coverage entry
together; the `Staging` helper makes emitting an item without reporting it
impossible. Verify any tag against libmisbklv's `uas0601.toml` first.
