# Knowledge Bundle Log

<!-- Durable chronological record, newest first, ISO YYYY-MM-DD day headings.
     One thin line per decision (chronology + link — the ADR owns the rationale);
     a fuller entry per significant implementation. Routine ingests/lint go here
     too. This is the ONLY home for history — PROGRESS.md is present-tense only. -->

## 2026-07-25

* **Scope narrowed to v3 and proto; phase statuses corrected; fork 4 opened.**
  A planning pass, no code.
  - **Supported formats are now v3 and proto** — the ANAFI range and ANAFI Ai /
    UKR, every Parrot drone still in production, both implemented. **v2** (Bebop
    2, Disco, Bluegrass) becomes a deferred add-on rather than committed scope;
    ADR 0004's normalization boundary means it would add one normalizer and
    touch nothing else if it ever lands. **v1** moves to out of scope, being
    deprecated by Parrot. ROADMAP § Scope, README and CLAUDE.md all said "all
    Parrot formats" and now say what is actually supported.
  - **The open mapping questions and the integration API are deferred**, and
    said to be so rather than left looking like pending work: Items 134, 45/46,
    48 and Tier 3 VMTI stay enumerated in [[field-mapping-analysis]] § Open
    questions, and each is currently *reported* as an omission rather than
    guessed at, so deferring them costs correctness nothing.
  - **Phase statuses corrected.** Phase 0 had read "(in progress)" while Phases
    1 and 2 had largely landed. Now: 0 done, 1 done for the supported formats,
    2 Tier 1 done, 3 next.
  - **Fork 4 (Tier 2 geometry fidelity) promoted from candidate to `OPEN`** —
    its stated trigger was the transform stage reaching the derived items, which
    it now has.
* **`camera.spectrum` added to the proto reader; Item 11 emitted — Tier 1 is
  now complete.** Field 17 of `CameraMetadata`, transcribed into
  `test/vmeta_subset.proto` alongside the `CameraSpectrum` enum, normalized to a
  format-neutral `ImageSpectrum` by name (as with the piloting modes).
  - **Item 11 is free-form utf8**, not a two-valued enum — the standard calls it
    "name of currently active sensor", and §8.11.1's example configuration names
    sensors `EO Narrow`, `EO Wide`, `IR Sensor`. So `camera.subtype` (field 18)
    was added alongside and the name is composed: the spectrum gives the stem
    (`EO`, `IR`, or `EO/IR Blended` for a fused image rather than one of its
    halves), the subtype qualifies it — `EO Wide`, `IR Tele`.
  - An unknown **spectrum** emits nothing even with a known subtype: a bare
    `Wide` does not say whether the imagery is visible or thermal. An unknown
    **subtype** leaves the stem unqualified.
  - The omission reason now distinguishes the two cases it always should have:
    v3 reports Item 11 as `format does not carry it`, a proto frame that simply
    did not report a spectrum as `not present in this frame`.
  - A proto frame with session metadata now yields 33 items, a v3 frame 29.
    Everything still unemitted is unemittable: 45/46 and 134 are open mapping
    questions, Tier 2 awaits the geometry fork, and v3 genuinely lacks an
    ellipsoid height, air speed and spectrum.
