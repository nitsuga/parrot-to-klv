---
type: Decision
title: ADR 0016 — Withdraw the Parrot documentation snapshot; cite the BSD-3 reference implementation instead
description: The vendored HTML export of Parrot's embedded-metadata documentation is copyrighted and not redistributable, so it is withdrawn from references/ and replaced by a link file plus commit-pinned citations into Parrot's BSD-3-Clause reference implementation
decision_status: accepted
fork: 14
tags: [decision, licensing, references, documentation, packaging]
generated:
  by: claude/opus-5
  at: 2026-08-24T18:00:00Z
sources:
  - id: parrot-vmeta-doc
    resource: https://developer.parrot.com/docs/groundsdk-tools/video-metadata.html
    title: Parrot — Embedded Video Metadata 8.4.2 (copyright © 2018-2026, Parrot; no redistribution grant)
  - id: libvideo-metadata-pin
    resource: https://github.com/Parrot-Developers/libvideo-metadata/tree/22f715adbb52ad14693b3b98fa537f3dbef53409
    title: Parrot-Developers/libvideo-metadata at 22f715a — BSD-3-Clause reference implementation
  - id: libvideo-metadata-copying
    resource: https://github.com/Parrot-Developers/libvideo-metadata/blob/22f715adbb52ad14693b3b98fa537f3dbef53409/COPYING
    title: libvideo-metadata COPYING — BSD 3-Clause, copyright (c) 2016 Parrot Drones SAS
---

# Context

This repository is intended to be made public, in order to reach GitHub's free
arm64 hosted runners and unmetered Actions minutes for the multi-architecture
release work, and to drop the private-dependency deploy key that both CI
workflows currently carry.

An audit ahead of that flip found the history clean — no secrets, no footage,
every committed `.mp4` synthetic. It found one blocker, in `references/`.

`references/Embedded Video Metadata - 8.4.2.html` and its `_files/` asset tree
were a verbatim browser export of Parrot's developer documentation: 16 tracked
files, 936 KB. The live page footer asserts **"Copyright © 2018-2026, Parrot"**
and grants no license and no redistribution permission.

The asset tree mattered more than the prose. It carried `logo-light-mode.svg`
and `logo-dark-mode.svg` — Parrot's trademarked logos, where the exposure is
implied affiliation or endorsement rather than copyright — and `hud.jpg`, a
516 KB product screenshot that was over half the directory. The remainder
(`furo.css`, `pygments.css`, `clipboard.min.js`, `doctools.js`,
`sphinx_highlight.js`, `copybutton.*`) was MIT/BSD Sphinx theme scaffolding:
safe to publish, and worth nothing to this project.

Held privately, this was ordinary internal reference material. Published, it is
redistribution of a third party's copyrighted documentation and marks from an
Apache-2.0 repository.

# Decision

**Withdraw the snapshot. Replace it with a link file plus commit-pinned
citations into Parrot's BSD-3-Clause reference implementation, and vendor the
one artifact that license permits.**

Concretely:

- `references/Embedded Video Metadata - 8.4.2.html` and `_files/` are deleted.
- `references/parrot-video-metadata.md` is a **link file** recording the URL,
  the version observed (8.4.2), the date observed (2026-08-24), the terms, and
  the pinned replacements — so every existing citation still resolves.
