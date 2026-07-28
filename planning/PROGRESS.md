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

**The mux stage never decodes the video — and for H.264 does not pass it through
untouched either.** libmisbklv's `KlvSink` re-muxes the source's elementary
stream (parsed, never decoded, so H.264 and H.265 both convert) beside the KLV,
and each packet is timestamped from its own metadata sample, so both branches
share the file's timeline. On an H.264 source the timing NALs are rewritten —
Picture Timing SEI out, ST 0604 in — because we ask for it
([ADR 0006](../context/decisions/0006-inherited-st0604-sei-injection.md)); any
other codec is carried through unstamped, because generation is H.264-only
upstream and asking for it elsewhere refuses the conversion outright
([ADR 0007](../context/decisions/0007-h265-vs-st0604-generation.md)).
`MuxReport::st0604_generated` and the CLI both say which happened, and
`mux_test` asserts both branches. Design, the timeline rule and the failure
behaviour: [mux stage](../context/mux-stage.md).

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

1. Resolve fork 8 — remove `--force-emit-attitude`, or say what it should be
   instead ([ADR 0008](../context/decisions/0008-force-emit-attitude-cannot-work.md))
2. Resolve fork 4, Tier 2 geometry fidelity, in [ROADMAP](./ROADMAP.md), then
   derive Items 21/22/23/24/25/57/78/82–89

## Known gaps

- **Non-H.264 output carries no ST 0604.** Generation is H.264-only upstream, so
  an H.265 source converts but is unstamped
  ([ADR 0007](../context/decisions/0007-h265-vs-st0604-generation.md)). Moot for
  the airframes in scope, which record H.264; it matters only to a consumer
  reading time from the video of an out-of-scope recording, and both the report
  and the CLI say when it happened.
- Deriving Items 21/22/23/24/25/57/78/82–89 needs a boresight-to-terrain
  intersection — the substantive remaining work in the transform stage, and the
  one part of the mapping with no direct Parrot source. Its fidelity is fork 4,
  now open in [ROADMAP](./ROADMAP.md).
- **`--force-emit-attitude` does not do what it says.** It emits exactly the
  clamped value — the ST 0601 encoding has no room past ±limit — while labelling
  it out-of-spec. Found by the tests that now cover both flags. Fork 8, proposed
  in [ADR 0008](../context/decisions/0008-force-emit-attitude-cannot-work.md):
  remove the flag. Until then `transform_test` asserts the defect so it cannot
  drift further.
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