* **MP4 session metadata parsed; Items 2, 4 and 10 unblocked.**
  `src/session_metadata.{hpp,cpp}` reads Parrot's untimed tags from `moov/udta`.
  See [[mp4-extraction]] § Session metadata.
  - Reads the **four-character key method** (`moov/udta/meta/ilst` — `©mod`
    model, `©too` serial, `©day` media date, `©ART`, `©mak`, `©swr`, `©nam`).
    The full reverse-DNS method under `moov/meta/ilst` is not implemented: it
    needs the `keys` box for index-to-name mapping, and the fields it would add
    beyond these have no 0601 item anyway.
  - **The geotag is not in the ilst** — Parrot puts `©xyz` directly in
    `moov/udta` for Android compatibility. A parser that only descends into the
    ilst misses the recording's position, which is also why the scrubber looks
    for it there.
  - **Item 2 for v3 now works.** `anchor_to_session()` treats the media date as
    the first sample's wall time and adds each frame's elapsed monotonic µs, so
    frame spacing stays exact and absolute time is good to the media date's
    *second* granularity — the limit the file imposes, recorded rather than
    papered over. A frame with a real UTC stamp is never overwritten by the
    anchor, and a session with no parsable date leaves the frame unanchored
    rather than dating it to 1970.
  - The timezone offset is the trap the tests target hardest: dropping it would
    put every frame two hours out, consistently and invisibly. `timegm`, never
    `mktime` — the converter's local timezone has nothing to do with when the
    flight happened — and the conversion was cross-checked against Python.
  - **Device identity got its own opt-in.** The serial reaches Item 4, so the
    coverage report could print it; that is identity, which the conformance
    reports exclude by construction. Rather than folding it into
    `--with-location`, `Disclosure` now has separate `location` and `identity`
    flags (CLI: `--with-location`, `--with-identity`) — they expose different
    things about different people.
  - Item 11 (EO/IR) remains the one Tier 1 item with no source: session tags do
    not carry the spectrum and the proto reader drops `camera.spectrum`.
    Omitted rather than assumed to be EO.
* **Fork 3 (transform input shape) resolved** —
  [[0004-shared-telemetry-snapshot]], `accepted`. Opened and decided as Phase 2
  began; [[0003-protobuf-dependency]] had explicitly deferred it.
* **Transform stage started: Tier 1 mapped end to end.** `src/telemetry.*`
  normalizes either frame type into a `TelemetrySnapshot`; `src/transform.*`
  maps that to a real ST 0601 packet through libmisbklv's registry codec. See
  [[transform-stage]] for the shape and the traps.
  - ~26 items from a typical frame, assembled with UL key, BER length and Item 1
    checksum. `transform_test` parses every emitted packet back through
    `misbklv::parse_packet` and decodes item by item, so values are checked as a
    consumer sees them rather than as we staged them.
  - **A privacy bug caught while testing the CLI**: the coverage report printed
    every emitted item's value, including Items 13/14 — latitude and longitude —
    regardless of `--with-location`. On real footage `--klv` would have leaked
    the flight's position into exactly the output someone pastes into an issue.
    `to_text()` now withholds Items 13/14/40/41 unless asked, and the test
    asserts both directions.
  - Sign and range traps handled and tested: Item 51 negated (0601 measures
    toward zenith, Parrot NED is positive down); Items 6/7 signalled with
    `0x8000` when out of their ±20°/±50° range rather than clamped, with 90/91
    carrying the true value; Item 112 omitted below 0.5 m/s because course over
    ground is undefined at rest; `asin` clamped so a barely-non-unit v3
    quaternion cannot yield NaN pitch.
  - **Item 2 is mandatory and still sometimes absent.** No v3 recording can
    supply it, and proto may declare its UTC stamp invalid. The packet is then
    assembled without mandatory enforcement and the omission is reported — a
    fabricated timestamp is indistinguishable from a real one downstream, a
    missing item is not.
  - The coverage report is ADR 0002's obligation made concrete, and is wired
    into `parrot-to-klv --klv`: emitted items with values, omitted items each
    with a reason, and the Parrot fields with no 0601 item at all. Tier 2
    geometry is reported as `needs terrain intersection` rather than
    approximated — its fidelity fork is still open.
