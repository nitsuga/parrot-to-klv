---
type: Component
title: MP4 metadata extraction and Phase 1 tooling
description: How the Parrot metadata track is located, read, scrubbed, and evidenced — the minimp4 / box-scan division of labour, minimp4's traps, and the two tools that let real footage inform the repo without entering it
tags: [mp4, extraction, minimp4, phase-1]
timestamp: 2026-07-31T00:00:00Z
---

# MP4 metadata extraction

Locates the Parrot timed-metadata track in an MP4, identifies which Parrot format
it carries, and yields its samples. The first stage of the pipeline; everything
downstream consumes `MetadataSample`.

Realizes [ADR 0001](./decisions/0001-mp4-extraction-approach.md) (direct MP4
parsing over GStreamer `qtdemux`) — see its follow-on note.

## Files

| File | Role |
|------|------|
| `src/mp4_box_scan.{hpp,cpp}` | Walks the box tree for what minimp4 discards |
| `src/mp4_metadata_extractor.{hpp,cpp}` | Public API: `open()`, `extract_sample()`, `locate_sample()` |
| `src/parrot_v3.{hpp,cpp}` | Decodes a v3 sample into `ParrotV3Frame` |
| `src/parrot_proto.{hpp,cpp}` | Hand-written proto3 reader — decodes a proto sample into `ParrotProtoFrame` (ADR 0003) |
| `test/vmeta_subset.proto` | The mapped subset of Parrot's schema, transcribed; documentation and fixture reference, **not** compiled |
| `src/session_metadata.{hpp,cpp}` | Reads Parrot's untimed session tags from `moov/udta` — see § Session metadata |
| `src/parrot_scrub.{hpp,cpp}`, `src/scrub_main.cpp` | `parrot-scrub` — see § Scrubbing |
| `src/main.cpp` | `parrot-to-klv` — extract, decode, summarize |
| `test/make_fixture.py` | Generates the synthetic MP4 fixtures |
| `test/mp4_extraction_test.cpp` | `ctest` target `mp4_extraction` |
| `test/parrot_v3_test.cpp` | `ctest` target `parrot_v3` |
| `test/parrot_proto_test.cpp` | `ctest` target `parrot_proto` |
| `test/session_metadata_test.cpp` | `ctest` target `session_metadata` |
| `test/parrot_scrub_test.cpp` | `ctest` target `parrot_scrub` |
| `src/conformance.{hpp,cpp}`, `src/conform_main.cpp` | `parrot-conform` — see § Conformance reports |
| `test/conformance_test.cpp` | `ctest` target `conformance` |
| `test/conformance/` | committed reports; `conformance_real_data` replays them |

## Division of labour

**minimp4 owns the sample index** — chunk offsets, sample sizes, and per-sample
timestamps, via `MP4D_frame_offset()`.

**The box scan owns everything else**, because minimp4 structurally cannot supply
it: `MP4D_track_t::SampleDescription` is a union covering `soun` and `vide` only,
so a metadata track's sample entry is parsed past and dropped. The scan recovers
`tkhd` track IDs, `hdlr` handler types, the `stsd` sample entry (format code and
MIME string), and `tref`/`cdsc` links.

Both enumerate `trak` boxes in file order, so their track indices line up.
`open()` verifies that the handler types agree rather than trusting it — a
mismatch would silently read the wrong track.

## Format detection is never inferred

The `stsd` MIME type — `application/octet-stream;type=com.parrot.videometadata3`
and friends — is the only thing that says which Parrot format the payload uses,
and Parrot's own documentation states it is sufficient. It resolves to a typed
`ParrotFormat`, and an unrecognized MIME type is a hard error that reports every
track found.

**Do not add a fallback here.** An earlier revision defaulted to v3 when
detection failed, which would hand a proto-format ANAFI Ai file to the v3 binary
parser: structurally valid, silently wrong telemetry. Absence of a format is
recoverable; a guessed one is not.

## minimp4 traps

Vendored in `external/minimp4/`, undocumented in places, and actively misleading
in others. Verified against the source:

- **The read callback returns non-zero on *failure*.** Returning a byte count
  inverts it — see `minimp4_fgets`, which treats any truthy return as an error.
- **`MP4D_open()` returns 1 on success**, 0 on failure.
- **`MP4D_open()`'s `file_size` must be real.** minimp4 treats
  `read_pos >= read_size` as EOF, so passing 0 aborts parsing at the first skip.
- **`MP4D_frame_offset()` timestamps are in *track* timescale.** Its header
  comment says movie timescale and is wrong; the implementation reads
  `tr->timestamp[]`. It also takes six arguments, returns an *unsigned*
  `MP4D_file_offset_t` (so a `< 0` error check is dead code), and reports size
  through an out-param — there is no `MP4D_frame_size()` or
  `MP4D_frame_duration()`, despite how natural those names look.
