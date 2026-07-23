---
type: Concept
title: Parrot to MISB 0601 Field Mapping Analysis
description: Analysis of which Parrot metadata fields map to ST 0601 tags, identifying implemented vs. missing tags in libmisbklv
tags: [mapping, parrot, misb, 0601, analysis]
timestamp: 2026-07-23T19:30:00Z
---

# Parrot to MISB 0601 Field Mapping Analysis

Analysis of Parrot drone telemetry fields and their mapping to MISB ST 0601.19
UAS Datalink Local Set tags, identifying what's already implemented in libmisbklv
vs. what needs to be added.

**Sources:**
- [[parrot-metadata-formats]] — Parrot field definitions
- libmisbklv `registry/uas0601.toml` — Currently implemented ST 0601 tags
- libmisbklv `/references/ST0601.19.pdf` — Full ST 0601.19 specification

## Currently Implemented in libmisbklv

Based on `registry/uas0601.toml`, libmisbklv implements **27 ST 0601 tags**:

| Tag | Name | Units | Kind | Notes |
|-----|------|-------|------|-------|
| 1 | Checksum | - | uint16 | Mandatory, must be last |
| 2 | Precision Time Stamp | µs | uint64 | Mandatory, µs since epoch |
| 5 | Platform Heading Angle | deg | linear_lds | 0..360 |
| 6 | Platform Pitch Angle | deg | linear_lds | -20..20 |
| 7 | Platform Roll Angle | deg | linear_lds | -50..50 |
| 11 | Image Source Sensor | - | utf8 | Camera/sensor name |
| 12 | Image Coordinate System | - | utf8 | E.g., "Geodetic WGS84" |
| 13 | Sensor Latitude | deg | linear_lds | -90..90 |
| 14 | Sensor Longitude | deg | linear_lds | -180..180 |
| 15 | Sensor True Altitude | m | linear_lds | -900..19000 (MSL) |
| 16 | Sensor Horizontal FOV | deg | linear_lds | 0..180 |
| 17 | Sensor Vertical FOV | deg | linear_lds | 0..180 |
| 18 | Sensor Relative Azimuth Angle | deg | linear_lds | 0..360 |
| 19 | Sensor Relative Elevation Angle | deg | linear_lds | -180..180 |
| 20 | Sensor Relative Roll Angle | deg | linear_lds | 0..360 |
| 21 | Slant Range | m | linear_lds | 0..5000000 |
| 22 | Target Width | m | linear_lds | 0..10000 |
| 23 | Frame Center Latitude | deg | linear_lds | -90..90 |
| 24 | Frame Center Longitude | deg | linear_lds | -180..180 |
| 25 | Frame Center Elevation | m | linear_lds | -900..19000 (HAE) |
| 40 | Target Location Latitude | deg | linear_lds | -90..90 |
| 41 | Target Location Longitude | deg | linear_lds | -180..180 |
| 42 | Target Location Elevation | m | linear_lds | -900..19000 (HAE) |
| 56 | Platform Ground Speed | m/s | uint8 | Direct value |
| 57 | Ground Range | m | linear_lds | 0..5000000 |
| 65 | UAS Datalink LS Version Number | - | uint8 | Mandatory, version |
| 74 | VMTI Local Set | - | nested_ls | ST 0903 nesting |

## Parrot → MISB 0601 Mapping (Direct)

Fields available in Parrot v3/proto formats that map directly to existing
libmisbklv tags:

### Platform/Drone
| Parrot Field | MISB Tag | Notes |
|--------------|----------|-------|
| `drone_quat` (yaw component) | 5 (Platform Heading) | Extract Euler yaw from quaternion |
| `drone_pitch` | 6 (Platform Pitch) | Convert radians → degrees |
| `drone_roll` | 7 (Platform Roll) | Convert radians → degrees |
| `location.latitude` | 13 (Sensor Lat) | Use drone location as sensor location |
| `location.longitude` | 14 (Sensor Lon) | Use drone location as sensor location |
| `location.altitude` | 15 (Sensor True Alt) | EGM96 → MSL (minimal difference) |
| `speed` (ground speed magnitude) | 56 (Platform Ground Speed) | Calculate from NED vector |