* **Proto parser implemented** — `src/parrot_proto.{hpp,cpp}`, a hand-written
  proto3 wire reader per [[0003-protobuf-dependency]]. `parrot-to-klv` now
  summarises v3 and proto both, and `test/vmeta_subset.proto` records the
  transcribed schema subset (documentation only — nothing compiles it).
  - **Schema traps the transcription surfaced**, each of which would have been a
    silent mis-decode: `Location.altitude_egm96amsl` is field **7**, not 4 (4 is
    `sv_count`); `battery_percentage` and wifi `rssi` are `sint32`, so **ZigZag**
    — reading them as plain varints turns −55 dBm into 109; the proto
    `PilotingMode` is offset by one from the binary enum (`PM_MANUAL` = 1, binary
    `Manual` = 0), so it gets its own C++ type rather than sharing `PilotingMode`;
    and proto carries the FOVs in **radians** where v3 uses degrees, named
    `picture_hfov_rad` so the difference cannot be missed at a call site.
  - **Bug caught by its own test:** message fields originally called
    `read_bytes()` without checking the wire type, so a group tag (wire 3) on
    e.g. field 1 read the next byte as a length — and a zero there decoded as a
    valid *empty* submessage. A mis-synced stream would have parsed "cleanly"
    into a default-valued frame. Now routed through `read_msg()`, which insists
    on wire type 2.
  - No depth guard, deliberately: no parser calls itself, so nesting is bounded
    by the schema (three deep) rather than by the input. An earlier draft
    declared a `DepthExceeded` error it never enforced; removed rather than
    shipped.
  - Tests cover the fixture field-by-field plus the wire edges — truncation,
    over-long varints, undefined wire types, field number 0, ZigZag at −1,
    unknown fields of every wire type, and a Starfish `oneof` that must not
    count as a Wi-Fi link. `parrot_proto.mp4` now holds real serialized
    `TimedMetadata`; it previously held v3 bytes under a proto MIME type, which
    only ever exercised format *detection*.
  - **Validation is weaker than for v3, and stays that way for now**: no proto
    recording exists to check against, and the fixture encoder in
    `make_fixture.py` is hand-written because this machine has no protobuf
    toolchain. Encoder and decoder are both ours, so a shared misreading of
    proto3 would pass. Tracked as an open question in the ADR.
* **Fork 2 (protobuf dependency) resolved** —
  [[0003-protobuf-dependency]], `accepted`. Opened from the ROADMAP candidate
  list and decided in the same session.
* **Verified on the footage machine: the updated tool reproduces both committed
  reports byte-for-byte.** Regenerating over the real recordings left the working
  tree clean, which settles the two things that could only be predicted from a
  machine without footage — the schema v3 reports edited in place match what the
  renderer emits, and the quaternion-seeding fix does not move either report (so
  those recordings' first samples carry uniform norms, consistent with the
  1.000/1.000 they already showed).
* **Consistency scan over the conformance work; two fixes in
  `run_conformance`.** Swept the sources and docs after the tooling landed
  across two sessions:
  - **Quaternion norm range was seeded once per quaternion instead of once per
    sample.** `first` is only cleared after the per-sample loop, so all three
    quaternions of the first decoded sample each re-seeded min/max and the two
    before the last were discarded. Narrow effect — it could hide an outlier
    occurring only in quats 1–2 of sample 0 — and it never weakened an
    assertion, since `quaternions_are_unit_norm` tests every quaternion
    unconditionally. Fixed with a per-sample `first_quat` flag.
  - `XXH3_createState()`'s result was dereferenced unchecked; now reported as an
    error like any other unreadable input.
  - The fixture report is byte-identical across the fix (its quaternions are all
    unit, so the discarded values equalled the kept one). That confirms the
    committed reports stay valid under `--check`; it does not exercise the bug,
    which needs a first sample with differing norms.
  - Doc drift found and fixed: `CLAUDE.md` § Repo layout still described
    `external/` as minimp4 alone after xxhash.h was vendored beside it. Nothing
    else was stale — no `input_sha256` survives outside this log's history.
* **Report schema v3: `input_sha256` renamed to `input_hash`.** The field had
  held an xxHash since v2 while still being named for SHA-256 — misleading in a
  committed artifact, and the "kept for compatibility" justification was empty
  since no v1 reports survived the v2 regeneration. The two committed reports
  were edited in place rather than regenerated (the footage is gone); the
  rename and the version bump are their only textual changes, and the renderer's
  output on a fixture was diffed against them to confirm `--check` will still
  match byte-for-byte when footage is present.
