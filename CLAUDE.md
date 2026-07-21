# <PROJECT>

<!-- Replace this block with a 2–4 line description of YOUR project: what it is,
     its scope, and any hard constraints. This is the first thing an agent reads;
     keep it terse. -->

One-line description of the project goes here.

> **This repo uses an OKF-based agent documentation workflow.** The sections
> below tell an agent *how to work* here — where knowledge, decisions, and the
> plan live, and the rules that keep those docs from rotting.

## First run — set up this project

If this copy still has template placeholders (`<PROJECT>`, `[EXAMPLE]` artifacts,
an unfilled **Project preferences** section), it hasn't been customized yet. On
your first interaction, **proactively offer to run the setup dialog** in
[`SETUP.md`](SETUP.md) — a short Q&A that fills in the project's identity,
preferences, and plan and removes the examples. If the user would rather set it
up by hand, point them to [`README.md`](README.md) § Manual setup. When setup is
complete, `SETUP.md` has you delete it and this section (it's one-time only).

## Agent knowledge base

The project's working knowledge lives in `context/` as an
[OKF](https://github.com/GoogleCloudPlatform/knowledge-catalog/blob/main/okf/SPEC.md)
v0.1 bundle.

- Read [`context/index.md`](context/index.md) first — the bundle catalog.
- [`context/CONVENTIONS.md`](context/CONVENTIONS.md) — frontmatter, type
  vocabulary, ingest/query/lint rules. Read before editing `context/`.
- `context/` is agent-owned. `references/` is immutable — read, never modify.
- For the live plan and status, read [`planning/PROGRESS.md`](planning/PROGRESS.md)
  and [`planning/ROADMAP.md`](planning/ROADMAP.md).

## Planning hygiene

Each doc has one job — keep them from re-narrating each other. **The *why* behind
each rule is in [`README.md`](README.md) § "Why these rules" — read it before
relaxing a rule. A rule without its reason gets deleted the first time it's
inconvenient.**

- [`context/log.md`](context/log.md) — the **durable chronological record**:
  what landed when, decision/change detail, routine ingests/lint. History lives
  here, newest-first.
- [`context/decisions/index.md`](context/decisions/index.md) — the **decided
  register**: fork # ↔ ADR ↔ status. The single source of truth for what's decided.
- [`planning/PROGRESS.md`](planning/PROGRESS.md) — **present state only**
  (Now / In-progress / Next). Volatile; rewrite freely. No "Done" history here
  (that's `log.md`). "Next" is the 1–3 *immediate* actions and points to ROADMAP
  for the candidate backlog — it doesn't re-list it.
- [`planning/ROADMAP.md`](planning/ROADMAP.md) — scope, phases, and the **single
  backlog** of open + candidate *forks* (a **fork** = a decision point that needs
  a choice — a fork in the road, not a git fork; see CONVENTIONS § Decisions).
  Defers the decided register (and the fork count) to `decisions/index.md`; never
  enumerates a fork range.

So:

- **On a significant decision** (a fork resolved or changed): write/update the
  ADR in [`context/decisions/`](context/decisions/index.md) per the lifecycle in
  [`context/CONVENTIONS.md`](context/CONVENTIONS.md) (status: proposed →
  accepted / superseded / deferred), add/update its row in the decided register,
  append **one thin** `log.md` line (chronology + link — the ADR owns the
  rationale), and refresh PROGRESS's present state. Touch ROADMAP only if it
  opens/closes an *open* fork or shifts a phase.
- **On implementing a significant change**: append a `log.md` entry (the detail)
  and refresh PROGRESS's present state — a thin pointer, not a history.
- **Closing scrubs the future**: when you resolve a fork or complete a
  candidate / "Next" item, *delete its forward-looking mentions* (ROADMAP
  candidates, PROGRESS "Next") — not just the present-state bullet you're editing.
  Open work has one home; a resolved item leaves it.
- **No live tallies in durable prose**: never bake a running count (test counts,
  item totals, "N of M done", fork ranges like "1–N") into present-tense docs
  (PROGRESS, ROADMAP) or ADR bodies — it drifts. Describe the state ("all tests
  green", "none open — see the register"), don't tally it. A specific number
  belongs only in a dated `log.md` snapshot (history, frozen at write time).

## Project preferences (edit these)

<!-- These are YOUR knobs, not part of the method. Set them to taste, then delete
     this comment. -->

- **Commit messages**: <e.g. no `Co-Authored-By` trailer · Conventional Commits ·
  ticket prefixes — your call>.
- **Repo layout**: list your top-level dirs and what each holds, e.g.
  - `references/` — immutable source-of-truth inputs. Read, never modify.
  - `context/` — agent knowledge bundle (maintain this).
  - `docs/` — human-facing guides (terse).
  - `planning/` — live plan + progress. Read first for "what now".
  - `<src/ · assets/ · …>` — <your code / assets>.
- **Build / test / run**: <how to build, test, and run this project>.
- **Anything else an agent must know**: <domain constraints, coding style, review
  expectations, …>.
