---
type: Decision
title: ADR 0002 — Disposition of Parrot fields with no ST 0601 item
description: Drop unmappable Parrot telemetry and report it, rather than smuggling it through a 0601 item that does not mean it
status: proposed
fork: 1
tags: [decision, mapping, misb, 0601, parrot]
timestamp: 2026-07-25T21:15:00Z
---

# Context

Now that libmisbklv registers the complete ST 0601.19 item set, the Parrot →
0601 mapping can be settled item by item (see [[field-mapping-analysis]]). Doing
that exhaustively surfaces a residue: Parrot telemetry with no corresponding
0601 item at all.

The residue is not marginal. It includes battery state
(`battery_percentage`, `battery_voltage`), the entire link telemetry group
(Wi-Fi RSSI / quality / goodput, Starfish per-link status), camera exposure
settings (`exposure_time`, `iso_gain`, AWB gains, principal point), all thermal
metadata (calibration state, min/max/probe spots, mask), take-off-relative and
local-frame positions, automation state, and location accuracy estimates.

ST 0601.19 has no vendor-extension or user-defined item. Items 138–143 are
specific things (Payload List, Active Payloads, Weapons Stores, Waypoint List,
View Domain, Metadata Substream ID Pack), not an escape hatch. The two opaque
nested-standard slots — Item 48 (ST 0102 Security LS) and Item 73 (ST 0806 RVT
LS) — carry other standards' schemas, and neither ST 0102 nor ST 0806 defines
battery, link, or exposure fields either.

So there is nowhere in a conformant 0601 packet for this data to go. The
question is what the converter does about that.

# Decision

**Drop the unmappable fields from the KLV output, and surface them in a
conversion coverage report** rather than encoding them into items that do not
mean them.

The coverage report is a per-conversion summary — which Parrot fields were
consumed, which were dropped, and (for the ones that map conditionally) why an
item was omitted for a given frame. It is a first-class output of the converter,
not a debug log: a user who wants battery telemetry needs to learn that from the
tool, not from noticing its absence downstream.

# Alternatives considered

## Repurpose a semantically adjacent item

E.g. battery percentage → Item 58 Platform Fuel Remaining; Wi-Fi RSSI → some
unused numeric item.

**Rejected.** The entire reason to convert to 0601 is that standard tooling
reads it. A stream where Item 58 means "battery %" decodes without error and
displays a wrong, confident number in every conformant viewer. Silent
mis-decoding is strictly worse than absence, and the damage lands on consumers
who never see our documentation.

## Carry them in Item 73 (ST 0806 RVT Local Set)

libmisbklv treats Item 73 as opaque bytes, so we *could* put anything there.

**Rejected.** Same failure mode one level down: a 0806-aware consumer would
parse our payload as RVT and get garbage. Opacity in our library is not
permission from the standard.

## Define a private Local Set in a second TS stream

Emit conformant 0601 on one PID and a private Parrot LS on another.

**Rejected for now, but not on principle** — this is the honest way to preserve
the data, and it breaks nothing for 0601-only consumers. It is rejected as
*scope*: it needs a private LS design, a UL registration or a documented local
key namespace, and muxer work, all before the primary conversion path works
end-to-end. Revisit once Phase 3 lands, if a concrete consumer wants the data.

## Drop silently

**Rejected.** Cheapest, but it makes lossiness invisible. Someone converting
thermal footage should not discover months later that every thermal spot was
discarded.

# Consequences

- The KLV output stays strictly conformant; no item carries a meaning 0601 does
  not assign it.
- Conversion is documented as lossy, with the loss enumerated rather than
  implied. The source MP4 remains the complete record — nothing is destroyed,
  only not carried forward.
- The converter needs a coverage-report mechanism, which is new surface area
  beyond the mapping tables themselves. It is small (a set of counters plus the
  drop list from [[field-mapping-analysis]]) but it is not free.
- Thermal-specific workflows get no useful output from this tool. If that turns
  out to matter, the private-LS alternative above is the path, and this ADR
  should be superseded rather than quietly bent.

# Assumptions / open questions

- Assumes the target consumers are standard 0601 tools. If the actual consumer
  is a single known system that would accept a private extension, the trade-off
  changes and the second-stream alternative becomes cheap to justify.
- Report format (human-readable summary vs. machine-readable JSON vs. both) is
  unsettled; it does not affect this decision.
- Item 48 (Security LS) is a separate question — it is *absent* rather than
  *unmappable*, since it is not derived from Parrot data at all. Tracked as an
  open question in [[field-mapping-analysis]].

# Citations

- [[field-mapping-analysis]] § "No home in ST 0601" — the enumerated residue
- [[parrot-metadata-formats]] — the Parrot side
- libmisbklv `registry/uas0601.toml` — the complete registered item set
- libmisbklv `references/ST0601.19.txt` §8.138–8.143 — the items that are not an
  extension point