- **`MP4D_close()` must be called** or the per-track index arrays leak.
- Only `MP4D_HANDLER_TYPE_VIDE` and `_SOUN` are defined; `'meta'` is ours to
  name.

Because `MP4D_frame_offset()` returns size and timestamp alongside the offset,
per-sample timestamps cost nothing — deriving them by summing durations from
zero is quadratic and unnecessary.

## Session metadata

Parrot's *untimed* metadata — what device recorded the file and when — sits in
MP4 tags rather than the sample track. It supplies three things the telemetry
cannot: ST 0601 Item 4 (tail number, from the airframe serial), Item 10
(platform designation, from the model), and the wall-clock anchor that turns a
v3 recording's monotonic frame clock into Item 2.

Parrot writes it **two ways**, and `parse_session_metadata` reads the first:

1. `moov/udta/meta/ilst` with iTunes-style four-character keys — `©mod` model,
   `©too` serial, `©day` media date, `©ART` friendly name, `©mak` maker,
   `©swr` software version, `©nam` title. Each key box contains a `data` box
   whose payload is 4 bytes of type indicator, 4 of locale, then the string.
2. `moov/meta/ilst` with full reverse-DNS keys (`com.parrot.serial`,
   `com.parrot.model.id`, run/boot/flight IDs), which needs the `keys` box to
   map indices to names. **Not implemented** — everything 0601 needs is in the
   four-character keys, and the extra fields it would add have no 0601 item.

**The geotag is not in the `ilst`.** Parrot puts `©xyz` directly in `moov/udta`
for Android compatibility, so a parser that only descends into the `ilst` misses
the recording's position entirely — which is also why the scrubber looks for it
there.

### The media date is second-granularity, and that bounds Item 2

`©day` is an ISO 8601 string — `"2018-10-06T18:12:52+02:00"` — with no
sub-second part. `anchor_to_session()` treats it as the time of the *first*
sample and adds each frame's elapsed monotonic microseconds, so frame-to-frame
spacing stays exact while the absolute anchor is accurate to within a second.
That is the best the file supports.

The timezone offset is the trap: dropping it puts every frame's Item 2 two hours
out, consistently and invisibly. `parse_iso8601_to_epoch` uses `timegm` (never
`mktime` — the converter's local timezone has nothing to do with when the flight
happened) and subtracts the trailing offset. It returns nothing rather than
guessing on a string it does not recognize.

### Serial and geotag are sensitive, and separately so

The serial identifies the airframe; the geotag says where it flew. Both are
withheld from printed output by default, under **separate** opt-ins
(`--with-identity` and `--with-location`) because they expose different things
about different people. The same split governs the coverage report's
`Disclosure` policy.

## Scrubbing a real recording

`parrot-scrub` (`src/parrot_scrub.{hpp,cpp}`, `src/scrub_main.cpp`) turns a real
flight into a shareable fixture by replacing its location-bearing fields with
synthetic values. That buys what synthetic fixtures cannot: a specimen of the
actual encoder, with real attitude, timing, and extension mix.

It copies the file byte for byte and patches fields **in place**. Sample sizes
never change, so every box, sample table and chunk offset stays valid, and the
output is the original file in all respects but the coordinates.

`scrub_file()` rejects an input/output alias by **file identity** before it
touches the output. This enforces the tool's no-in-place-scrub policy for every
spelling of one file, including relative paths, symlinks, and hard links; string
comparison alone cannot do that. The guard belongs in the operation rather than
only its CLI, so library callers cannot bypass it.

The immediate failure mode differs from muxing's: scrub reads the complete
input into memory and closes it before opening the output, so an alias did not
race the reader and was not confirmed to truncate the input at startup. It was
still an unintended in-place rewrite, and a failure or interruption after the
output is opened can leave that original only partially written. Rejecting
aliases fixes the stated no-in-place policy. A distinct pre-existing output is
still opened with truncation; preserving it until the scrubbed replacement is
complete requires a separate transactional write policy.

Two location channels have to be handled, and missing either defeats the
exercise:

1. The **v3 telemetry track** — latitude, longitude, altitude, and the
   automation extension's framing-target and flight-destination triples. The
   satellite count shares a word with the altitude and is preserved; it is not
   location-bearing.
2. The **`moov/udta` `©xyz` geotag** — an ISO 6709 string Parrot writes
   separately from the telemetry. Scrubbed by zeroing its digits, which
   preserves length (so no box resizes) and yields a valid coordinate.

3. **Extensions it cannot decode.** Real ANAFI recordings carry a `0x4536`
   extension in every sample that the reference documentation does not describe.
   Its contents are unknown, and the protobuf schema's LFIC makes position a
   plausible guess — so its payload is **blanked by default**, keeping the id and
   length so the extension stays well-formed and is still skipped identically.
   An extension we cannot read is one we cannot clear, and the alternative is
   leaking location out of a file reported as scrubbed.
   `--keep-unknown-extensions` opts out, for studying what an extension holds;
   the report then says plainly that the output is not scrubbed.

