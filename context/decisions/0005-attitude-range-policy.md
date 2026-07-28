---
type: Decision
title: ADR 0005 — Out-of-range policy for Items 6/7, and the two escape hatches
description: Keep the 0x8000 sentinel as the default for out-of-range platform pitch/roll, and expose clamping and out-of-spec emission as explicit opt-in flags for consumers that cannot parse it
status: accepted
fork: 5
tags: [decision, transform, mapping, misb, 0601, phase-2]
timestamp: 2026-07-27T21:03:00Z
---

# Context

ST 0601 gives Items 6 and 7 (Platform Pitch / Roll) ranges of ±20° and ±50°. A
quadcopter exceeds both in ordinary flight, so this is not an edge case — it is
the common case for the airframes this tool targets. The spec's answer is the
`0x8000` special value: "out of range", encoded rather than approximated.

[[transform-stage]] took that position and put a reason under it: a clamped
attitude is a confident lie. A consumer reading Item 6 as `20.0°` has no way to
know the aircraft was at 60°, whereas Items 90/91 (Sensor Pitch/Roll, ±90°)
carry the trustworthy value alongside and are always emitted accurately.

What changed is contact with a real consumer. A downstream KLV decoder treats
`0x8000` as a *parse failure* rather than a value: the key is left absent from
its decoded map, and a downstream `.at()` throws `std::out_of_range`. So the
spec-compliant packet crashes a real consumer, and the correctness argument for
the sentinel — that it refuses to state a number it cannot support — is exactly
what that consumer cannot survive.

This is the fork: whether "correct" here means compliant with the standard or
consumable by the readers that exist.

# Decision

**Keep the `0x8000` sentinel as the default, and add two mutually exclusive
opt-in flags that override it.** Neither is on unless asked for.

- `--clamp-attitude` clamps Items 6/7 to their spec limits, so a numeric value
  is always present. For consumers that cannot handle a missing key.
- `--force-emit-attitude` extrapolates the ST 0601 linear encoding past its
  defined range and emits the actual angle. **Non-compliant output**; it exists
  because some consumers want fidelity over conformance and will accept the
  consequences.

Three properties make this safe enough to ship:

- **The default is unchanged.** Anyone not passing a flag gets the same
  spec-compliant packet as before, so the tool's out-of-the-box behavior is
  still the one [[transform-stage]] argued for.
- **Items 90/91 are never affected.** Whichever flag is set, Sensor Pitch/Roll
  carry the accurate full-range value, so the truthful number is always in the
  packet somewhere. This is what makes clamping tolerable rather than lossy:
  the lie in Item 6 has a correction sitting next to it.
- **The coverage report names what it did.** A clamped item renders as
  `(clamped)` and a forced item as `(out-of-spec)`, so the report never presents
  either as an ordinary in-range value. The tool does not quietly disagree with
  the standard.

The flags are rejected in combination — `main` errors out rather than picking a
winner, because there is no sensible precedence between "understate it" and
"overstate it".

# Alternatives considered

## Clamp by default

Make the consumable thing the default and hide the sentinel behind a flag.

**Rejected.** It makes the tool's ordinary output non-conformant in substance
while looking conformant on the wire — the failure mode is silent and lands on
whoever reads the KLV later, not on whoever chose the flag. The compliant
behavior should be what you get for not thinking about it.

## Emit nothing when out of range

Omit Items 6/7 entirely instead of sending `0x8000`.

**Rejected.** It has the sentinel's problem without its precision: a consumer
that chokes on a missing key chokes identically on an omitted item, and a
consumer that handles `0x8000` correctly loses the explicit "we measured this
and it was out of range" signal. Strictly worse than the default on both sides.

## Fix it downstream, in the consumer

Treat that decoder's `.at()` as the consumer's bug to fix.

**Rejected as the *only* answer, not as a diagnosis.** It is their bug. But this
tool does not control that decoder's release cycle, and a converter whose output
only works against corrected consumers is not much of a converter. The flag is
the pragmatic bridge; it is not an endorsement of the decoder's behavior.

# Consequences

- The transform's out-of-range path now has three branches instead of one, and
  the two new ones are the ones no fixture exercises — see the open question
  below.
- `--force-emit-attitude` produces packets that a conformance checker should
  reject, and that is intended, not a defect to file. Anything comparing our
  output against ST 0601 must know which flags produced it.
- The forced encoding **saturates**, and this is a real limit rather than a
  theoretical one: `encode_out_of_spec_angle` extrapolates linearly but pins the
  result to `±0x7FFF`.
  > **Corrected 2026-07-28.** This bullet originally said saturation began
  > "beyond roughly twice the spec limit (±40° for Item 6, ±100° for Item 7)",
  > so fidelity was "recovered over part of the excess range". Both are wrong,
  > and wrong in the flattering direction. The scale is the spec's own
  > (`0x7FFF/limit`) and the function only runs when `|degrees| > limit`, so it
  > saturates **always** — force-emit emits exactly the clamped value. Measured,
  > not re-derived: [[0008-force-emit-attitude-cannot-work]]. Items 90/91 remain
  > the only trustworthy source, which this bullet did get right.
- `to_0601()` and `mux_to_ts()` both grew two boolean parameters. Two adjacent
  bools at a call site is a mistake waiting to happen; if a third mode ever
  appears this should become an enum. *(Back to one, per the revision below.)*

# Revision — 2026-07-28: one of the two escape hatches is gone

`--force-emit-attitude` was removed. It never did what this ADR said it did:
ST 0601 maps ±limit onto the whole int16 range, so there is no encoding for a
larger angle, and the flag emitted exactly the clamped value while labeling it
out-of-spec. Found by the tests this ADR asked for.
[[0008-force-emit-attitude-cannot-work]] carries the reasoning and the removal.

**The decision above otherwise stands unchanged**: the `0x8000` sentinel is
still the default, `--clamp-attitude` is still the opt-in for consumers that
cannot parse it, and Items 90/91 are still where the true attitude lives — which
this ADR always said, and which is now the only answer for fidelity rather than
one of two.

# Assumptions / open questions

- ~~**No test covers either flag.**~~ **Closed 2026-07-28.** What survives the
  removal is covered: `check_attitude_clamped` (both signs, both items, Items
  90/91 still true) and `check_attitude_in_range_is_untouched`. Writing these is
  what found that force-emit never extrapolated — see the correction above and
  [[0008-force-emit-attitude-cannot-work]].
- Assumes that decoder's behavior is representative of the consumers that
  matter. It is one reader, and the flag was designed from one failure report;
  if others turn out to mishandle `0x8000` differently, clamping may not be the
  shape of the fix they need either.
- ~~The saturation ceiling above was derived from the encoding, not measured.~~
  **That was the tell.** Deriving it from a reading of the code rather than
  running it produced a threshold off by the whole working range. It is now
  measured, and there is no ceiling to speak of — saturation is immediate.

# Citations

- [[transform-stage]] § Traps this code exists to get right — the original
  "never clamped" position this ADR qualifies
- [[field-mapping-analysis]] — Items 6/7 and 90/91, and the ranges 0601 assigns
  them
- `src/transform.cpp`, `put_narrow_angle` / `encode_out_of_spec_angle` — the
  three branches and the saturation behavior
