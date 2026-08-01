# references/ — source-of-truth inputs, append-only

External inputs the project is built on: specs, standards, prior-art repos, and
their snapshots (an HTML doc export, a PDF, a `.txt` extract, a vendored doc).
Currently: the Parrot embedded-metadata spec, saved as an HTML page plus its
asset tree (`Embedded Video Metadata - 8.4.2.html` and `..._files/`). They are
the ground truth that `context/` synthesizes.

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
