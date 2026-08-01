---
type: Concept
title: Parrot to MISB 0601 Field Mapping Analysis
description: Which Parrot telemetry fields map to which ST 0601.19 items, what must be derived geometrically, and what has no home in 0601
tags: [mapping, parrot, misb, 0601, analysis]
generated:
  by: claude/opus-5
  at: 2026-07-31T16:36:53Z
sources:
  - id: parrot-vmeta-html
    resource: "../references/Embedded Video Metadata - 8.4.2.html"
    title: Parrot's vmeta.proto schema and v1/v2/v3 binary structs, verbatim
  - id: libmisbklv-uas0601
    resource: https://github.com/nitsuga/libmisbklv/blob/1732e0c986e4b252a030147c389585da8248ddb2/registry/uas0601.toml
    title: libmisbklv — registered ST 0601.19 item set
  - id: libmisbklv-st0601-txt
    resource: https://github.com/nitsuga/libmisbklv/blob/1732e0c986e4b252a030147c389585da8248ddb2/references/ST0601.19.txt
    title: libmisbklv — MISB ST 0601.19 (per-item format blocks, Tables 3/10/11/12)
---

# Parrot to MISB 0601 Field Mapping Analysis

Maps Parrot drone telemetry ([[parrot-metadata-formats]]) onto MISB ST 0601.19
UAS Datalink Local Set items.

**Sources:**
- [[parrot-metadata-formats]] — Parrot field definitions
- `references/Embedded Video Metadata - 8.4.2.html` — Parrot's `vmeta.proto` and
  the v1/v2/v3 binary structs, verbatim
- libmisbklv `registry/uas0601.toml` — the registered ST 0601.19 item set
- libmisbklv `references/ST0601.19.txt` — the standard itself (per-item format
  blocks, Tables 3/10/11/12)

## Registry status: no libmisbklv work required

libmisbklv registers the **full ST 0601.19 item set** (§8.1–8.143, less
deprecated Item 66), validated against the standard's worked examples. Every
item named below already exists with the correct codec, range, and special
values. Nothing needs adding to `uas0601.toml` for this project.

> **Correction, 2026-07-25.** The previous revision of this doc listed a large
> set of "missing" tags to add, with tag numbers taken from assumption rather
> than the standard. Nearly all were wrong (3, 53, 54, 92, 93, 103, 106, 107,
> 108, 114, 134, 135 are all different items than claimed, and no "Sensor
> Lat/Lon/Alt Extended" or "Platform Groundspeed Extended" item exists in 0601 at
> all). It also premised a precision problem that does not exist: Item 13/14 are
> int32 mapped over ±90/±180, i.e. ~4.7 mm resolution. **Trust `uas0601.toml`
> over any tag number written from memory.**

## Tier 1 — direct mappings

Field-to-field, needing only unit/datum conversion. This covers essentially all
of Parrot's usable telemetry.

`proto` = `vmeta.proto` `TimedMetadata` (ANAFI Ai / UKR). `v2/v3` = the binary
base struct (Bebop 2 / Disco / ANAFI). *Session* = untimed MP4 session metadata.