* **Conformance reports committed for two real ANAFI recordings.** Ran
  `parrot-conform --scan data --reports test/conformance` on the machine that
  held the footage; the recordings have since been removed, the reports remain.
  Snapshot at write time — 699 samples (92 MB) and 10,709 samples (1.2 GB),
  **100% decoded, zero errors, every structural check passing on both**:
  - **The sample shape does not vary.** 144 bytes in every sample of both
    recordings — no histogram spread at all — and the same three extensions in
    every frame at fixed payload lengths: `0x4531` (8 B), `0x4533` (28 B),
    `0x4536` (24 B). The decoder's fixed-layout assumption holds against a real
    encoder across two flights.
  - **0x4536 is confirmed real and universal**, at 24 payload bytes in all
    11,408 samples, despite reference docs stating it does not exist. Its
    length across every sample is now committed evidence — most of what would
    identify it if it is ever worth decoding.
  - Quaternion norms 1.000 min and max in both; frame deltas 33,323–33,411 µs
    (30 fps, monotonic and strictly increasing throughout).
  - The long recording exercised more of the state machine —
    `landed`/`taking-off`/`flying`/`hovering`/`emergency` — so the flying-state
    decode is covered beyond the steady-state cases, `emergency` included.
  - Both flights were `manual` piloting mode, so no automated-mode evidence yet;
    that remains unexercised by real footage.
  - `data/` on this machine is now empty, so `conformance_real_data` skips (77)
    as designed — verified after the deletion.
* **`parrot-conform` switched to xxHash and anonymizes report filenames.**
  Performance and security improvements:
  - Replaced SHA-256 with xxHash XXH3_128 for 20-100× faster file identification
    (1.2GB scan: 2s total vs 6+s, hash step <100ms vs 4+s). Hash is for
    identification only, not security, so collision resistance of 128 bits is
    plenty.
  - Report filenames now derived from hash prefix (`a0ff2d3b1fdb07ab.json`)
    instead of source filename, anonymizing sensitive video names. Label field
    defaults to same prefix. Console still shows original filename for context.
  - Added file-size display and progress dots (every 100MB) during hashing.
  - Schema version bumped to 2 (hash algorithm change). `input_sha256` field now
    holds xxHash XXH3_128, not SHA-256.
  - Vendored xxhash.h single-header library (BSD-2-Clause).
* **Doc lint after `parrot-conform`.** Four items, two of them rule violations
  rather than ordinary staleness:
  - `PROGRESS` "Next" had grown a *fork* — the protobuf-dependency choice for the
    proto parser — stated inline. Forks live in ROADMAP; PROGRESS points at them.
    Moved to ROADMAP candidates, PROGRESS now defers ("open work has one home").
  - `CLAUDE.md` § Repo layout said "tests must never require anything from
    `data/`", which reads as false now that `conformance_real_data` uses `data/`
    when present. The rule still holds in substance and is now stated precisely:
    no test may *require* footage, one test may *use* it and skip without.
  - [[mp4-extraction]]'s Files table predated the conformance tool, and its title
    and description still described a doc about extraction alone. Retitled to
    cover the Phase 1 tooling it has actually accumulated.
* **`parrot-conform` added: real-encoder evidence without the recording.** The
  test-data problem had two unsatisfying answers — commit a scrubbed video (still
  imagery nobody wants in a repo) or source a public one (attempts kept turning up
  files whose metadata track had been transcoded away). This commits the
  *evidence* instead: a JSON report of what a real encoder produced — sample-size
  histogram, extension ids with payload lengths, decode rate and error kinds,
  quaternion norm range, aircraft-state ranges, and structural assertions.
  - **No position, absolute time, or device identity, by construction** rather
    than by filtering: nothing that reads a coordinate writes to the report, and
    latitude/longitude appear only as range checks. `conformance_test` enforces it
    negatively — renders a report from a fixture with known coordinates and fails
    if any appear at any precision or in fixed-point form.
  - `ctest` target `conformance_real_data` regenerates and compares against the
    committed reports, so a decoder change that alters how a real recording reads
    is caught. Skips (`SKIP_RETURN_CODE 77`) where `data/` is absent, empty, or
    holds nothing applicable — a no-op in CI, a real regression test where the
    footage lives. A non-Parrot video in `data/` is skipped, not failed.
  - Reports are tied to recordings by hash (originally SHA-256, now xxHash), so a
    report whose input was replaced is detectable rather than silently stale.
  - Side benefit: the report records `0x4536`'s payload length across every
    sample, which is most of what would identify that undocumented extension.
