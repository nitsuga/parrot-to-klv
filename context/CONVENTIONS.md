---
type: Conventions
title: Knowledge bundle conventions
description: How the context/ OKF bundle is structured, written, and maintained.
tags: [meta, okf, conventions]
generated:
  by: claude/opus-5
  at: 2026-07-23T18:30:00Z
---

# Purpose

`context/` is the agent-facing knowledge base for this project, in
[OKF](https://github.com/GoogleCloudPlatform/knowledge-catalog/blob/main/okf/SPEC.md)
v0.2. It is the synthesis layer between immutable sources and the working agent:
knowledge is compiled here once and kept current, not re-derived each session.
The agent writes and maintains this directory; humans curate sources and direct
the work. On disagreement, the OKF spec wins over this doc.

# Layout

| Path | Role |
|------|------|
| `../references/` | Raw sources, append-only. Agent reads, and deposits new snapshots on a directed ingest; never edits one that's already there. |
| `context/` | This bundle. Agent-owned synthesis. |
| `../docs/` | Human-facing authored guides (terse). Not agent-maintained. |
| `../planning/` | Project management (ROADMAP, PROGRESS). Transient; human + agent read/write. Not OKF concepts. |

# Frontmatter

Every concept `.md` carries YAML frontmatter. Required: `type` — the only key
OKF mandates. Recommended: `title`, `description`, `tags`, and `generated` (who
produced this, and when). Keep frontmatter minimal; OKF tolerates unknown keys
but they add noise.

```yaml
type: Concept
title: Short display name
description: One-sentence summary.
tags: [parrot, phase-2]
generated:
  by: claude/opus-5
  at: 2026-08-01T00:00:00Z
```

**Actors.** `generated.by` (and `verified[].by`) use the OKF actor convention:
`human:<id>` for people, `<producer>/<version>` for agents and tools (e.g.
`claude/opus-5`), `process:<id>` for automation. Trust tooling keys off the
`human:` prefix — use it only for genuine human authorship or review, never for
agent-written docs a human merely merged. Every doc in this bundle is
agent-written, so no `generated.by` here is ever `human:` — it names whichever
agent authored that doc (`claude/opus-5`, `claude/sonnet-5`, …). More than one
model works this repo, so don't assume a single actor; `AGENTS.md` § Agent
authorship covers how an agent names itself when it posts to a PR or issue.

**`generated.at` is the last *meaningful* content change** — the spec's words,
and consumers use it to tell a recent edit from a stale fact. So bump it when
you change what the doc *claims*: a revised conclusion, a new section, a fact
updated against its source. Don't bump it for a typo, a reflow, or a link
repair — an `at` that tracks keystrokes tells a consumer nothing, and one that
never moves makes current knowledge look abandoned. `verified` is independent:
content can change without re-confirmation, and re-confirmation isn't a
content change.

Optional families — add one only when it earns its keep:

| Field | Use for |
|------|------|
| `resource` | Canonical URI/path of the asset the concept describes. Already in wide use here: the standard PDF a `Standard` synthesizes, the local `references/` snapshot a `Reference` cites. |
| `sources` | Machine-readable inputs the concept derives from. Per entry `resource` is required; `id` when the body cites it; `title` optional. See § Citations and `sources`. |
| `verified` | List of `{by, at}` checks confirming the doc still matches its sources. |
| `status` | **OKF document lifecycle**: `draft` / `stable` (default) / `deprecated`. Not the ADR state — see § Decisions. |
| `stale_after` | `YYYY-MM-DD` after which the doc should be re-checked. |

**Migrating from OKF v0.1:** `timestamp` was replaced by `generated: {by, at}`,
and the body `# Citations` list by frontmatter `sources`. Consumers may fall
back to the legacy fields, so an un-migrated doc still reads — but write new
docs in the v0.2 spelling.

# Type vocabulary

Open, not closed — new types may emerge. OKF tolerates unknown types, so an
emerging type breaks nothing; still, prefer an existing type if it fits. A
domain-neutral starter set (rename / add for your project):

| Type | Use for |
|------|---------|
| `Reference` | Synthesis of an external source (a spec, a standard, a prior-art repo). Cites the immutable snapshot in `../references/`. |
| `Concept` | A piece of durable domain or design knowledge. |
| `Component` | A module / subsystem of the project. |
| `Decision` | Architecture decision record — the *why* of a resolved (or proposed) fork. |
| `DataFormat` | Specification of a data format (Parrot metadata v1/v2/v3/proto, MISB KLV, MPEG-TS). |
| `Standard` | A formal standard document (MISB ST 0601, ST 0903, etc.). |

OKF v0.2 also defines one type with spec-level semantics, `Attested Computation`
(a concept carrying `runtime` / `parameters` / `computation` / `executor` /
`attester`, so a value's meaning and its verifiable derivation live together).
This project doesn't compute attested values — ignore this type here.

# Decisions

A **fork** is a decision point — a branch in the design or plan that needs a
choice (the metaphor is a fork in the road; it has nothing to do with a git fork).
Forks are the unit the roadmap and the register track.

A `type: Decision` concept is an ADR: the *why* of a resolved (or proposed) fork
— context, alternatives considered, the choice, consequences. Carry a
`decision_status:` frontmatter field:

| `decision_status` | Meaning |
|---|---|
| `proposed` | Under deliberation; not yet adopted. |
| `accepted` | Resolved and in force. |
| `superseded` | Replaced by a later Decision (link it). |
| `deferred` | Parked; revisit later. |

**Why not `status:`?** OKF v0.2 claims `status` for *document* lifecycle
(`draft` / `stable` / `deprecated`) — a different axis from *decision* state: a
superseded ADR is a stable document you keep for the record (e.g. a future
`superseded` ADR here would still be `stable`, not `deprecated`). Since the spec
wins on disagreement (§ Purpose), the ADR field is `decision_status`, which OKF
tolerates as an unknown key. An ADR may still carry `status` in its OKF sense,
but rarely needs to.

Lifecycle: an open fork starts under **Open forks** in
[`../planning/ROADMAP.md`](../planning/ROADMAP.md) (`OPEN`). Once deliberated,
write the Decision in [`./decisions/`](./decisions/index.md) with
`decision_status: proposed`; on resolution set `accepted`, add its row to the
**decided register** [`./decisions/index.md`](./decisions/index.md) (fork # ↔
ADR ↔ status), and refresh present state in
[`../planning/PROGRESS.md`](../planning/PROGRESS.md). ROADMAP defers decided
forks to the register.

Record the decision in [`log.md`](./log.md) as a **single thin line** —
chronology + a link to the ADR. The ADR owns the rationale, so do **not** restate
the decision or its rejected alternatives in the log (that puts the same content
in three places). Add a *second* log line for one fork only on a genuine
cross-session gap (proposed in one session, accepted in a later one) or a
revision / supersede — **not** for a same-session propose→accept, which is one
event and gets one line.

## ADR format

- **Filename:** `NNNN-slug.md` — 4-digit zero-padded, hyphenated lowercase slug.
  Numbering is sequential by **creation order** (not the roadmap-fork number;
  they diverge). Monotonic; never reused (a superseded ADR keeps its number, the
  superseder takes the next).
- **Frontmatter:** `type: Decision`, `title`, `decision_status`, `tags` (include
  `decision`, a topic tag, optionally `phase-N`), `generated: {by, at}`, optional
  `sources:`, optional `fork:` (roadmap fork # for traceability).
- **Body:** `# Context` → `# Decision` → `# Alternatives considered` →
  `# Consequences` → `# Assumptions / open questions` → `# Citations`
  (specialized sections allowed between). Use `# Decision` always —
  `decision_status` carries proposed/accepted, so no header churn on accept.
- **Register:** [`./decisions/index.md`](./decisions/index.md) lists
  `Fork | ADR | Status`; updated on every status change.

# Citations and `sources`

v0.2 replaces the body `# Citations` list with frontmatter `sources`. Here, both
have a job, because they carry different things:

- **`sources:` (frontmatter)** — the machine-readable index of *external*
  inputs: a file under `../references/`, a standard's own repo at a pinned
  commit, a vendor's documentation URL. One entry per input, `resource`
  required; `id` when the body cites it; `title` optional and worth adding.
  This is what a consumer reads first.
- **`# Citations` (body)** — the annotated bibliography this bundle already
  writes: each entry says *why that source mattered to this doc*, not merely
  that it was consulted. Links to sibling concepts and ADRs (`[[slug]]` §
  Section — what it supplied) live here.

Keep the annotation. A bare `resource:` list records that a source was
consulted; the prose records what it decided — and that reasoning is the
reason to keep an ADR (or a `Reference`/`Standard` concept) at all. Purely
internal cross-references (other concepts, other ADRs) need no `sources`
entry; the body link is enough — most of this bundle's `# Citations` sections
are exactly that, and correctly stay bare.

**Give every cited source an `id`.** The spec makes `id` optional but says it
SHOULD be present when the body cites the source. The `id` is what makes
attribution machine-readable rather than a prose coincidence: without it, a
consumer can see *that* a doc has sources, but not which claim came from which
one. Keep the same `id` for the same source wherever it recurs —
`parrot-vmeta-html` and `libmisbklv-uas0601` each appear in more than one file
here, always spelled the same way, so a consumer following the join key lands
on the same source every time.

**Pin a specific claim with a footnote whose label is the `id`.** The label is
the join key into `sources`; a consumer resolves attribution through the
matching entry, not by reading the footnote text:

```markdown
---
sources:
  - id: libmisbklv-f8cc8c5
    resource: https://github.com/nitsuga/libmisbklv/commit/f8cc8c5cd94d6cda8a6648fcf73e9edc79d1df53
    title: "libmisbklv — Derive Time Status bits 6/5; refuse Generate on non-H.264"
---

Upstream then made `Generate` **H.264-only**.[^libmisbklv-f8cc8c5]

[^libmisbklv-f8cc8c5]: libmisbklv `f8cc8c5`, `src/gst/gst_backend.cpp` §
    `on_video_pad_added` — refuses `Sei0604::Generate` on any non-H.264 video pad.
```

Footnote the claims that would be *contested or checked* — a number, a quoted
rule, a constraint someone might dispute — not every sentence. Footnoting
everything restates the bibliography inline and is read as noise, which is how
attribution stops being read at all: most of this bundle's cited claims carry
no footnote at all, and that's correct — the `# Citations` annotation already
says why the source mattered. A label with no matching `sources` entry is a
dangling citation: the spec doesn't say what a consumer should do with one, so
don't produce one — [`link-check`](../.github/workflows/link-check.yml) fails
the build on it.

**Cite by stable anchor, not by position.** Name the section, clause, message, or
heading (`ST 0601 §6.3`, `vmeta.proto` § `TimedMetadata`) — never a line number
of a file under `../references/`, and never a page of an extract. Those files get
re-fetched and revised, and a positional citation then silently points at the
wrong text: worse than a broken link, because it still resolves and still looks
right. The same rule applies to a `sources:` entry that points into another
project's repository (e.g. libmisbklv, fetched at a pinned commit): cite the
commit or file the claim actually came from, not a line number that shifts the
next time that pin moves.

The [`link-check`](../.github/workflows/link-check.yml) CI fails on the
mechanical form of this — a backticked `path.ext:NN` anywhere outside
[[log]], whose dated entries are frozen snapshots. It cannot catch the subtler
case: a symbol anchor is only as stable as the symbol, so a function that moves
between files leaves a citation that still names it correctly and no longer says
where it is — which is exactly what happened to ADR 0007's `on_video_pad_added`
when libmisbklv split its backend. That one stays a read-through item
(§ Operations).

# Subdirectories

Types stay flat in `context/` by default; the root `index.md` groups by type.
Split a type into its own subdir when flat-listing grows noisy (~10+ files) or
develops sub-structure, and give the subdir its own `index.md`. `decisions/` is
pre-created because ADRs accumulate and supersede (a register is useful
immediately).

# Linking

Two forms, chosen by **where the target is** — not by a judgment about who might
read it:

- **Target inside `context/`** → **`[[slug]]`**. Resolved by name rather than
  path, so it survives a file moving or a type being split into its own subdir
  (see § Subdirectories — that split is expected, and it would break every
  relative link pointing into the moved set at once). GitHub renders it as
  literal text, which is the accepted cost inside an agent-facing bundle.
- **Target outside `context/`** → **`[Title](../planning/ROADMAP.md)`**,
  relative. Wikilinks cannot address outside the bundle, and these are the links
  a person is most likely to follow on GitHub.

The boundary is structural on purpose. It was previously "whatever a person is
likely to read on GitHub", which is a judgment call made afresh at every link —
so the boundary drifts, and two docs end up disagreeing about the same target.
A rule you can check by looking at the path is one a lint can check too.

One exception, in the other direction: a `context/` doc written primarily for
humans to read on GitHub may use markdown links throughout. Say so at the top of
that doc, so the deviation is visible rather than inferred.

Never root-absolute (`](/concept.md)`): it resolves against the *site* root, not
the bundle, so it works nowhere — not on GitHub, not in an editor, not for an
agent following paths.

Relationship semantics — references, nests-in, depends-on — live in the
surrounding prose; the link itself is untyped.

**Broken links are errors here.** OKF *consumers* must tolerate them — a
dangling link may be not-yet-written knowledge — but that's a robustness rule
for readers, not license for this bundle to ship them:
[`link-check`](../.github/workflows/link-check.yml) checks both forms above and
fails the build on one — the same guarantee that lets the wikilink form resolve
by name instead of path (see the boundary above). When a concept isn't written
yet, name it in prose without linking, or write the stub; the lint's "concepts
mentioned but not written" pass (§ Operations) is what turns those into links
later.

# Reserved files

`index.md` — directory listing for progressive disclosure. No frontmatter except
`okf_version: "0.2"` at the bundle root. `log.md` — chronological change history,
newest first, ISO `YYYY-MM-DD` headings. Both agent-maintained.

# Operations

**Ingest.** A new source (a spec, a prior-art repo) enters `../references/` or is
fetched. Read it, extract key facts, write a concept doc, update relevant
existing concepts and cross-links, add an `index.md` entry, append a `log.md`
line. One source may touch several docs.

**Query.** Read `index.md` first to find relevant concepts, drill in, synthesize
with citations back to `../references/` or external URLs. File valuable query
results back as new concepts so explorations compound.

**Lint.** Periodically check for: contradictions between docs, stale claims
superseded by newer sources, **forward-looking claims a later change resolved**
(an "open decision" / "candidate" / "Next" item that's since been decided or done
— the future-tense analog of a stale claim), orphan concepts with no inbound
links, concepts mentioned but not written, broken-link targets worth creating,
and type drift (synonyms / casing). No trigger fires the lint automatically — run
it periodically (e.g. when closing a fork, or before a release).

# Source discipline

`../references/` is **append-only**. Never edit, reformat, or delete a file
that's already there — the immutable sources are the ground truth, and this
bundle is the curated, cross-linked reading of them. Adding is different from
editing: when the user directs an ingest, deposit the new source there as a
faithful snapshot (§ Operations — Ingest), then leave it alone forever.
"Immutable" governs each snapshot, not the directory's file count.

Correcting a source you believe is wrong is *never* an edit there: the
correction is knowledge, so it belongs in a `context/` concept that cites the
original and says where it departs from it.