| Item | Name | proto source | v2/v3 source | Conversion |
|------|------|--------------|--------------|------------|
| 2 | Precision Time Stamp | `camera.utc_timestamp` | ext `0x4531` timestamp | proto is already µs since epoch — **but `camera.utc_timestamp_accuracy == 0` means invalid**. v2/v3 carry a monotonic clock only; anchor it to session `media_date`. |
| 4 | Platform Tail Number | session `serial_number` | session `serial_number` | utf8 |
| 5 | Platform Heading Angle | `drone.quat` → yaw | `drone_w..z` (Q2.14) → yaw | quat→Euler, rad→deg, wrap to [0, 360) |
| 6 | Platform Pitch Angle | `drone.quat` → pitch | ditto | rad→deg. **Range is only ±20°** — see *Range traps* |
| 7 | Platform Roll Angle | `drone.quat` → roll | ditto | rad→deg. **Range is only ±50°** |
| 8 | Platform True Airspeed | — | `air_speed` (Q8.8) | round to uint8 m/s; **negative means no data** — omit the item |
| 10 | Platform Designation | session `model` | session `model` | utf8, e.g. `"ANAFI Ai"` |
| 11 | Image Source Sensor | `camera.spectrum` + `camera.subtype` | thermal ext `0x4534` presence, opt-in only | free-form utf8, not an enum: spectrum gives the stem (`EO` / `IR` / `EO/IR Blended`), subtype qualifies it (`EO Wide`, `IR Tele`). v2/v3 state no spectrum; `--infer-image-source` guesses the stem from whether thermal metadata was written, labeled `(inferred)` — [ADR 0011](./decisions/0011-inferred-image-source-sensor.md) |
| 12 | Image Coordinate System | — | — | constant `"Geodetic WGS84"` |
| 13 | Sensor Latitude | `camera.location.latitude` | `latitude` (Q10.22) | direct. Prefer `camera.location` over `drone.location`; fall back when absent |
| 14 | Sensor Longitude | `camera.location.longitude` | `longitude` (Q10.22) | direct |
| 15 | Sensor True Altitude | `camera.location.altitude_egm96amsl` | `altitude_and_sv` bits 31..8 (Q16.8) | both sides are EGM96/AMSL — direct. **proto: zero means unknown** |
| 16 | Sensor Horizontal FOV | `camera.hfov` | `picture_hfov` (Q8.8) | **proto is radians, v2/v3 is degrees** — do not share a code path |
| 17 | Sensor Vertical FOV | `camera.vfov` | `picture_vfov` (Q8.8) | same asymmetry |
| 18 | Sensor Relative Azimuth | `camera.quat` ⊖ `drone.quat` → yaw | `frame_*` ⊖ `drone_*` | relative quaternion → Euler, rad→deg, wrap to [0, 360) |
| 19 | Sensor Relative Elevation | ditto → pitch | ditto | rad→deg, ±180 |
| 20 | Sensor Relative Roll | ditto → roll | ditto | rad→deg, wrap to [0, 360) |
| 40/41/42 | Target Location Lat/Lon/Elev | `lfic[].location` | automation ext `0x4533` `framing_target_*` (v3 only) | proto: LFIC *is* cursor-on-target — prefer `LFIC_TYPE_COT` when several are present. For v3 use the automation extension's framing target; v2 has no equivalent. **Unresolved:** real v3 recordings carry an undocumented `0x4536` extension in every sample that may be a binary LFIC — if it is, it is the better source here. See [[parrot-metadata-formats]] § Extension Mechanism |
| 45/46 | Target Error Estimate CE90/LE90 | `lfic[].horizontal_radial_accuracy` / `horizontal_tangential_accuracy` | — | **proto only.** Approximate even there: Parrot's radial/tangential horizontal accuracies are not literally CE90/LE90. **Open question** |
| 51 | Platform Vertical Speed | `-drone.speed.down` | `-down_speed` (Q8.8) | **negate.** 0601 §8.51 is "relative to zenith" (positive up); Parrot NED is positive down |
| 56 | Platform Ground Speed | `hypot(speed.north, speed.east)` | `hypot(north_speed, east_speed)` | round to uint8 m/s; omit above 255 rather than saturating |
| 65 | UAS Datalink LS Version | — | — | constant `19` |
| 75 | Sensor Ellipsoid Height | `camera.location.altitude_wgs84ellipsoid` | — | **proto gives both datums directly — no geoid model needed.** v2/v3 carry AMSL only, so this item needs an EGM96 lookup or omission |
| 79 | Sensor North Velocity | `drone.speed.north` | `north_speed` (Q8.8) | direct, ±327 m/s |
| 80 | Sensor East Velocity | `drone.speed.east` | `east_speed` (Q8.8) | direct, ±327 m/s |
| 90 | Platform Pitch Angle (Full) | `drone.quat` → pitch | ditto | **the item to actually trust** — ±90° |
| 91 | Platform Roll Angle (Full) | `drone.quat` → roll | ditto | ±90° |
| 112 | Platform Course Angle | `atan2(speed.east, speed.north)` | same | rad→deg, wrap [0, 360). **Undefined at zero ground speed** — omit below a threshold |
| 113 | Altitude AGL | `drone.ground_distance` | `ground_distance` (Q16.16) | direct |
| 123 | Number of NAVSATs in View | `*.location.sv_count` | `altitude_and_sv` bits 7..0 | direct uint8; omit an out-of-range proto count rather than saturating |
| 124 | Positioning Method Source | derived | derived | Table 10 bit-flags; set bit 1 (GPS) when `sv_count > 0`. Parrot does not say *which* constellation |
| 125 | Platform Status | `drone.flying_state` + `piloting_mode` | `state` / `mode` | enum map — below |
| 126 | Sensor Control Mode | `drone.piloting_mode`, `tracking.state` | `mode` | enum map — below |
| 134 | Zoom Percentage | `camera.zoom_level` | — | proto only. Parrot's `zoom_level` is a multiplier, 0601 wants IMAPB 0..100 % — needs a defined mapping |

