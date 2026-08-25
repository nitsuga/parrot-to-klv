# Parrot Embedded Video Metadata — link file

A **link file**, not a snapshot: the source it points at is Parrot's copyrighted
documentation, which this project has no license to redistribute. The verbatim
HTML export that used to sit here was withdrawn — see
[ADR 0016](../context/decisions/0016-references-licensing-withdrawal.md) for the
reasoning and for what the withdrawal cost.

## The documentation

- **URL:** <https://developer.parrot.com/docs/groundsdk-tools/video-metadata.html>
- **Version observed:** 8.4.2
- **Date observed:** 2026-08-24
- **Terms:** "Copyright © 2018-2026, Parrot" in the page footer. No license grant,
  no redistribution permission.

**The URL is unversioned.** It serves whatever release is current — 8.4.2 today,
8.4.0 as recently as this project's search-engine snapshots, and GroundSDK Tools
7.0 lived at a different path entirely (`/docs/pdraw/video-metadata.html`). So
this link is *not* a stable citation target the way a snapshot here would be.
Cite the pinned sources below instead wherever the claim allows it, and cite this
page by section name only when the claim exists nowhere else.

## The pinned, redistributable sources

Parrot's reference implementation is BSD-3-Clause
([`COPYING`](https://github.com/Parrot-Developers/libvideo-metadata/blob/22f715adbb52ad14693b3b98fa537f3dbef53409/COPYING),
`.MODULE_LICENSE_BSD`, plus an in-file notice at the head of each source file).
It carries the substance the documentation renders, and a commit-pinned URL is
immutable in the way this directory's append-only rule wants.

Pinned at commit
[`22f715adbb52ad14693b3b98fa537f3dbef53409`](https://github.com/Parrot-Developers/libvideo-metadata/tree/22f715adbb52ad14693b3b98fa537f3dbef53409)
(2026-04-14):

| Claim | Pinned source |
|---|---|
| `vmeta.proto` schema — messages, field numbers, wire types | [`proto/vmeta.proto`](https://github.com/Parrot-Developers/libvideo-metadata/blob/22f715adbb52ad14693b3b98fa537f3dbef53409/proto/vmeta.proto) — also vendored here as `vmeta.proto` |
| v3 binary layout | [`include/video-metadata/vmeta_frame_v3.h`](https://github.com/Parrot-Developers/libvideo-metadata/blob/22f715adbb52ad14693b3b98fa537f3dbef53409/include/video-metadata/vmeta_frame_v3.h) |
| v2 binary layout | [`include/video-metadata/vmeta_frame_v2.h`](https://github.com/Parrot-Developers/libvideo-metadata/blob/22f715adbb52ad14693b3b98fa537f3dbef53409/include/video-metadata/vmeta_frame_v2.h) |
| Common types — location, quaternion, euler | [`include/video-metadata/vmeta.h`](https://github.com/Parrot-Developers/libvideo-metadata/blob/22f715adbb52ad14693b3b98fa537f3dbef53409/include/video-metadata/vmeta.h) |
| RTP header extension carriage | [`src/vmeta_frame.c`](https://github.com/Parrot-Developers/libvideo-metadata/blob/22f715adbb52ad14693b3b98fa537f3dbef53409/src/vmeta_frame.c) |

## `vmeta.proto` — vendored here

`references/vmeta.proto` is a faithful copy of `proto/vmeta.proto` at the commit
above, its BSD-3-Clause notice intact at the head of the file, as that license
requires. It is a normal immutable snapshot: never edited, never restyled.

It is **not** the same file as `test/vmeta_subset.proto`, which is a hand
transcription of the ~13 messages the ST 0601 mapping draws on, written so a
protobuf toolchain can read what our hand-written wire parser implements. That
transcription now has a local original to be checked against rather than an
external URL — see [ADR 0003](../context/decisions/0003-protobuf-dependency.md).

## What this link file does not carry

The documentation's **prose and tables**: fixed-point encodings, field semantics,
the extension mechanism narrative, and the "Integration in the RTP stream"
section. Headers give struct layouts and the proto gives field numbers; the
explanatory text is prose they only imply. This project's reading of it survives
in [`../context/parrot-metadata-formats.md`](../context/parrot-metadata-formats.md),
which is this project's own writing and stays.
