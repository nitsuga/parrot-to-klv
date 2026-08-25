# references/ — source-of-truth inputs, append-only

External inputs the project is built on: specs, standards, prior-art repos, and
their snapshots (an HTML doc export, a PDF, a `.txt` extract, a vendored doc, a
link file). They are the ground truth that `context/` synthesizes.

Currently:

- `vmeta.proto` — Parrot's protobuf schema for the proto metadata format,
  vendored from `Parrot-Developers/libvideo-metadata` at a pinned commit under
  BSD-3-Clause, its notice intact at the head of the file.
- `parrot-video-metadata.md` — a **link file** for Parrot's embedded-metadata
  documentation, which is copyrighted and not redistributable. It records the
  URL, the version and date observed, and the commit-pinned BSD-3-Clause
  sources to cite in its place.

**Append-only, not read-only.** A directed ingest may *add* a new source here;
nothing already here is ever edited, reformatted, or deleted. Each snapshot is
immutable — the directory still grows.

- Put the raw source here (an HTML export, a PDF, a `.txt`, a vendored doc, a
  link file), exactly as it came. If it needs correcting or interpreting,
  that's a `context/` concept citing it, not an edit here.
- Cite it from `context/` concepts by name + section, not by extract line
  number (line numbers and anchors shift across revisions).
- Large binaries (PDFs, media) are good candidates for
  [git-lfs](https://git-lfs.com/) — track them in `.gitattributes`.

**Prefer a source you may redistribute.** Where a vendor publishes both
documentation and a reference implementation, the implementation is usually the
better snapshot: permissively licensed, and a commit pin is immutable in exactly
the way this directory needs. Check the terms *before* depositing, not after.

## Withdrawal — the one exception to append-only

A snapshot may be **removed** when keeping it would redistribute content this
project has no license to redistribute. That is the sole exception, and it is
not a license to prune: it exists because this repository is intended to be
public, and an immutable copy of someone else's copyrighted work is not ours to
publish.

A withdrawal is never a silent delete. It requires, in the same commit:

1. A **link file** in its place recording what the source was, where it lives,
   the version and date observed, and its terms — so every citation still
   resolves to something.
2. **Pinned replacements** for whatever claims the withdrawn source carried,
   wherever a redistributable equivalent exists.
3. An **ADR** recording what was withdrawn, why, and what the project lost —
   because a withdrawal destroys ground truth, and that cost belongs on the
   record rather than in a commit message.

The first withdrawal is
[ADR 0016](../context/decisions/0016-references-licensing-withdrawal.md).