### Camera/Frame
| Parrot Field | MISB Tag | Notes |
|--------------|----------|-------|
| `picture_hfov` | 16 (Sensor H FOV) | Already in degrees |
| `picture_vfov` | 17 (Sensor V FOV) | Already in degrees |
| `camera_pan` | 18 (Sensor Rel Azimuth) | Convert radians → degrees |
| `camera_tilt` | 19 (Sensor Rel Elevation) | Convert radians → degrees |

### Timestamp
| Parrot Field | MISB Tag | Notes |
|--------------|----------|-------|
| `frame_timestamp` or `frame_utc_timestamp` | 2 (Precision Time Stamp) | Convert to µs since 1970-01-01 epoch |

### Metadata
| Parrot Field | MISB Tag | Notes |
|--------------|----------|-------|
| `"EO"` or `"Thermal"` | 11 (Image Source Sensor) | Map camera spectrum |
| `"Geodetic WGS84"` | 12 (Image Coordinate System) | Static value |
| `19` (ST 0601.19) | 65 (UAS LS Version) | Use latest version |

## Missing ST 0601 Tags (Needed for Parrot Data)

High-priority tags that Parrot provides data for, but libmisbklv doesn't yet
implement:

### Platform Speed/Velocity
| Tag | Name | Units | Kind | Parrot Source | Priority |
|-----|------|-------|------|---------------|----------|
| **3** | **Platform Designation** | - | utf8 | `model` + `serial` | **Medium** |
| **8** | **Platform True Airspeed** | m/s | uint8 | `air_speed` (proto only) | Medium |
| **9** | **Platform Indicated Airspeed** | m/s | uint8 | - | Low (not available) |
| **10** | **Platform Track Angle** | deg | uint16 | Calculated from `speed_ned` | Medium |

### Altitude/Elevation
| Tag | Name | Units | Kind | Parrot Source | Priority |
|-----|------|-------|------|---------------|----------|
| **26** | **Offset Corner Lat Point 1** | deg | int32 | - | Low (can calculate) |
| **27** | **Offset Corner Lon Point 1** | deg | int32 | - | Low (can calculate) |
| **28** | **Offset Corner Lat Point 2** | deg | int32 | - | Low |
| **29** | **Offset Corner Lon Point 2** | deg | int32 | - | Low |
| **30** | **Offset Corner Lat Point 3** | deg | int32 | - | Low |
| **31** | **Offset Corner Lon Point 3** | deg | int32 | - | Low |
| **32** | **Offset Corner Lat Point 4** | deg | int32 | - | Low |
| **33** | **Offset Corner Lon Point 4** | deg | int32 | - | Low |
| **82** | **Corner Lat Point 1 (Full)** | deg | int32 | - | Low |
| **83** | **Corner Lon Point 1 (Full)** | deg | int32 | - | Low |
| **84** | **Corner Lat Point 2 (Full)** | deg | int32 | - | Low |
| **85** | **Corner Lon Point 2 (Full)** | deg | int32 | - | Low |
| **86** | **Corner Lat Point 3 (Full)** | deg | int32 | - | Low |
| **87** | **Corner Lon Point 3 (Full)** | deg | int32 | - | Low |
| **88** | **Corner Lat Point 4 (Full)** | deg | int32 | - | Low |
| **89** | **Corner Lon Point 4 (Full)** | deg | int32 | - | Low |

### Platform Position/Altitude (Extended)
| Tag | Name | Units | Kind | Parrot Source | Priority |
|-----|------|-------|------|---------------|----------|
| **112** | **Platform Course Angle** | deg | IMAPB | Calculated from `speed_ned` | **High** |
| **113** | **Platform AGL Altitude** | m | IMAPB | `ground_distance` | **High** |
| **114** | **Platform Altitude Extended** | m | IMAPB | `altitude` (relative to takeoff) | **High** |
| **115** | **Platform Ellipsoid Height** | m | IMAPB | `location.altitude` (WGS84) | Medium |
| **116** | **Platform Ellipsoid Height Extended** | m | IMAPB | Same as 115, extended range | Medium |