### Enum maps

Named below with the **protobuf** enumerators. The binary formats use the same
concepts with *different numeric values* — binary `piloting_mode` starts at
`MANUAL=0` where protobuf starts at `PM_UNKNOWN=0`, and the two disagree on the
last pair. Map through the names, never the numbers; see
[[parrot-metadata-formats]] § v2/v3.

**Item 125 Platform Status** (ST 0601.19 Table 11), from `FlyingState` × `PilotingMode`:

| Parrot | 0601 value |
|---|---|
| `FS_LANDED` | 12 Landed-Parked |
| `FS_MOTOR_RAMPING` | 3 Run-up |
| `FS_TAKINGOFF`, `FS_USER_TAKEOFF` | 4 Take-off |
| `FS_HOVERING`/`FS_FLYING` + `PM_MANUAL` | 6 Manual operation |
| `FS_HOVERING`/`FS_FLYING` + `PM_TRACKING` | 7 Automated-orbit |
| `FS_HOVERING`/`FS_FLYING` + `PM_FLIGHT_PLAN`/`PM_MOVETO` | 8 Transitioning |
| `FS_HOVERING`/`FS_FLYING` + `PM_RETURN_HOME` | 9 Egress |
| `FS_LANDING`, `FS_EMERGENCY_LANDING` | 10 Landing |
| `FS_EMERGENCY` | 0 Active — Table 11 has no emergency value |

**Item 126 Sensor Control Mode** (Table 12): `PM_TRACKING` with tracking active
→ 6 Auto-Tracking; `PM_MANUAL` → 3 Manual Control; `FS_LANDED` → 1 Home
Position; otherwise → 2 Uncontrolled.

## Tier 2 — derived geometry

Not present in Parrot telemetry in any form; each requires projecting the gimbal
boresight from the sensor position onto a terrain model.

| Item | Name |
|------|------|
| 21 | Slant Range |
| 22 | Target Width |
| 23 / 24 / 25 | Frame Center Latitude / Longitude / Elevation |
| 57 | Ground Range |
| 78 | Frame Center Height Above Ellipsoid |
| 82–89 | Corner Lat/Lon Points 1–4 (Full) |
| 26–33 | Offset Corner Points 1–4 (compact alternative to 82–89) |

Inputs are all in Tier 1: sensor position (13/14/15), platform attitude (5/90/91),
gimbal orientation (18/19/20), FOV (16/17), and AGL (113).

Two fidelity levels, and the choice is independent of the registry:

- **Flat-earth** — intersect the boresight with a horizontal plane at
  `sensor altitude − AGL`. No external data. Degrades on sloped terrain and
  becomes meaningless near the horizon.
- **DTED / terrain model** — real ray-cast. Accurate, but adds a data dependency
  and a lookup path.

This is the substantive remaining engineering in the transform stage.

## Tier 3 — VMTI (Item 74)

Proto `tracking.target` (a `BoundingBox` with class, confidence, and UID) and
`proposal.proposals` map onto the ST 0903 VMTI Local Set, which libmisbklv
registers as a nested LS (`child = "vmti_0903"`). Out of scope for the initial
converter; noted so the data isn't forgotten.

