# okf-project-template

A starter template for **agent-driven projects** that keeps project knowledge,
decisions, and plan in a small set of Markdown docs an AI agent maintains — and,
crucially, *instructions that tell the agent how to work* so those docs don't rot.

It's built on the
[OKF](https://github.com/GoogleCloudPlatform/knowledge-catalog/blob/main/okf/SPEC.md)
knowledge-catalog format (that's the `context/` bundle) and adds a
planning/decisions/working-hygiene layer around it. It is **not** an official OKF
artifact — OKF is one of the four parts.

## Quick Start

**1. Make your own copy.** On this repo's GitHub page, click **"Use this
template" → Create a new repository** (or
`gh repo create my-project --template <owner>/okf-project-template --private`).
Then clone your new repo locally.

**2. Customize it — let an agent walk you through it (recommended).** Open the new
project in your coding agent (e.g. Claude Code) and paste this:

> I just created this project from **okf-project-template** and it still has the
> template's placeholders. Please run the setup dialog in `SETUP.md`: ask me what
> you need to know, fill in the project's docs (name, scope, preferences, first
> tasks), and remove the example/placeholder content when we're done.

The agent runs a short Q&A, fills in the docs, deletes the example artifacts, and
removes the onboarding scaffolding — leaving a clean, customized project. Prefer
to do it by hand? Follow [Manual setup](#manual-setup) below instead.

**3. Start working.** Your project now has a knowledge bundle (`context/`), a
decision register (ADRs), a living plan (`planning/`), and agent working rules
(`CLAUDE.md`) — all wired together.

## The doc taxonomy

Four top-level doc directories, each with one job:

| Dir | Role | Audience |
|-----|------|----------|
| `references/` | **Immutable** source-of-truth inputs (specs, standards, prior art). Read, never modify. | inputs |
| `context/` | **Agent-owned** knowledge bundle (OKF): synthesized concepts + the decision register (ADRs). | agent |
| `planning/` | **Volatile** plan + present state — `ROADMAP.md` (scope/phases/open forks) and `PROGRESS.md` (Now/Next). | both |
| `docs/` | **Human-facing** authored guides (terse). | humans |

Plus [`CLAUDE.md`](CLAUDE.md) at the root — the *how-to-work* instructions an
agent auto-loads: the doc roles above and the planning-hygiene rules below.

## Manual setup

The by-hand alternative to the agent dialog (Quick Start step 2) — the same
result, done yourself:

1. Copy/clone this tree into your new project (or use it as a GitHub template).
2. Fill in [`CLAUDE.md`](CLAUDE.md): the project description (top) and the
   **Project preferences** section (commit policy, repo layout, build/test/run).
3. Set the real `timestamp` in `context/CONVENTIONS.md` frontmatter; trim the
   `type` vocabulary to your domain.
4. Delete (or replace) the **example artifacts**, each marked with an
   `[EXAMPLE — replace or delete]` note:
   - `context/decisions/0001-example-decision.md` (a worked ADR)
   - the example entry in `context/log.md`
   - the example rows/sections in `context/index.md`,
     `context/decisions/index.md`, `planning/ROADMAP.md`, `planning/PROGRESS.md`
5. Start working. On the first real decision, write ADR `0001`.

To find every spot that needs your attention (and confirm you got them all when
done), grep for the markers:
`grep -rnE '<PROJECT>|<[a-z][^>]*>|\[EXAMPLE|<!--' --include='*.md' .`

**Optional CI:** a GitHub Action that fails on broken internal doc links ships
**disabled**. Enable it any time with
`git mv .github/workflows/link-check.yml.disabled .github/workflows/link-check.yml`.

## Why these rules

The hygiene rules in `CLAUDE.md` look fussy until you've watched docs rot. Each
earned its place by a failure mode:

- **One job per doc.** When two docs narrate the same thing, one copy drifts and
  you can't tell which is right. So: history lives only in `log.md`, the decided
  register only in `decisions/index.md`, present state only in `PROGRESS.md`.
- **History → `log.md`, present-state → `PROGRESS.md`.** A curated knowledge base
  must not carry volatile session history, and a status doc must not accrete a
  "Done" pile. Keeping them separate keeps each trustworthy.
- **No live tallies.** A count ("19 tests", "forks 1–15") is wrong the moment the
  next change lands, and nobody remembers to update prose numbers. Describe the
  *state* ("all tests green", "none open — see the register"); a number is only
  ever a frozen snapshot in a dated `log.md` line.
- **Closing scrubs the future / one home for open work.** Forward-looking content
  drifts exactly like history: resolve an item but leave it listed under "Next" /
  "candidate" elsewhere, and the docs now lie. So a candidate lives in *one* place
  (ROADMAP's backlog), and resolving it means deleting its forward-looking
  mentions too — not just updating the present-state bullet.
- **Lint is the backstop.** Nothing fires it automatically, so run it periodically
  (closing a fork, before a release). But the real lever is reducing what *can*
  drift (the rules above), so the lint has little to catch.

The meta-lesson: **duplication rots — in both tenses.** Most of these rules are
one idea (don't say the same thing in two places) applied to the past (history,
decisions) and the future (open/candidate work).

## Structure

```
CLAUDE.md              how-to-work instructions (agent auto-loads)
SETUP.md               one-time setup dialog (agent-run; self-deletes after)
.github/workflows/
  link-check.yml.disabled   optional CI (rename to enable — fails on broken links)
context/               agent-owned OKF knowledge bundle
  index.md             bundle catalog (read first)
  CONVENTIONS.md       frontmatter, types, ADR format, ingest/query/lint
  log.md               durable chronological record
  decisions/
    index.md           decided register: fork ↔ ADR ↔ status
    0001-*.md          ADRs (one example included)
planning/
  ROADMAP.md           scope, phases, open + candidate forks
  PROGRESS.md          present state: Now / In-progress / Next
references/            immutable inputs (read-only)
docs/                  human-facing guides
```

## License

[0BSD](LICENSE) (Zero-Clause BSD) — use, copy, and modify this template freely,
**no attribution required**. Projects you generate from it are yours to license
however you like (keep 0BSD or swap in your own `LICENSE`).