- `references/vmeta.proto` is `proto/vmeta.proto` from
  [`Parrot-Developers/libvideo-metadata`](https://github.com/Parrot-Developers/libvideo-metadata)
  at commit `22f715adbb52ad14693b3b98fa537f3dbef53409`, vendored with its
  BSD-3-Clause notice intact at the head of the file, as that license requires.
- Claims that came from the documentation now cite the pinned implementation:
  the proto schema from `proto/vmeta.proto`, the v3 and v2 binary layouts from
  `include/video-metadata/vmeta_frame_v3.h` and `vmeta_frame_v2.h`, the common
  types from `vmeta.h`, and RTP header-extension carriage from
  `src/vmeta_frame.c`.

**A bare link would have been the wrong replacement.** The documentation URL is
unversioned — it serves whatever release is current, was on 8.4.0 within this
project's lifetime, and GroundSDK Tools 7.0 lived at a different path entirely
(`/docs/pdraw/video-metadata.html`). Pointing at it alone would have discarded
the version pinning `references/` exists to provide. A commit SHA does not move.

**The implementation is also the better source.** The documentation *renders*
the schema; the repository *is* it. `context/libvideo-metadata.md` had cited that
repository as a `type: Reference` concept since the project's first week — it was
simply never made primary.

This required amending the append-only rule, which forbade deletion outright.
`context/CONVENTIONS.md` § Source discipline, `references/README.md`, and
`AGENTS.md` now carry a single narrow **withdrawal** exception, conditioned on a
link file, pinned replacements, and this ADR. Amending the rule in the same
commit as the deletion is deliberate: the alternative is a repository that
violates its own stated invariant and explains itself only in a commit message.

# Alternatives considered

**Keep the snapshot and publish anyway.** Rejected. The prose alone might have
been arguable as fair use for interoperability work; the logos are not, and
trademark exposure is not the kind of risk to take for a convenience copy.

**Keep the repository private.** Rejected as a solution to *this* problem,
though it remains the status quo until the flip. It would leave the arm64 work
dependent on QEMU emulation or paid runners, and it does not make the copy any
more licensed — it only makes it unpublished.

**Strip the asset tree, keep the HTML prose.** Rejected. It removes the sharpest
exposure (logos, screenshot) while leaving the actual copyrighted work in place,
and it would mean editing a snapshot, which the append-only rule forbids for
better reasons than this ADR overrides.

**Replace with a bare link file, no vendoring.** Rejected as insufficient rather
than wrong: it resolves the licensing problem but discards version pinning, and
it leaves ADR 0003's reasoning — which depends on the schema being locally
present — resting on a URL that will not keep serving 8.4.2.

**Re-derive the formats from our own parsers.** Rejected as circular. The
parsers were written *from* this source; citing them back would launder the
provenance and leave no external ground truth at all.

# Consequences

- The default checkout no longer contains the withdrawn material: nothing in a
  working tree at `main` carries Parrot's copyrighted documentation or its marks.
  **Full clones still receive it through reachable history** — a normal clone
  fetches the entire reachable ancestry, and the HTML, both logo SVGs, and
  `hud.jpg` are still listed by `git rev-list --objects` from this branch's head.
- **Publication remains blocked pending history remediation.** Deleting a file at
  the tip does not remove it from history. Those blobs remain readable from every
  commit ancestral to `main` and from all eleven release tags (`v0.1.0`–`v0.2.3`),
  and from the 45 `refs/pull/*` refs, so changing visibility would publish them
  exactly as before. Clearing them requires rewriting every reachable branch and
  tag and asking GitHub Support to purge cached objects and `refs/pull/*`, or
  publishing a fresh clean-history repository and keeping this one private.
- What this ADR settles is the **source policy** — the decision to withdraw, what
  replaces it, and the amended append-only rule — and it cleans the default tree.
  Redistribution by the repository is only **partially remediated** until history
  is purged; that operation is tracked separately.
- The arm64 release work does **not** depend on either. GitHub shipped arm64
  standard runners for private repositories in January 2026, free-tier eligible,
  so multi-architecture builds need no visibility change — see ROADMAP fork 15.
- `references/` shrinks by 936 KB and 16 files, and gains 1184 lines of schema
  that is actually load-bearing.
- **The transcription was verified against a real original for the first time.**
  [[0003-protobuf-dependency]] recorded that `test/vmeta_subset.proto` "has NOT
  been diffed against Parrot's SDK repository", because diffing it meant fetching
  something the project did not hold. With `references/vmeta.proto` present, that
  diff is local: all 19 transcribed messages and enums exist upstream and every
  field number agrees. The vendored schema also carries 53 messages and enums,
  matching the 35 messages + 18 enums ADR 0003 counted in the 8.4.2 export — so
  the pinned commit has not drifted from the version this project was written
  against. The withdrawal therefore *strengthened* the weakest link in ADR 0003's
  validation story.
- **ADR 0003's claim is preserved, by a different file.** It argued in part that
  "the schema is already in `references/`, not just cited by it" — true of the
  withdrawn HTML, and true again of `references/vmeta.proto`. The transcription
  in `test/vmeta_subset.proto` now has a local original to be checked against
  rather than an external URL, which makes its drift-check stronger than before.
- **Prose and tables are genuinely lost.** The fixed-point encodings, field
  semantics, the extension-mechanism narrative, and the "Integration in the RTP
  stream" section that [[0013-live-streaming-ingest]] cites existed as
  explanatory text. Headers give struct layouts and the proto gives field
  numbers; neither explains. This project's reading survives in
  [[parrot-metadata-formats]], which is our own writing — but that reading can no
  longer be re-checked against the original offline, only against the live URL
  at whatever version it then serves.
- A future re-check of [[parrot-metadata-formats]] against 8.5 becomes a
  deliberate online act rather than a local diff. The link file records the
  version this project's reading was taken from so that check knows its baseline.
- The append-only rule is weaker than it was. It is now conditioned rather than
  absolute, and a conditioned rule invites argument at the margin. The three
  requirements are what keep the exception narrow; a withdrawal that skips any
  of them should be refused.

# Assumptions / open questions

- **Not legal advice.** This is a practical risk assessment by an agent, not
  counsel. The judgment that a verbatim doc export plus vendor logos is not
  publishable is conservative by intent.
- BSD-3-Clause permits redistribution with the notice retained, which
  `references/vmeta.proto` carries in-file. GitHub's license detector reports
  `NOASSERTION` for that repository because the license sits in `COPYING` and
  `.MODULE_LICENSE_BSD` rather than a filename it recognizes; the text in both
  is the standard 3-clause form, read at the pinned commit.
- The pinned commit was `master` HEAD on 2026-04-14. Nothing checks that the pin
  still resolves; a link-check run would catch it going away, not going stale.
- Whether the withdrawn prose is needed again is untested. If a future mapping
  question can only be answered from the documentation's narrative, the answer is
  to fetch it, cite it by section from the link file, and write the reading into
  `context/` — not to re-deposit the export.

# Citations

- [[0003-protobuf-dependency]] § Four facts — argued from the schema being
  present in `references/`; that claim now rests on `references/vmeta.proto`.
- [[0013-live-streaming-ingest]] — cited the documentation's "Integration in the
  RTP stream" section, one of the passages the withdrawal loses.
- [[parrot-metadata-formats]] — this project's synthesis of the withdrawn source,
  and the reason the loss is survivable.
- [[libvideo-metadata]] — the reference-implementation concept that had cited the
  BSD-3 repository all along, now the primary source.
- `libvideo-metadata` `COPYING` at the pinned commit — the BSD-3-Clause grant
  this decision relies on.