## No home in ST 0601

These Parrot fields have **no corresponding 0601 item**, and 0601 defines no
vendor/user extension point to carry them:

- `drone.battery_percentage`, `drone.battery_voltage`
- Link telemetry — `WifiLinkMetadata` (rssi, quality, goodput),
  `StarfishLinkMetadata` (per-link type/status/quality)
- `camera.exposure_time`, `camera.iso_gain`, `camera.awb_r_gain`,
  `camera.awb_b_gain`, `camera.principal_point`
- `ThermalMetadata` — calibration state, min/max/probe spots, mask
- `drone.altitude_ato` (altitude above take-off), `drone.position` (NED),
  `drone.local_position`, `camera.local_position`
- `automation` — animation, follow-me / look-at-me, destination
- `location.horizontal_accuracy` / `vertical_accuracy` (0601's 45/46 are
  *target* error, not sensor error)
- `lfic[].grid_precision`, `tracking.quality` / `cookie`
- `UserMetadata`, `PhotoMetadata`

Disposition is a live fork — see
[ADR 0002](./decisions/0002-unmappable-parrot-fields.md).

## Conversions

### Quaternion → Euler

Parrot quaternions are scalar-first (W, X, Y, Z); v2/v3 encode them Q2.14.

- Yaw (ψ) = `atan2(2(qw·qz + qx·qy), 1 − 2(qy² + qz²))`
- Pitch (θ) = `asin(2(qw·qy − qz·qx))`
- Roll (φ) = `atan2(2(qw·qx + qy·qz), 1 − 2(qx² + qy²))`

For the **relative** gimbal angles (Items 18/19/20), compose the frame quaternion
with the conjugate of the drone quaternion before converting.

### Units and datums

- Angles: Parrot radians → 0601 degrees (×180/π), except v2/v3 `picture_[hv]fov`
  which are already degrees.
- Altitude: proto supplies **both** `altitude_egm96amsl` (→ Item 15) and
  `altitude_wgs84ellipsoid` (→ Item 75), so no geoid model is needed. v2/v3
  supply AMSL only.
- Velocity: both sides are NED. Only the vertical component flips sign (Item 51).

### Range traps

- **Items 6/7 are narrow.** Platform Pitch is ±20°, Roll is ±50°. A quadcopter
  exceeds those in ordinary flight, and out-of-range must be signaled with the
  `0x8000` special value rather than clamped. Always emit **90/91** (±90°)
  alongside; treat 6/7 as the legacy-compatibility pair.
- **Item 56 is uint8 m/s.** 1 m/s quantization, 255 m/s ceiling, and 0601 has no
  extended variant. A speed above the ceiling is reported as not encodable,
  while Items 79/80 + 112 remain independently usable when their ranges allow.
- **Item 15 is uint16 mapped −900..19000 m** → ~0.3 m resolution. Item 75 has
  the same resolution.
- Items 79/80 have a ±327 m/s range, and Item 51 a ±180 m/s range. Values
  outside those ranges are omitted and reported as not encodable, not saturated.

## Open questions

- ~~Item 2 for v3: anchoring the monotonic frame clock to wall time.~~
  **Answered**: the session `media_date` is taken as the first sample's wall
  time and each frame adds its elapsed monotonic µs, so spacing stays exact and
  the absolute anchor inherits the field's second granularity. See
  [[transform-stage]] § Item 2.
- Item 134: define `zoom_level` (a multiplier) → zoom percentage.
- Items 45/46: whether Parrot's radial/tangential horizontal accuracies can
  honestly be reported as CE90/LE90.
- Tier 2 fidelity: flat-earth vs. DTED (see Tier 2).
- Item 48 Security Local Set (ST 0102) — not derivable from Parrot data, but
  many 0601 consumers expect it. Whether to synthesize a minimal one.

**Why:** Parrot's telemetry maps onto 0601 more completely than previously
believed, and entirely with items libmisbklv already registers. The real work is
geometric derivation, not registry extension.

**How to apply:** Use the Tier 1 table as the implementation spec for the
transform stage. Verify any tag number against `uas0601.toml` before writing code
against it.