### Platform Rates (Angular Velocities)
| Tag | Name | Units | Kind | Parrot Source | Priority |
|-----|------|-------|------|---------------|----------|
| **117** | **Platform Pitch Rate** | deg/s | IMAPB | Not directly available | Low |
| **118** | **Platform Roll Rate** | deg/s | IMAPB | Not directly available | Low |
| **119** | **Platform Yaw Rate** | deg/s | IMAPB | Not directly available | Low |

### Platform Speed (Extended)
| Tag | Name | Units | Kind | Parrot Source | Priority |
|-----|------|-------|------|---------------|----------|
| **103** | **Platform Groundspeed Extended** | m/s | IMAPB | `speed` magnitude | **High** |
| **104** | **Platform True Airspeed Extended** | m/s | IMAPB | `air_speed` (proto) | Medium |
| **105** | **Platform Indicated Airspeed Extended** | m/s | IMAPB | - | Low |

### Location/Position (Extended)
| Tag | Name | Units | Kind | Parrot Source | Priority |
|-----|------|-------|------|---------------|----------|
| **106** | **Sensor Latitude Extended** | deg | IMAPB | `location.latitude` | **High** |
| **107** | **Sensor Longitude Extended** | deg | IMAPB | `location.longitude` | **High** |
| **108** | **Sensor True Altitude Extended** | m | IMAPB | `location.altitude` | **High** |
| **109** | **Sensor Ellipsoid Height** | m | IMAPB | `location.altitude` | Medium |
| **110** | **Sensor Ellipsoid Height Extended** | m | IMAPB | Same as 109, extended | Medium |

### Sensor/Camera Extended
| Tag | Name | Units | Kind | Parrot Source | Priority |
|-----|------|-------|------|---------------|----------|
| **90** | **Sensor Relative Roll Angle Extended** | deg | IMAPB | - | Low |
| **91** | **Image Horizon Pixel Pack** | pack | VLP | - | Low |
| **92** | **Sensor North Velocity** | m/s | IMAPB | `speed_ned.north` | **High** |
| **93** | **Sensor East Velocity** | m/s | IMAPB | `speed_ned.east` | **High** |
| **94** | **Image Horizon Pixel Pack (Ext)** | pack | VLP | - | Low |

### Frame/Image Extended
| Tag | Name | Units | Kind | Parrot Source | Priority |
|-----|------|-------|------|---------------|----------|
| **96** | **Target Width Extended** | m | IMAPB | - | Low |
| **97** | **Sensor Calibrated FOV** | deg | uint16 | `picture_hfov` / `picture_vfov` | Medium |

### System/Link
| Tag | Name | Units | Kind | Parrot Source | Priority |
|-----|------|-------|------|---------------|----------|
| **53** | **Link RSSI** | dBm | int8 | `wifi_rssi` | Medium |
| **54** | **Battery Level** | % | uint8 | `battery_percentage` | **High** |
| **55** | **Battery Voltage** | V | uint16 | Not available | Low |

### Exposure/Camera Settings
| Tag | Name | Units | Kind | Parrot Source | Priority |
|-----|------|-------|------|---------------|----------|
| **134** | **Exposure Time** | µs | uint32 | `exposure_time` (convert ms → µs) | Medium |
| **135** | **ISO Sensitivity** | - | uint16 | `gain` or `iso_speed` | Medium |

## Coordinate System Transformations

### Altitude Datums
- **Parrot**: Uses EGM96 geoid (AMSL — Above Mean Sea Level)
- **MISB 0601**:
  - Tag 15 (Sensor True Altitude): MSL (essentially same as EGM96)
  - Tags 109/110 (Ellipsoid Height): WGS84 ellipsoid (HAE — Height Above Ellipsoid)
  - Tag 113 (AGL): Above Ground Level

