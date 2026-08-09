# Changelog

What changed for someone *using* the tool. Written by hand, because the commit
list is not a changelog — it carries reverts, refactors and internal churn that
tell a user nothing.

The release workflow publishes the section matching the tag, so a heading here
must be exactly `## vX.Y.Z`.

## v0.1.4

### Changed

- **Fresh source and CI builds fetch libmisbklv without its purged historical
  data corpus.** The dependency pin now names the latest upstream revision, so
  clean builds download substantially less unused data and start faster. The
  shipped converter's runtime behavior is unchanged.

## v0.1.3

### Fixed

- **Conversion and scrubbing now refuse an output that is the input file.** The
  check recognizes ordinary spelling differences, `.` / `..`, symlinks, and
  hard links, and fails before touching the output. This prevents conversion
  from truncating its source and prevents an accidental in-place scrub.

  **If you script either tool, give `-o` a distinct destination.** A workflow
  that intentionally reused the input pathname, or an alias to it, now exits
  non-zero instead of modifying or destroying the source.

- **Invalid telemetry no longer becomes a plausible measurement.** Non-finite
  source values are omitted as invalid; finite values outside an ST 0601 item's
  encodable range are omitted rather than silently saturated. In particular, an
  out-of-range ground speed no longer becomes a false 255 m/s measurement, and
  malformed airspeed and satellite counts are not clamped into valid-looking
  values. Course over ground at rest is reported as undefined.

  The `--klv` coverage report distinguishes these cases from a field that was
  simply absent, using `source value is invalid`, `value is not encodable as
  this item`, or `undefined for this frame` as appropriate.

- **Media-backend failures retain their specific reason.** Type mismatches and
  resource-limit failures from the updated libmisbklv dependency no longer fall
  through to the generic `unknown error` text.

### Packaging

- **The release packaging scripts refuse unsafe staging directories before
  clearing them.** This affects people who run `packaging/make-bundle.sh` or
  `packaging/make-image.sh` themselves: use a dedicated child under `staging/`
  or a nested temporary directory, never the checkout, an input, your home
  directory, or a broad system directory.

## v0.1.2

### Added

- **`--infer-image-source`** fills ST 0601 **Item 11 (Image Source Sensor)** for
  recordings whose format states no imaging spectrum — which is every v3
  recording, so the whole ANAFI range. It names the sensor `IR` when the frame
  carried thermal metadata and `EO` when it did not.

  This is a **guess from a proxy, not a converted field**, so it is off by
  default, it never overrides a spectrum the recording actually states, and the
  `--klv` coverage report marks the value `(inferred)`. Two things it gets
  wrong and cannot do better on: blended EO/IR footage is named `IR`, and a
  thermal recording that wrote no thermal metadata would be named `EO`. No
  subtype is guessed — the value is always a bare `EO` or `IR`, never `EO Wide`.

  Without the flag, Item 11 is omitted exactly as before and the report says the
  format does not carry it.

- **`--version`** and **`-h` / `--help`**. `--help` previously fell through and
  was treated as a filename.

### Changed

- **Unknown options are now refused instead of ignored.** A mistyped flag —
  `--klvv`, `--with-locations` — used to be silently dropped: the tool ran to
  completion, exited 0, and produced output missing whatever you asked for. It
  now prints the offending option and exits non-zero. Likewise `-o` with no
  path, and passing two input files.

  **If you script this tool, this is the change to check.** A pipeline that has
  been quietly passing a typo will now fail instead of silently doing the wrong
  thing.

- Spelling normalized to American English throughout, which touches two strings
  you might match on: the error `Unrecognised Parrot MIME type` is now
  `Unrecognized …`, and `parrot-conform --help` says `analyze` rather than
  `analyse`.

### Known limitations

- **In the output MPEG-TS the KLV is stream `0:0` and the video is `0:1`.**
  Select streams by *type*, not index — `ffmpeg -map 0:v`, not `-map 0:0`. The
  muxer orders the PMT by internal pad order, and reversing it was tried and
  reverted because every approach cost either the KLV stream or ST 0604
  timestamp accuracy. PIDs and stream types are correct either way; only the
  announcement order is surprising.

- The `IR` half of `--infer-image-source` has never been exercised against a
  real thermal recording. The `EO` half is covered by fixtures and real flights.

## v0.1.1

- The `--klv` coverage report names ST 0601 items instead of numbering them
  (`Item 11  Image Source Sensor`), with names and units taken from the ST 0601
  registry, one item per line.
- A container image as a second deliverable, built from the same bundle, with a
  launcher that mounts only the directories in play and runs as the calling
  user.

## v0.1.0

First release. Converts Parrot v3 and proto metadata from an MP4 into an
MPEG-TS carrying the recording's own video plus a MISB ST 0601 KLV stream, with
a coverage report naming everything that could not be carried. Shipped as a
relocatable bundle that runs with nothing installed.
