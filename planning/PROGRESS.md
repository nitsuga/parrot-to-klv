# Parrot to KLV — Progress

Current status only — **present tense, volatile, rewrite freely**. The
chronological history (what landed when, decision detail) lives in
[`../context/log.md`](../context/log.md); the decided-fork register is
[`../context/decisions/index.md`](../context/decisions/index.md); the plan is
[ROADMAP.md](./ROADMAP.md). This file does not re-narrate the past.

## Now

**Phase 1 (Parse) in progress.** MP4 metadata extraction works end to end: the
track is located, its format is read from the stsd MIME type, and samples come
out with timestamps. Verified against both synthetic fixtures (`test/fixtures/`)
and real Parrot footage. Design and minimp4's traps:
[MP4 metadata extraction](../context/mp4-extraction.md).

**The v3 binary parser is done** — base structure plus the timestamp, automation
and thermal extensions, decoded to SI units. `./build/parrot-to-klv <file>.mp4`
decodes and summarises; it withholds latitude/longitude unless given
`--with-location`. Verified against real ANAFI footage (hundreds of samples,
100% decode success). Real recordings contain a 0x4536 extension in every frame
despite reference docs stating it doesn't exist; the decoder skips it correctly.

**`parrot-conform` commits real-encoder evidence without the recording** — a JSON
report of sample sizes, extension shapes, decode rates and structural assertions,
carrying no position, absolute time or device identity. `ctest` replays it against
whatever sits in `data/` and skips where there is none.

**Reports from two real ANAFI recordings are committed** in
[`test/conformance/`](../test/conformance/) — a short flight and a long one,
every sample decoded, all structural checks passing. They are the standing
answer to "does a real encoder actually produce what the decoder assumes",
and they hold across both recordings: a uniform 144-byte sample, the same three
extensions in every frame, and 0x4536 — undocumented — always present at 24
payload bytes.

**`parrot-scrub` replaces a recording's location fields** so a real flight can be
committed as a fixture. It refuses any format it cannot fully scrub, blanks the
payload of any extension it cannot decode (which includes the undocumented
0x4536 present in every real sample), and does not touch the imagery unless given
`--zero-media`. See
[MP4 metadata extraction](../context/mp4-extraction.md) § Scrubbing.

**The proto parser is done** — a hand-written proto3 wire reader
(`src/parrot_proto.{hpp,cpp}`) covering the ~13 messages the ST 0601 mapping
draws on, per [ADR 0003](../context/decisions/0003-protobuf-dependency.md).
Unknown fields are skipped and recorded, so a newer Parrot SDK decodes rather
than failing. `./build/parrot-to-klv <file>.mp4` now summarises v3 *and* proto,
withholding location in both unless given `--with-location`. It is validated
against synthetic fixtures only: no proto recording has ever been seen, and the
fixture encoder is ours rather than a reference implementation — see the ADR's
validation strategy for what that does and does not catch.

**Phase 2 (Transform) has started: Tier 1 is mapped end to end.** Either frame
type normalizes into a format-neutral `TelemetrySnapshot`
([ADR 0004](../context/decisions/0004-shared-telemetry-snapshot.md)) — units and
enum numbering resolved there, so the mapping never sees a format quirk — and
`to_0601()` emits a real ST 0601 packet through libmisbklv's registry codec,
checksum and all. `./build/parrot-to-klv <file>.mp4 --klv` shows it per frame.

**Tier 1 is complete.** Every direct mapping with a source now emits: a proto
frame with session metadata yields 33 items, a v3 frame 29. What remains unemitted
is unemittable — Items 45/46 and 134 are open mapping questions, Tier 2 needs the
geometry fork, and v3 simply lacks an ellipsoid height, air speed and spectrum.

**Session metadata is parsed** from `moov/udta`, which supplies Items 4 and 10
and — via `anchor_to_session()` — turns a v3 recording's monotonic frame clock
into a real Item 2 timestamp, to the media date's second granularity. The
airframe serial and the geotag are withheld from printed output under their own
`--with-identity` opt-in, separate from `--with-location`.

Every conversion carries a **coverage report**, which is ADR 0002's obligation
made concrete: what was emitted, what was omitted *and why*, and which Parrot
fields have no 0601 item at all. Coordinates are withheld from it unless
`--with-location`, like every other output. Tier 2 (frame centre, corner points,
slant range) is reported as `needs terrain intersection` rather than
approximated — its fidelity is still an open fork.

The field mapping is settled against the complete ST 0601.19 registry —
[field mapping](../context/field-mapping-analysis.md) is the implementation spec
for the transform stage, and libmisbklv needs no registry work for this project.
Parrot fields with no 0601 item are dropped and reported, per
[ADR 0002](../context/decisions/0002-unmappable-parrot-fields.md) — so the
transform stage owes a conversion coverage report alongside the KLV output.

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