**Conversion needed:** EGM96 (AMSL) → WGS84 (HAE) requires geoid model lookup.
For most locations, difference is <100m. Can use approximation or geoid library.

### Coordinate Frames
- **Parrot**: NED (North-East-Down) for velocities
- **MISB 0601**: Also NED for platform velocities (tags 92/93 are N/E components)

No conversion needed for velocities, direct mapping.

### Angular Units
- **Parrot**: Radians for most angles
- **MISB 0601**: Degrees for all angles

**Conversion:** Multiply by (180/π) ≈ 57.2958.

### Quaternion → Euler
Parrot provides frame quaternions; MISB wants Euler angles for platform attitude.

**Conversion:** Standard quaternion-to-Euler formula:
- Yaw (ψ) = atan2(2(qw*qz + qx*qy), 1 - 2(qy² + qz²))
- Pitch (θ) = asin(2(qw*qy - qz*qx))
- Roll (φ) = atan2(2(qw*qx + qy*qz), 1 - 2(qx² + qy²))

## Recommended Implementation Priority

### Phase 1 — Minimum Viable (Use Existing Tags)
Implement conversion using **only currently implemented libmisbklv tags**:

**Required tags (all implemented):**
- Tag 2: Precision Time Stamp
- Tags 5-7: Platform attitude (heading, pitch, roll)
- Tags 13-15: Sensor location + altitude
- Tags 16-17: Sensor FOV
- Tags 18-19: Sensor pointing (azimuth, elevation)
- Tag 56: Platform ground speed
- Tag 65: UAS LS Version

**Can produce basic working KLV stream with telemetry overlay capability.**

### Phase 2 — Extended Position/Velocity (Add IMAPB Tags)
Add extended IMAPB tags for better precision and range:

**High priority additions:**
- Tag 103: Platform Groundspeed Extended (IMAPB)
- Tags 106-108: Sensor Lat/Lon/Alt Extended (IMAPB)
- Tag 112: Platform Course Angle (IMAPB)
- Tag 113: Platform AGL Altitude (IMAPB)
- Tags 92-93: Sensor North/East Velocity (IMAPB)

**Requires:** libmisbklv IMAPB codec (ST 1201) — **already implemented** per
`st1201.md` and PROGRESS.md. Just needs registry entries added to `uas0601.toml`.

### Phase 3 — System/Camera Metadata
Add system state and camera settings:

- Tag 3: Platform Designation (drone model + serial)
- Tag 54: Battery Level
- Tag 53: Link RSSI
- Tags 134-135: Exposure Time, ISO Sensitivity

### Phase 4 — Corner Points & Advanced
Low priority, can calculate from frame center + FOV + slant range:

- Tags 82-89: Full corner points
- Tags 26-33: Offset corner points

## libmisbklv Extension Requirements

### Tags to Add to `uas0601.toml`

For **Phase 2** (high priority), add these IMAPB tags:

```toml
[[item]]
tag = 92
name = "Sensor North Velocity"
units = "m/s"
kind = "imapb"
# IMAPB: variable length, dynamic range

[[item]]
tag = 93
name = "Sensor East Velocity"
units = "m/s"
kind = "imapb"

[[item]]
tag = 103
name = "Platform Groundspeed Extended"
units = "m/s"
kind = "imapb"

[[item]]
tag = 106
name = "Sensor Latitude Extended"
units = "deg"
kind = "imapb"

[[item]]
tag = 107
name = "Sensor Longitude Extended"
units = "deg"
kind = "imapb"

[[item]]
tag = 108
name = "Sensor True Altitude Extended"
units = "m"
kind = "imapb"

[[item]]
tag = 112
name = "Platform Course Angle"
units = "deg"
kind = "imapb"

[[item]]
tag = 113
name = "Platform AGL Altitude"
units = "m"
kind = "imapb"

[[item]]
tag = 114
name = "Platform Altitude Extended"
units = "m"
kind = "imapb"
```