**It refuses anything it cannot fully scrub** — a proto-format file, or one with
no Parrot track — and writes no output in that case. An output that looked
scrubbed but still carried real coordinates would be worse than a failure.

**It does not scrub the imagery.** A video of your street is still a video of
your street once its coordinates are zeroed. `--zero-media` blanks the video and
audio payloads (preserving their sizes) for output destined for a repository.
Session metadata — serial number, friendly name, software version — is also left
alone; it identifies the aircraft rather than the place.

## Conformance reports: real-encoder evidence without the recording

Fixtures generated from the documentation cannot catch a parser that is wrong in
the same way the documentation is. Only real footage can — but real footage
cannot be committed, and scrubbing it still leaves imagery nobody wants in a
repository.

`parrot-conform` (`src/conformance.{hpp,cpp}`, `src/conform_main.cpp`) resolves
that by committing the *evidence* rather than the recording. It decodes every
sample of a real file and writes a JSON report of what the encoder actually
produced: sample-size histogram, extension ids with their payload lengths, decode
rate and error kinds, quaternion norm range, aircraft-state ranges, and a set of
pass/fail structural assertions. Reports are named by the first 16 characters of
the file's xxHash (e.g., `a0ff2d3b1fdb07ab.json`), anonymizing sensitive source
filenames.

**The report carries no position, no absolute time, and no device identity** —
not by filtering, but by construction: nothing that reads a coordinate ever
writes to the report. Latitude and longitude appear only as range checks
("was within ±90"), never as values.
`test/conformance_test.cpp` enforces this negatively, rendering a report from a
fixture with known coordinates and failing if any appear in the output at any
precision or in fixed-point form. Extend that list when you add a report field.

Workflow, on the machine that holds the footage:

```
./build/parrot-conform --scan data --reports test/conformance      # generate
ctest --test-dir build -R conformance_real_data                    # verify
```

The `conformance_real_data` test regenerates and compares. It **skips** (ctest
`SKIP_RETURN_CODE 77`) where `data/` is absent, empty, or holds nothing this
checker applies to — so it is a no-op in CI and on machines that have never held
a recording, while acting as a real regression test where the footage lives. A
non-Parrot video in `data/` is skipped rather than failed; `data/` holds whatever
the user put there.

This is also how an undocumented extension gets pinned down: the report records
`0x4536`'s payload length across every sample, which is most of what identifies
its shape.

## Testing without footage

`data/` holds real drone recordings with live location data and is never
committed, so the suite runs on synthetic MP4s in `test/fixtures/`, built by
`test/make_fixture.py` with Parrot's documented track layout
(`tkhd`/`tref(cdsc)`/`hdlr('meta')`/`nmhd`/`stsd(TextMetaDataSampleEntry)`) and
**structurally valid** v3 payloads whose field values the tests assert against.

| Fixture | Covers |
|---------|--------|
| `parrot_v3.mp4` | v3 base structure + timestamp extension |
| `parrot_v3_extensions.mp4` | every documented v3 extension, plus an unknown one to skip |
| `parrot_v3_unknown_ext.mp4` | an undecoded extension holding coordinates — the scrub regression |
| `parrot_v3_encoded.mp4` | writer that fills `content_encoding`, moving the MIME type to the second string |
| `parrot_proto.mp4` | serialized `vmeta.TimedMetadata` — drone, camera, wifi link, LFIC, tracking, plus an unmapped field to skip |
| `foreign_meta.mp4` | non-Parrot `meta` track that must be refused |
| `parrot_v3_video.mp4`, `parrot_proto_video.mp4` | the same metadata beside a **real** H.264 track, for the mux — see [[mux-stage]] |

Every fixture above but the last two carries five dummy bytes per video frame,
which is all a metadata parser ever needs: it reads the other track. Only the
mux hands the video anywhere that cares.

The expected values live in `v3_base()` in the generator, and the tests assert
against those same numbers — change one, change both.

**What these fixtures cannot prove** is that Parrot's encoder agrees with the
documentation, since they are generated *from* that documentation: a parser wrong
in the same way as the docs would still pass. Only real footage settles that, and
it has — see [[parrot-metadata-formats]] for what real recordings turned up that
the reference does not describe. There are still no fixtures for the proto
format.

The box walker is bounded on depth and string length and is checked against
truncated and single-byte-corrupted inputs under ASan+UBSan; keep that property
when editing it, since it parses untrusted file structure.

**Why:** the metadata track is the one part of an MP4 that general-purpose
demuxers drop, so the interesting work is in what they leave out.

**How to apply:** consume `Mp4MetadataExtractor`; branch on
`track_info().format` when the Parrot payload parsers land.
