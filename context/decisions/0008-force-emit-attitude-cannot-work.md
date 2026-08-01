---
type: Decision
title: ADR 0008 — `--force-emit-attitude` cannot do what it promises
description: ST 0601 spends the whole int16 range on ±limit, so there is no headroom to encode a value beyond it — the flag emits exactly the clamped value while labeling it out-of-spec
decision_status: accepted
fork: 8
tags: [decision, transform, mapping, misb, 0601, cli]
generated:
  by: claude/opus-5
  at: 2026-07-28T01:00:00Z
---

# Context

[[0005-attitude-range-policy]] added `--force-emit-attitude` as the fidelity
escape hatch: where `--clamp-attitude` understates, this was to emit the real
angle and accept non-compliance. Its log entry described "extrapolating the ST
0601 encoding beyond its defined range".

**It does not do that.** Writing the tests that ADR asked for (fork 5's open
question, "no test covers either flag") produced this on the first run:

```
FAIL: Item 6 carries the true pitch when force-emitted — expected 45 ±0.01, got 20
```

At 45° of pitch, `--force-emit-attitude` emits **20.0** — bit for bit what
`--clamp-attitude` emits. The tests now assert that equality directly.

The cause is not a slipped constant, it is arithmetic:

```cpp
const double scale = 0x7FFF / limit;              // 0x7FFF per `limit` degrees
const int32_t encoded = std::round(degrees * scale);
if (encoded > 0x7FFF) value = 0x7FFF;             // always taken
```

ST 0601 maps ±limit onto the *entire* int16 range, so `0x7FFF/limit` is exactly
the spec's own scale. `encode_out_of_spec_angle` is only ever reached when
`|degrees| > limit` — that is the branch condition — so `encoded` always exceeds
`0x7FFF` and always saturates. There is no input for which this function
extrapolates anything.

**This is structural, not a bug to fix.** The item is two bytes and the standard
has already spent all of them on ±limit. No value beyond the limit has an
encoding at that scale, so "emit the actual value" is not implementable in the
field's own terms. Any encoding that did carry 45° would have to use a different
scale, which a decoder reading ST 0601 correctly would silently misread as some
other angle.

So the flag currently delivers the clamp behavior under a label that says the
opposite. That is worse than either honest option: a consumer told "out-of-spec,
use with caution" receives a clamped value and has been given a reason to trust
it as measured.

[[0005-attitude-range-policy]] § Consequences also mis-stated the threshold —
it claimed saturation "beyond roughly twice the spec limit (±40° for Item 6,
±100° for Item 7)". That is wrong, and wrong in the flattering direction: it
implied a working range that does not exist. Corrected there.

# Decision

**Remove `--force-emit-attitude`.**

The flag cannot keep its promise, and the two behaviors it could honestly
provide are already covered — spec-compliant `0x8000` by default,
consumer-friendly clamping behind `--clamp-attitude`. Removing it costs no
capability, because it has none that clamping does not.

Items 90/91 remain what they always were: the full-range pitch and roll, always
accurate, which is where a consumer wanting the true attitude should read.
That is the answer this ADR points fidelity-seekers at.

# Alternatives considered

## Redefine it as an explicit alias for clamping

Keep the flag, document it as "clamp, but mark the coverage report as
out-of-spec".

**Rejected.** There is no audience for it. A consumer that can accept a clamped
value wants `--clamp-attitude`; one that cannot is not served by the same bytes
under a scarier label. It preserves a CLI surface purely to avoid admitting the
original one was wrong.

## Re-scale so out-of-range angles encode distinctly

Map ±90° onto the int16 range for these items when the flag is set, so 45°
round-trips.

**Rejected, and it is the tempting one.** It would carry the angle — but only
to a decoder that knows to apply the non-standard scale. Every conformant ST
0601 reader would decode 45° as roughly 10° for Item 6 and take it at face
value: a wrong number presented as a right one, with nothing in the packet
saying otherwise. The current flag at least errs toward a value that is merely
understated. Silent misreading is the one failure mode worse than clamping.

## Leave it and document the limitation

**Rejected.** The label is the problem. Documentation reaches the person reading
the docs; the coverage report saying "out-of-spec" reaches everyone downstream
of a conversion.

# Consequences

- A CLI flag disappears. It is only days old and its behavior was never
  distinguishable from `--clamp-attitude`, so no output changes and no consumer
  can be relying on it for what it claimed.
- `encode_out_of_spec_angle` and the `force_emit` parameter came out of
  `to_0601()` and `mux_to_ts()`, returning those signatures to one boolean and
  removing the mutually-exclusive check — and its CTest — along with them.
- The test asserting the defect went with the flag; the clamp and in-range
  coverage stayed.
- **The flag is rejected, not ignored.** An unrecognized argument is silently
  dropped by the CLI, and this one used to change output, so passing it now
  errors with what it actually did and where to read the true attitude. A script
  still carrying it gets told rather than quietly getting the default.
- ADR 0005 records the correction in place rather than being quietly rewritten:
  the reasoning that led to the flag was sound, and the error was in believing
  the encoding had room.

# Assumptions / open questions

- Assumes nobody is using the flag. It shipped on 2026-07-27 and is
  indistinguishable from `--clamp-attitude` in output, so "using it" can only
  mean having typed it, not depending on its effect.
- Assumes no consumer wants the re-scaled variant with prior agreement about
  the scale. If one ever does, that is a different feature — a private
  convention, negotiated, and not something to reach by way of a flag whose name
  suggests standard behavior.
- Whether Items 90/91 are a sufficient answer for fidelity-seeking consumers is
  untested against a real one, same as fork 5's original assumption.

# Citations

- [[0005-attitude-range-policy]] — the ADR that introduced the flag, and whose
  § Consequences mis-stated the saturation threshold
- `src/transform.cpp`, `encode_out_of_spec_angle` — the scale that leaves no
  headroom
- `test/transform_test.cpp`, `check_attitude_clamped` and
  `check_attitude_in_range_is_untouched` — what survives the removal