* **Doc lint.** Six pieces of drift, all from the last two days' work outrunning
  the docs describing it:
  - [[mp4-extraction]] still said "these fixtures prove MP4 handling only… the
    sample payloads are invented", which stopped being true when the generator
    started emitting real v3 structs. Replaced with a fixture table and the
    sharper caveat that actually holds: fixtures generated *from* the docs cannot
    catch a parser wrong in the same way the docs are — only real footage can,
    and did.
  - Its Files table listed four files out of nine, predating the v3 decoder and
    the scrubber; its frontmatter description predated scrubbing too.
  - [[field-mapping-analysis]] still asserted flatly that v2/v3 have "no LFIC",
    contradicting the hedged 0x4536 status in [[parrot-metadata-formats]]. Items
    40/41/42 now carry the same open question rather than a claim the sibling doc
    withdraws.
  - [[parrot-video-metadata-docs]] listed LFIC among the reference's documented
    extension structures. It is not there.
  - `parrot-metadata-formats.md` frontmatter still carried its 2026-07-23
    timestamp and original description after being substantially rewritten.
  - README structure block didn't mention `test/fixtures/` or that `src/` now
    builds two tools.
* **Scrubber leak fixed: undecoded extension payloads passed straight through.**
  Found immediately after the 0x4536 discovery below, and demonstrated with a
  fixture whose 0x4536 payload held coordinates — `parrot-scrub` reported success,
  listed its usual caveats, and left all of them in the output. Since real ANAFI
  recordings carry 0x4536 in *every* sample, the tool's central promise was false
  for exactly the files it exists to handle.
  - Undecoded extension payloads are now blanked by default, id and length kept
    so the extension stays well-formed and is skipped identically. Known-safe
    extensions (timestamp, thermal) are preserved; automation is rewritten as
    before. `--keep-unknown-extensions` opts out and the report then states the
    output is **not** scrubbed.
  - The regression test asserts the coordinates hidden inside the extension are
    absent from the whole output — the same standard the other scrub tests use.
  - The general rule this encodes: an extension we cannot decode is one we cannot
    inspect, so it cannot be trusted. "Unknown" has to mean "cleared", not
    "assumed harmless".
