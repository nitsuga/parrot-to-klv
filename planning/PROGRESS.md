# Parrot to KLV — Progress

Current status only — **present tense, volatile, rewrite freely**. The
chronological history (what landed when, decision detail) lives in
[`../context/log.md`](../context/log.md); the decided-fork register is
[`../context/decisions/index.md`](../context/decisions/index.md); the plan is
[ROADMAP.md](./ROADMAP.md). This file does not re-narrate the past.

## Now

**Phases 0–1 are done and Phase 2 is most of the way there; Phase 3 (Mux) is the
next work.** See [ROADMAP](./ROADMAP.md) for scope and phase status.

**Parsing works end to end for both supported formats.** The metadata track is
located and its format read from the stsd MIME type (never inferred); v3 and
proto samples decode to SI units; MP4 session metadata supplies the device and
the recording date. Design, minimp4's traps and the session tags:
[MP4 metadata extraction](../context/mp4-extraction.md).

**Tier 1 of the transform is complete.** Either frame type normalizes into a
format-neutral `TelemetrySnapshot`
([ADR 0004](../context/decisions/0004-shared-telemetry-snapshot.md)), and
`to_0601()` emits a real ST 0601 packet through libmisbklv's registry codec,
checksum and all, alongside a conversion coverage report naming everything it
could not carry ([ADR 0002](../context/decisions/0002-unmappable-parrot-fields.md)).
What remains unemitted is unemittable without a decision or an input the
recording lacks. Shape and traps: [transform stage](../context/transform-stage.md).

**Three tools build.** `parrot-to-klv` converts and summarises (`--klv` for the
0601 mapping and its coverage; location and device identity are withheld unless
`--with-location` / `--with-identity`). `parrot-scrub` replaces a recording's
location fields so a real flight can become a fixture. `parrot-conform` commits
real-encoder evidence without the recording, and reports from two real ANAFI
flights are in [`test/conformance/`](../test/conformance/) — the standing answer
to whether a real encoder produces what the decoder assumes.

**The whole suite passes with no drone footage present**, and the one test that
uses `data/` skips cleanly without it.

## In progress

- Nothing mid-flight.

## Next

1. **Phase 3 (Mux)**: remux video + KLV into MPEG-TS through libmisbklv
2. Resolve fork 4, Tier 2 geometry fidelity, in [ROADMAP](./ROADMAP.md), then
   derive Items 21/22/23/24/25/57/78/82–89

## Known gaps

- Deriving Items 21/22/23/24/25/57/78/82–89 needs a boresight-to-terrain
  intersection — the substantive remaining work in the transform stage, and the
  one part of the mapping with no direct Parrot source. Its fidelity is fork 4,
  now open in [ROADMAP](./ROADMAP.md).
- Item 2 for v3 recordings is anchored to session `media_date`, so it inherits
  that field's **second** granularity. Frame-to-frame spacing stays exact.
- The proto parser has never seen a real recording, and its fixtures are encoded
  by our own hand-written encoder rather than a reference implementation — see
  [ADR 0003](../context/decisions/0003-protobuf-dependency.md) § Validation.
