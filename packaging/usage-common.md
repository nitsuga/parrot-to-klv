## What it does

Reads the telemetry Parrot drones embed in an MP4 and writes an MPEG-TS carrying
two streams: the recording's own video, and a MISB ST 0601 KLV stream with one
packet per metadata sample, on the same timeline.

The video is **never decoded or re-encoded** — no generation loss. It is not
byte-identical either: for H.264 the timing NALs are rewritten so the video
carries ST 0604 Precision Time Stamps, letting a consumer correlate frames
without reading the KLV. Other codecs are carried through unstamped.

## Options

| | |
|---|---|
| `-o <path>` | write the MPEG-TS here. Without it, the tool only reads and reports. |
| `--klv` | print the ST 0601 mapping: every item emitted, everything omitted and why, and the Parrot fields that have no ST 0601 equivalent. |
| `--with-location` | include latitude/longitude in that report. |
| `--with-identity` | include the airframe serial in that report. |
| `--clamp-attitude` | see *Attitude* below. |

## Reports withhold sensitive values by default

A coverage report is the kind of thing that gets pasted into an issue, and
telemetry from a real flight carries the operator's position and the aircraft's
identity. Both are withheld unless asked for, and shown as `withheld` rather
than dropped, so you can tell the difference between "not disclosed" and "not
present".

## Attitude

ST 0601 gives Items 6 and 7 (Platform Pitch/Roll) ranges of ±20° and ±50°, which
a quadcopter exceeds in ordinary flight. By default the tool emits the
spec-mandated `0x8000` "out of range" value. Some consumers treat that as a
parse failure; `--clamp-attitude` emits the spec limit instead.

**Items 90 and 91 always carry the true full-range attitude**, whichever mode is
in use. If you want the real number, read those. There is no option to put the
true angle into Items 6/7 — ST 0601 maps ±limit onto the whole 16-bit range, so
a larger value has no encoding there at all.

## What is not converted

Items needing a boresight-to-terrain intersection — frame centre, corner points,
slant range — are reported as omitted rather than guessed at. So are a handful of
open mapping questions. `--klv` names every one of them, with the reason.

## Supported input

Parrot's **v3** and **proto** metadata formats: the ANAFI range and ANAFI Ai.
The older v2 format is not converted. A file with no Parrot metadata track is
rejected with that as the reason.