* **v3 decoder verified against real Parrot ANAFI footage.** A real recording with
  hundreds of samples decoded successfully at 100% completion rate. All base
  structure fields parse correctly: state, mode, battery level, RSSI, satellite
  count, altitude AGL, NED velocities, field-of-view angles, quaternions (drone and
  frame), and microsecond timestamp. Automation extension detected and parsed in
  every sample. Quaternion magnitudes validate at exactly 1.0, confirming the
  fixed-point decoding is correct.
  - **0x4536 extension appears in every real sample.** The reference docs say "0x4536
    appears nowhere" and that LFIC exists only in protobuf format, but real v3
    recordings contain a 0x4536 extension in every frame. The decoder correctly skips
    it by `ext_length` (unknown extensions are logged but don't cause failure), so
    decoding succeeds. This suggests the reference documentation may be incomplete
    rather than the encoder being wrong. The extension's content is not needed for
    ST 0601 field mapping since target coordinates come from the automation
    extension's `framing_target_*` fields.
* **Metadata scrubber added** (`parrot-scrub`). Turns a real flight into a
  shareable fixture by replacing its location fields with synthetic values,
  patching **in place** over a byte-for-byte copy so sample sizes never change
  and every box, table and chunk offset stays valid — the output is a specimen of
  the real encoder in all respects but the coordinates.
  - Two separate location channels: the v3 telemetry track (latitude, longitude,
    altitude, and the automation extension's two coordinate triples) and the
    `moov/udta` `©xyz` ISO 6709 geotag, which no metadata-level scrub would
    otherwise touch. The geotag is neutralised by zeroing its digits, preserving
    string length so nothing needs resizing.
  - **Refuses what it cannot fully scrub** (proto format, non-Parrot files) and
    writes no output — a file that looked scrubbed while still carrying real
    coordinates would be worse than a failure.
  - Does **not** touch the imagery; `--zero-media` blanks video/audio payloads
    for output destined for a repository. The tool says so rather than leaving it
    to be discovered.
  - Tests assert the original coordinates' encoded byte pattern is absent from
    the whole output, not merely that the decoded fields changed.
* **Parrot v3 binary parser implemented** (`src/parrot_v3.{hpp,cpp}`). Decodes the
  72-byte base structure and the timestamp (0x4531), automation (0x4533) and
  thermal (0x4534) extensions, converting fixed-point to SI at parse time.
  Unknown extensions are skipped by `ext_length` and recorded rather than
  ignored; a sample that does not decode cleanly returns a typed error instead of
  a partial frame. `test/make_fixture.py` now emits structurally valid v3 payloads
  with known values, so `parrot_v3_test` asserts field-by-field against the
  generator's inputs. Clean under ASan+UBSan over 60,300 random buffers.
* **Two reference errors corrected while implementing it.**
  - **There is no binary LFIC extension.** `0x4536` appears nowhere in the
    reference; LFIC exists only in the protobuf schema. [[parrot-metadata-formats]]
    listed it as a standard v2/v3 extension and [[field-mapping-analysis]] cited
    it as the v2/v3 source for Items 40/41/42 — same class of fabrication as the
    ST 0601 tag numbers. Items 40/41/42 now cite the automation extension's
    `framing_target_*` for v3, and nothing for v2.
  - **v2 and v3 are not layout-compatible.** They share a prefix through
    `drone_z`, then diverge: v3 inserts a frame-base quaternion, drops v2's
    explicit `camera_pan`/`camera_tilt`, adds AWB gains / FOV / link quality, and
    reorders the trailing four bytes. [[parrot-metadata-formats]] had described a
    single shared "base structure"; it now carries both byte layouts in full.
    Also recorded: binary and protobuf number `piloting_mode` differently, so the
    enum maps must go through names, not values.
* **Extraction verified against real Parrot footage.** `./build/parrot-to-klv
  data/P0330036.MP4` detected 699 samples and extracted successfully — the box
  scan and format detection survive a real ANAFI file the synthetic fixtures
  don't represent.
* **CMakeLists.txt fixed to use SSH URLs.** FetchContent was using HTTPS for
  libmisbklv, prompting for GitHub credentials even with SSH keys configured.
  Changed `GIT_REPOSITORY` from `https://github.com/...` to `git@github.com:...`
  so it uses SSH authentication.
* **MP4 metadata extraction works end to end.** The extractor did not compile;
  fixing it turned up seven distinct defects, most of them fabricated API:
  - `MP4D_frame_size()` and `MP4D_frame_duration()` do not exist in minimp4.
    `MP4D_frame_offset()` takes six arguments, was called with four, and returns
    an *unsigned* `MP4D_file_offset_t`, so the `offset < 0` guard was dead. Its
    out-params supply size and timestamp together, which also deleted an O(n²)
    loop that re-summed durations from zero for every sample (~162M iterations
    on a 10-minute 30 fps track).
  - `MP4D_HANDLER_TYPE_META`/`_NONE` do not exist — minimp4 defines only `VIDE`
    and `SOUN`. `track.duration_units` does not exist either (`duration_hi`/`lo`).
  - The read callback returned the byte count, but minimp4 treats **any truthy
    return as failure** (see `minimp4_fgets`), so a successful read read as an
    error. `MP4D_open` returns 1 on success and the caller tested `!= 0`.
  - `MP4D_open(..., 0)` passed a zero file size; minimp4 treats
    `read_pos >= read_size` as EOF, so parsing aborted at the first skip.
  - `MP4D_close()` was never called — the per-track index arrays leaked.
* **Format detection is real, and no longer guesses.** `extract_mime_type()` was
  a stub returning `""`, and the caller then *defaulted to v3* — which would have
  fed a proto-format ANAFI Ai file to the v3 binary parser and produced confident
  garbage. minimp4 cannot supply it: `MP4D_track_t::SampleDescription` is a union
  covering `soun` and `vide` only, so a metadata track's sample entry is parsed
  past and dropped. New `src/mp4_box_scan.{hpp,cpp}` walks the box tree directly
  (consistent with [ADR 0001](./decisions/0001-mp4-extraction-approach.md)) and
  recovers `tkhd` track IDs, `hdlr` handler types, the `stsd` sample entry, and
  `tref`/`cdsc` links. The MIME type now yields a typed `ParrotFormat`, and an
  unrecognised one is a hard error listing what was found.
* **Test fixtures are synthetic and committed.** `test/make_fixture.py` builds
  MP4s with Parrot's documented track layout and invented payloads, so extraction
  is testable without committing footage that carries live location data. Covers
  v3, proto, a writer that fills in `content_encoding` (moving the MIME type to
  the second string), and a non-Parrot `meta` track that must be refused. Wired
  into `ctest` as `mp4_extraction`; the suite passes, and the box walker survives
  6701 malformed inputs (every truncation plus every single-byte corruption at
  three values) clean under ASan+UBSan.
* **Doc lint after the extraction work.** New [[mp4-extraction]] Component doc
  (first entry under `index.md` § Components) carries the durable knowledge the
  above turned up — the division of labour and the minimp4 traps — so it isn't
  re-derived from a log entry. ADR 0001 gained a follow-on note: implementation
  took *both* of its options (minimp4 for the sample index, a custom reader for
  the sample entry), which sharpens the decision rather than changing it. Also
  added `test/` to the README and `CLAUDE.md` structure listings, and converted
  two `[[wiki]]` links in `planning/` to relative Markdown — wiki links resolve
  within the bundle, and the link-check CI only validates Markdown ones.
* **Field mapping rewritten against the complete ST 0601.19 registry.** libmisbklv
  commit `a981a1a` registers §8.1–8.143 (all but deprecated Item 66), validated
  against the standard's worked examples, so the mapping could finally be settled
  item by item instead of guessed.
  - [[field-mapping-analysis]] was **substantially wrong above tag 65** and is
    replaced, not amended. Its "missing tags" tables carried tag numbers written
    from assumption: 3, 53, 54, 92, 93, 103, 106, 107, 108, 114, 134 and 135 are
    all different items than it claimed, and the "Sensor Lat/Lon/Alt Extended" and
    "Platform Groundspeed Extended" items it proposed adding do not exist in 0601
    at all. It also premised a precision problem that isn't real — Items 13/14 are
    int32 mapped over ±90/±180, ~4.7 mm.
  - Its four-phase plan is gone with it. The old Phase 2/3 existed to extend
    `uas0601.toml`; **no registry work is required for this project.** Replaced by
    three tiers: direct mappings, derived geometry, VMTI.
  - Real items the full registry unlocked: 90/91 Platform Pitch/Roll (Full), which
    matter because Items 6/7 cap at ±20°/±50° and a quadcopter exceeds that
    routinely; 79/80 Sensor North/East Velocity with 51 Platform Vertical Speed
    (negated — §8.51 is zenith-positive, Parrot NED is down-positive); 75 Sensor
    Ellipsoid Height; 123 NAVSATs in View ← `sv_count`; 124/125/126 positioning
    and status enums; 134 Zoom Percentage; 40/41/42 Target Location ← the LFIC
    extension, which the old doc had written off as unavailable.
  - Re-read Parrot's `vmeta.proto` and the v2/v3 structs verbatim from
    `references/`, correcting two things the format doc obscured: proto `hfov`/
    `vfov` are **radians** while v2/v3 `picture_[hv]fov` are **degrees**, and
    proto `Location` carries **both** `altitude_egm96amsl` and
    `altitude_wgs84ellipsoid` — so the geoid conversion the old doc called for is
    unnecessary on proto.
* **Fork 1 opened, deliberated, and accepted: Parrot fields with no ST 0601 item**
  — [ADR 0002](./decisions/0002-unmappable-parrot-fields.md).

* **README made human-facing; rule rationale moved into the bundle.** The
  top-level README carried two agent-facing sections inherited from the template
  ("The doc taxonomy", "Why these rules"), which cluttered what a human sees and
  put workflow directives outside the docs agents read.
  - New [[workflow-rationale]] (`context/workflow-rationale.md`) is now the single
    home for the per-rule reasoning; added to `index.md` § Bundle guide, and
    `CLAUDE.md` § Planning hygiene points there instead of at the README.
  - "The doc taxonomy" was **deleted, not moved** — it duplicated
    [`CONVENTIONS.md`](./CONVENTIONS.md) § Layout, which already carries the same
    four-directory table.
  - README now runs: what it is → build → status pointer → a short "Working on
    this project" pointer → structure → license. It states no rules itself.
  - Same change upstream in `okf-project-template`, where the README keeps a short
    pitch that links to the rationale rather than hosting it, and `SETUP.md` now
    tells setup to delete both sections rather than keep them.
* **Doc-workflow consistency pass.** Three fixes to the docs describing the
  workflow itself:
  - `CLAUDE.md` — promoted "Before every commit checklist" from `###` to `##`.
    As a subsection it structurally swallowed the doc-role bullets and the "So:"
    rules that belong to § Planning hygiene, orphaning them from their intro
    sentence. Now a sibling section, still ahead of the hygiene rules.
  - `README.md` § "Why these rules" — added the missing rationale for the
    same-commit rule. The rule landed without one, which § Planning hygiene
    itself warns against ("a rule without its reason gets deleted the first time
    it's inconvenient").
  - `README.md` — completed the setup step that was skipped when this project was
    created: the file was still `okf-project-template`'s README verbatim (template
    title/intro, Quick Start, Manual setup, a dead `SETUP.md` link, 0BSD license
    line, template Structure block). Replaced with this project's identity, build
    instructions, real structure, and the Apache 2.0 license line. The doc taxonomy
    and "Why these rules" sections were kept, per `SETUP.md` § 2.
* The checklist and the same-commit rationale are being ported back upstream to
  `okf-project-template`; the README fix is local (the template's own README is
  correct as-is).

## 2026-07-23

* **Project initialized** from `okf-project-template`. Defined scope: convert
  Parrot telemetry from MP4 to MPEG-TS with MISB 0601 KLV. C++ with CMake,
  FetchContent for libmisbklv. License: Apache 2.0.
* **Domain types added**: `DataFormat` and `Standard` to the type vocabulary in
  CONVENTIONS.md.
* **Reference sources identified**: Parrot video metadata documentation (HTML),
  libvideo-metadata repo (GitHub, reference only), libmisbklv MISB specs
  (ST 0601.19, ST 0903.6, ST 1201.5).
* **Phase plan established**: Phase 0 (Foundation), Phase 1 (Parse), Phase 2
  (Transform), Phase 3 (Mux). First tasks: CMake setup, Parrot format research,
  field mapping documentation.
* **Parrot metadata formats researched**: Analyzed v1, v2, v3, and proto formats
  from documentation and libvideo-metadata source. Documented binary structures,
  fixed-point encoding, extension mechanism, and field categories. Created
  [[parrot-metadata-formats]] concept, [[parrot-video-metadata-docs]] and
  [[libvideo-metadata]] references.
* **Field mapping analysis completed**: Compared Parrot telemetry fields against
  ST 0601.19 tags in libmisbklv's registry. Identified 27 currently implemented
  tags, mapped Parrot fields to existing tags (Phase 1 viable with current impl),
  and documented high-priority missing tags (IMAPB extended items: 92-93, 103,
  106-108, 112-114) for Phase 2+. Created [[field-mapping-analysis]] with 3-phase
  implementation plan. Phase 0 Foundation tasks 1-3 complete.
* **Decision (accepted)**: ADR 0001 — Use direct MP4 box parsing for metadata
  extraction rather than GStreamer qtdemux. Investigation showed qtdemux doesn't
  expose metadata tracks as pads. Simple sequential parsing clearer for Phase 1.
  Will use lightweight library (minimp4) or custom minimal reader.
* **minimp4 vendored**: Added single-header MP4 parser (commit 4575afb, ~3.5K LOC,
  Unlicense/public domain) to `external/minimp4/`. Updated CMakeLists.txt to
  include vendored header. Standard practice for single-header libs (no
  build-time fetch, clear provenance via README).
* **Phase 1 started — MP4 extractor WIP**: Created `Mp4MetadataExtractor` class
  with minimp4 integration (pimpl pattern). Implements track finding, sample
  extraction API. Build integration with local libmisbklv via CMAKE_PREFIX_PATH.
  Status: Compiles with API corrections needed (minimp4 API differs from initial
  assumptions). Next: Fix API usage, test extraction with P0330036.MP4.
