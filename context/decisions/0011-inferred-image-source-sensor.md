---
type: Decision
title: ADR 0011 — Inferring Item 11 for formats that state no spectrum
description: Name the sensor EO or IR from thermal-metadata presence, behind an opt-in flag and labeled as inferred, rather than leaving Item 11 permanently empty for v3 recordings
status: accepted
fork: 11
tags: [decision, mapping, misb, 0601, parrot, v3]
timestamp: 2026-07-28T00:00:00Z
---

# Context

ST 0601 Item 11, Image Source Sensor, is free-form utf8 naming the sensor that
produced the imagery — `EO`, `IR`, `EO Wide`. [[field-mapping-analysis]] maps it
from the proto format's `camera.spectrum` (the stem) and `camera.subtype` (the
qualifier).

**The v3 binary format carries neither.** Its base structure has no spectrum
field and no extension supplies one, so every v3 recording — the whole ANAFI
range — reports Item 11 as `format does not carry it`. That is accurate, and it
is also useless to a consumer that needs to know whether it is looking at
visible or thermal imagery. The airframes in scope include ones with both.

There is one in-band trace of which sensor was imaging: the **thermal extension
(0x4534)**, which carries min/max/probe temperature spots. Those spots have no
0601 item and are dropped as ADR 0002 residue, but the extension's *presence*
correlates with the imagery being thermal, because that is when Parrot writes
it.

Correlates, not equals. Reading presence as `IR` and absence as `EO` is an
inference from a proxy, and it has two known failure modes:

- **Blended imagery.** ANAFI Thermal and USA can record a fused EO/IR image,
  which writes the thermal extension like a pure thermal recording does. The
  inference would name it `IR`. The proto format distinguishes this case with a
  `Blended` spectrum; v3 offers nothing to distinguish it with.
- **Absence is the weaker half.** "No thermal extension" says only that no
  thermal metadata was written. A thermal recording that omitted it — a
  firmware variant, a mode we have not seen — would be named `EO`, which is the
  more damaging direction: a consumer told `EO` about thermal imagery has been
  actively misled, where a consumer told nothing knows it was told nothing.

So the choice is between a permanently empty item and a mostly-right guess.

# Decision

**Emit an inferred Item 11 only when explicitly asked for, and label it.**

`--infer-image-source` (`TransformOptions::infer_image_source`) names the sensor
`IR` when the frame carried thermal metadata and `EO` when it did not, for any
recording whose format states no spectrum. Three constraints make it tolerable:

- **Off by default.** The default output remains what the recording supports and
  nothing more — the same posture ADR 0002 takes toward unmappable fields and
  ADR 0005 takes toward out-of-range attitude.
- **A stated spectrum always wins.** The flag fills a gap; it never overrides.
  Turning it on for a proto recording changes nothing, because proto states the
  spectrum.
- **The coverage report says `(inferred)`.** Rendered exactly as `(clamped)` is,
  for the same reason: a relaxation the report does not admit to would be
  indistinguishable from data.

**The subtype is not inferred.** No v3 field distinguishes wide from tele, so
the value is a bare stem — `EO`, never `EO Wide`. A guessed qualifier would add
no information while making the guess look more precise than it is.

The KLV value itself stays the bare sensor name. `(inferred)` is a report label,
not a suffix on the wire: a 0601 consumer must not have to parse a caveat out of
a field whose whole content is a name.

# Alternatives considered

## Leave Item 11 empty for v3, permanently

The status quo, and the strictly honest position.

**Rejected.** It is honest at the cost of being unhelpful to the one question
Item 11 exists to answer, for the majority of recordings in scope. The
information *is* in the file, weakly. Refusing to surface it under any
circumstance treats "cannot be certain" as "must not say", which is not the line
this project draws elsewhere — `--clamp-attitude` already ships a value that is
knowingly not the true one, behind a flag, labeled.

## Emit the inference by default

**Rejected.** It would make every v3 conversion assert a sensor identity the
recording never stated, silently, including to consumers who never read this
ADR. The failure direction is bad — mislabeling thermal footage `EO` — and the
default must not be the one that can mislead without being asked.

## Infer from the airframe model instead

Session metadata names the model, and a model implies its camera complement.

**Rejected.** The model says which sensors *exist*, not which one was recording.
An airframe with three cameras tells you nothing about the active one. It is a
worse proxy than the thermal extension, not a better one.

## Infer from the field of view

The thermal sensor's FOV differs from the EO sensors'.

**Rejected as scope, not as principle.** It needs a per-model FOV table
maintained against every firmware, and it fails on the zoom range where EO FOVs
sweep continuously. The thermal extension is a signal already parsed; this would
be a new subsystem of guesses.

## Name blended imagery something hedged, e.g. `IR (or blended)`

**Rejected.** Item 11 is a sensor name that consumers match and display. A
parenthetical inside it is not a name, and a consumer doing a string comparison
against `IR` would simply fail to match. The hedge belongs in the coverage
report, which is where it now is.

# Consequences

- v3 recordings can carry Item 11 for consumers that need it, at the cost of a
  flag those consumers must know to pass — which is the point: passing it is the
  acknowledgment.
- Blended EO/IR v3 recordings will be named `IR` under the flag. Documented
  here, in the CLI help, and in the user guide; there is no v3 signal that would
  let the tool do better.
- `TelemetrySnapshot` gained `thermal_data_present`, a field that is not
  telemetry and is read by exactly one thing. It stays on the snapshot rather
  than in the v3 parser's output because the transform is where the inference
  belongs and ADR 0004 puts the format boundary at normalization.
- `to_0601` and `mux_to_ts` now take a `TransformOptions` struct instead of a
  trailing bool. Two adjacent unnamed bools at a call site is a defect waiting
  to happen, and the next relaxation would have made it three.

# Assumptions / open questions

- Assumes Parrot writes the thermal extension whenever thermal imagery is being
  recorded. **Unverified against a real thermal recording** — no v3 thermal
  footage has been through this tool. The conformance reports in
  `test/conformance/` come from EO flights. Until a thermal recording confirms
  it, the `IR` half of this inference is reasoning about the format, not
  evidence from it.
- If a real blended recording turns out to be distinguishable after all — a
  different extension, a different spot layout — this ADR should be revised
  rather than the flag quietly re-pointed.

# Citations

- [[field-mapping-analysis]] § Tier 1, Item 11 — the mapping this fills a gap in
- [[parrot-metadata-formats]] § Extension Mechanism — 0x4534, and the four
  standard extensions
- [[transform-stage]] § The coverage report — where `(inferred)` is rendered
- [ADR 0002](./0002-unmappable-parrot-fields.md) — the thermal spots themselves
  as unmappable residue
- [ADR 0005](./0005-attitude-range-policy.md) — the precedent for a labeled,
  opt-in relaxation
