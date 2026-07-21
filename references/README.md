# references/ — immutable source-of-truth inputs

External inputs the project is built on: specs, standards, prior-art repos,
datasets, PDFs, and their text extracts. **The agent reads these and never
modifies them** — they are the ground truth that `context/` synthesizes.

- Put the raw source here (a PDF, a `.txt`, a vendored doc, a link file).
- Cite it from `context/` concepts by name + section, not by extract line number
  (line numbers shift across revisions).
- Large binaries (PDFs, media) are good candidates for
  [git-lfs](https://git-lfs.com/) — track them in `.gitattributes`.

This README is a placeholder; keep it or delete it once real sources land.