For **Phase 3**, add:

```toml
[[item]]
tag = 3
name = "Platform Designation"
kind = "utf8"

[[item]]
tag = 53
name = "Link RSSI"
units = "dBm"
kind = "int"
length = 1

[[item]]
tag = 54
name = "Battery Level"
units = "%"
kind = "uint"
length = 1

[[item]]
tag = 134
name = "Exposure Time"
units = "µs"
kind = "uint"
length = 4

[[item]]
tag = 135
name = "ISO Sensitivity"
kind = "uint"
length = 2
```

## Field Mapping Table (Complete)

Comprehensive Parrot → MISB 0601 mapping for reference:

| Parrot Field | Type | MISB Tag | Phase | Notes |
|--------------|------|----------|-------|-------|
| `frame_timestamp` / `frame_utc_timestamp` | uint64 µs | 2 | 1 | Convert to epoch µs |
| `model` + `serial` | string | 3 | 3 | Concatenate, e.g. "ANAFI-PI040416..." |
| `drone_quat` (→ yaw) | float rad | 5 | 1 | Quat→Euler, rad→deg |
| `drone_pitch` | float rad | 6 | 1 | rad→deg |
| `drone_roll` | float rad | 7 | 1 | rad→deg |
| `air_speed` | float m/s | 8, 104 | 3 | Direct (proto only) |
| `speed_ned` (→ course) | NED | 10, 112 | 2 | atan2(E, N), rad→deg |
| `camera_spectrum` | enum | 11 | 1 | Map to "EO"/"Thermal" |
| - | - | 12 | 1 | Static "Geodetic WGS84" |
| `location.latitude` | double deg | 13, 106 | 1, 2 | Direct (drone = sensor) |
| `location.longitude` | double deg | 14, 107 | 1, 2 | Direct |
| `location.altitude` | double m | 15, 108 | 1, 2 | EGM96 ≈ MSL, convert to HAE for 108 |
| `picture_hfov` | float deg | 16 | 1 | Direct |
| `picture_vfov` | float deg | 17 | 1 | Direct |
| `camera_pan` | float rad | 18 | 1 | rad→deg |
| `camera_tilt` | float rad | 19 | 1 | rad→deg |
| - | - | 20 | - | Camera roll (not in Parrot) |
| - | - | 21 | - | Slant range (need calculation) |
| - | - | 22 | - | Target width (need calculation) |
| - | - | 23-25 | - | Frame center (need calculation) |
| - | - | 40-42 | - | Target location (tracking data) |
| `wifi_rssi` | int8 dBm | 53 | 3 | Direct |
| `battery_percentage` | uint8 % | 54 | 3 | Direct |
| `speed` (magnitude) | float m/s | 56, 103 | 1, 2 | Calculate from NED |
| - | - | 57 | - | Ground range (need calculation) |
| - | - | 65 | 1 | Static 19 (ST 0601.19) |
| - | - | 74 | - | VMTI (future: tracking data) |
| `speed_ned.north` | float m/s | 92 | 2 | Direct |
| `speed_ned.east` | float m/s | 93 | 2 | Direct |
| `ground_distance` | double m | 113 | 2 | AGL altitude |
| `altitude` (takeoff-relative) | double m | 114 | 2 | Altitude above takeoff |
| `exposure_time` | float ms | 134 | 3 | Convert ms→µs |
| `gain` / `iso_speed` | uint16/32 | 135 | 3 | Direct |

**Why:** Parrot provides rich telemetry that maps cleanly to MISB 0601, but needs
extended IMAPB tags for full fidelity. Phase 1 with existing tags is viable;
Phase 2 adds precision; Phase 3 adds system metadata.

**How to apply:** Use this as the field mapping specification when implementing
the Parrot → MISB 0601 converter. Extend libmisbklv registry incrementally as
needed (Phases 2-3).
