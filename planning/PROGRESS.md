# Parrot to KLV — Progress

Current status only — **present tense, volatile, rewrite freely**. The
chronological history (what landed when, decision detail) lives in
[`../context/log.md`](../context/log.md); the decided-fork register is
[`../context/decisions/index.md`](../context/decisions/index.md); the plan is
[ROADMAP.md](./ROADMAP.md). This file does not re-narrate the past.

## Now

**The pipeline runs end to end: `parrot-to-klv in.mp4 -o out.ts` writes an
MPEG-TS carrying the recording's own video plus a synchronous ST 0601 KLV
stream.** Every phase has landed; what remains is depth, not plumbing —
Tier 2 geometry (fork 4) and the deferred mapping questions. See
[ROADMAP](./ROADMAP.md) for scope and phase status.

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

**The mux stage never decodes the video — but does not pass it through
untouched.** libmisbklv's `KlvSink` re-muxes the source's elementary stream
(parsed, never decoded, so H.264 and H.265 both work) beside the KLV, and each
packet is timestamped from its own metadata sample, so both branches share the
file's timeline. The video's timing NALs are rewritten on the way — Picture
Timing SEI out, ST 0604 in — because we ask for it: upstream's default now
leaves the stream byte-identical, and `mux_to_ts()` passes
`Sei0604::Generate` so our downstream client can read time from the video
([ADR 0006](../context/decisions/0006-inherited-st0604-sei-injection.md)).
`mux_test` asserts the SEI is there and carries our item 2 values.
Design, the timeline rule and the failure behaviour:
[mux stage](../context/mux-stage.md).

**Three tools build.** `parrot-to-klv` converts with `-o` (shows progress every
10%) and summarises without it (`--klv` for the 0601 mapping and its coverage;
location and device identity are withheld unless `--with-location` /
`--with-identity`; `--clamp-attitude` / `--force-emit-attitude` override the
Items 6/7 out-of-range default per
[ADR 0005](../context/decisions/0005-attitude-range-policy.md)). `parrot-scrub`
replaces a recording's location fields so a real flight can become a fixture.
`parrot-conform` commits
real-encoder evidence without the recording, and reports from two real ANAFI
flights are in [`test/conformance/`](../test/conformance/) — the standing answer
to whether a real encoder produces what the decoder assumes.

**The whole suite passes with no drone footage present**, and the one test that
uses `data/` skips cleanly without it.

## In progress

- Nothing mid-flight.

## Next

1. Resolve fork 7 — confirm whether the ANAFI Ai records H.265, then pick from
   [ADR 0007](../context/decisions/0007-h265-vs-st0604-generation.md) and
   correct the four H.265 claims to match
2. Cover the two attitude override flags in `transform_test` — both branches,
   the saturation boundary, and the mutually-exclusive rejection
3. Resolve fork 4, Tier 2 geometry fidelity, in [ROADMAP](./ROADMAP.md), then
   derive Items 21/22/23/24/25/57/78/82–89

## Known gaps

- **H.265 recordings do not convert.** Asking libmisbklv for ST 0604 refuses any
  non-H.264 source, so an H.265 input fails at open — while this file, ROADMAP,
  `mux-stage` and `mux.hpp` all still say H.264 and H.265 both work. No fixture
  is H.265, so the suite is green on a capability that is gone. Fork 7, proposed
  in [ADR 0007](../context/decisions/0007-h265-vs-st0604-generation.md); the
  four claims get corrected to match whichever way it resolves.
- Deriving Items 21/22/23/24/25/57/78/82–89 needs a boresight-to-terrain
  intersection — the substantive remaining work in the transform stage, and the
  one part of the mapping with no direct Parrot source. Its fidelity is fork 4,
  now open in [ROADMAP](./ROADMAP.md).
- **Neither attitude override flag is tested.** `transform_test` still asserts
  only the default `0x8000` path, so the clamping, the out-of-spec
  extrapolation and its saturation ceiling, and the mutual-exclusion check are
  all unverified ([ADR 0005](../context/decisions/0005-attitude-range-policy.md)
  § Assumptions).
- Item 2 for v3 recordings is anchored to session `media_date`, so it inherits
  that field's **second** granularity. Frame-to-frame spacing stays exact.
- The mux has run on real footage on an authorized machine — that is what found
  the audio-track gap the fixtures now model. The suite here still only ever
  converts synthetic input, and the fixture cannot reproduce that bug on
  gstreamer 1.20; CI's newer stack is the guard. See
  [mux stage](../context/mux-stage.md) § The fixtures this needed.
- The proto parser has never seen a real recording, and its fixtures are encoded
  by our own hand-written encoder rather than a reference implementation — see
  [ADR 0003](../context/decisions/0003-protobuf-dependency.md) § Validation.
