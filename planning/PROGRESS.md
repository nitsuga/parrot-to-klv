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

**`parrot-scrub` replaces a recording's location fields** so a real flight can be
committed as a fixture. It refuses any format it cannot fully scrub, blanks the
payload of any extension it cannot decode (which includes the undocumented
0x4536 present in every real sample), and does not touch the imagery unless given
`--zero-media`. See
[MP4 metadata extraction](../context/mp4-extraction.md) § Scrubbing.

The field mapping is settled against the complete ST 0601.19 registry —
[field mapping](../context/field-mapping-analysis.md) is the implementation spec
for the transform stage, and libmisbklv needs no registry work for this project.
Parrot fields with no 0601 item are dropped and reported, per
[ADR 0002](../context/decisions/0002-unmappable-parrot-fields.md) — so the
transform stage owes a conversion coverage report alongside the KLV output.

## In progress

- Nothing mid-flight.

## Next

1. On the machine with footage: `parrot-conform --scan data --reports
   test/conformance`, then commit the reports — real-encoder evidence in CI
   without the recording
2. Implement Parrot proto parser (protobuf) — needs a fork on the protobuf
   dependency (vendored `vmeta.proto` + protoc, vs. a minimal hand-rolled reader)
3. Begin transform stage: direct field mappings (Parrot → ST 0601 items)

## Known gaps

- Deriving Items 21/22/23/24/25/57/78/82–89 needs a boresight-to-terrain
  intersection — the substantive remaining work in the transform stage, and the
  one part of the mapping with no direct Parrot source. Fidelity choice is a
  candidate fork in [ROADMAP](./ROADMAP.md).
- Item 2 for v2/v3 recordings: the frame clock is monotonic, so wall-clock
  anchoring depends on session `media_date` (second granularity).
