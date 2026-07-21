---
type: Conventions
title: Knowledge bundle conventions
description: How the context/ OKF bundle is structured, written, and maintained.
tags: [meta, okf, conventions]
timestamp: 2000-01-01T00:00:00Z
---

# Purpose

`context/` is the agent-facing knowledge base for this project, in
[OKF](https://github.com/GoogleCloudPlatform/knowledge-catalog/blob/main/okf/SPEC.md)
v0.1. It is the synthesis layer between immutable sources and the working agent:
knowledge is compiled here once and kept current, not re-derived each session.
The agent writes and maintains this directory; humans curate sources and direct
the work. On disagreement, the OKF spec wins over this doc.

# Layout

| Path | Role |
|------|------|
| `../references/` | Immutable raw sources. Agent reads, never modifies. |
| `context/` | This bundle. Agent-owned synthesis. |
| `../docs/` | Human-facing authored guides (terse). Not agent-maintained. |
| `../planning/` | Project management (ROADMAP, PROGRESS). Transient; human + agent read/write. Not OKF concepts. |

# Frontmatter

Every concept `.md` carries YAML frontmatter. Required: `type`. Recommended:
`title`, `description`, `tags`, `timestamp` (ISO 8601). Keep frontmatter minimal;
OKF tolerates unknown keys but they add noise.

# Type vocabulary

Open, not closed — new types may emerge. OKF tolerates unknown types, so an
emerging type breaks nothing; still, prefer an existing type if it fits. A
domain-neutral starter set (rename / add for your project):

| Type | Use for |
|------|---------|
| `Reference` | Synthesis of an external source (a spec, a standard, a prior-art repo). Cites the immutable original in `../references/`. |
| `Concept` | A piece of durable domain or design knowledge. |
| `Component` | A module / subsystem of the project. |
| `Decision` | Architecture decision record — the *why* of a resolved (or proposed) fork. |

<!-- e.g. a data project might add `Dataset`; a standards-heavy one `Standard
     Reference`. Delete types you won't use. -->

# Decisions

A **fork** is a decision point — a branch in the design or plan that needs a
choice (the metaphor is a fork in the road; it has nothing to do with a git fork).
Forks are the unit the roadmap and the register track.

A `type: Decision` concept is an ADR: the *why* of a resolved (or proposed) fork
— context, alternatives considered, the choice, consequences. Carry a `status:`
frontmatter field:

| `status` | Meaning |
|---|---|
| `proposed` | Under deliberation; not yet adopted. |
| `accepted` | Resolved and in force. |
| `superseded` | Replaced by a later Decision (link it). |
| `deferred` | Parked; revisit later. |

Lifecycle: an open fork starts under **Open forks** in
[`../planning/ROADMAP.md`](../planning/ROADMAP.md) (`OPEN`). Once deliberated,
write the Decision in [`./decisions/`](./decisions/index.md) with
`status: proposed`; on resolution set `accepted`, add its row to the **decided
register** [`./decisions/index.md`](./decisions/index.md) (fork # ↔ ADR ↔
status), and refresh present state in
[`../planning/PROGRESS.md`](../planning/PROGRESS.md). ROADMAP defers decided
forks to the register.

Record the decision in [`log.md`](./log.md) as a **single thin line** —
chronology + a link to the ADR. The ADR owns the rationale, so do **not** restate
the decision or its alternatives in the log (that puts the same content in three
places). Add a second log line for a fork only on a genuine cross-session gap or
a revision / supersede.

## ADR format

- **Filename:** `NNNN-slug.md` — 4-digit zero-padded, hyphenated lowercase slug.
  Numbering is sequential by **creation order** (not the roadmap-fork number;
  they diverge). Monotonic; never reused (a superseded ADR keeps its number, the
  superseder takes the next).
- **Frontmatter:** `type: Decision`, `title`, `status`, `tags` (include
  `decision`, a topic tag, optionally `phase-N`), `timestamp` (ISO 8601),
  optional `fork:` (roadmap fork # for traceability).
- **Body:** `# Context` → `# Decision` → `# Alternatives considered` →
  `# Consequences` → `# Assumptions / open questions` → `# Citations`
  (specialized sections allowed between). Use `# Decision` always — `status`
  carries proposed/accepted, so no header churn on accept.
- **Register:** [`./decisions/index.md`](./decisions/index.md) lists
  `Fork | ADR | Status`; updated on every status change.

# Subdirectories

Types stay flat in `context/` by default; the root `index.md` groups by type.
Split a type into its own subdir when flat-listing grows noisy (~10+ files) or
develops sub-structure, and give the subdir its own `index.md`. `decisions/` is
pre-created because ADRs accumulate and supersede (a register is useful
immediately).

# Linking

Prefer bundle-relative links resolved from `context/`: `[Title](/concept.md)` or
`[Title](./concept.md)`. Relationship semantics — references, nests-in,
depends-on — live in the surrounding prose; the link itself is untyped. Broken
links are not errors; they may be not-yet-written knowledge.

# Reserved files

`index.md` — directory listing for progressive disclosure. No frontmatter except
`okf_version` at the bundle root. `log.md` — chronological change history,
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

Never edit files in `../references/`. The immutable sources are the ground truth;
this bundle is the curated, cross-linked reading of them.
