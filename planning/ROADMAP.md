# <PROJECT> — Roadmap

The plan: scope, sequencing, and **open** forks. Living doc — rewrite freely. For
"where are we right now" see [PROGRESS.md](./PROGRESS.md). Resolved forks and
their rationale live in [`../context/`](../context/) as `type: Decision` concepts;
the **decided register** (fork # ↔ ADR ↔ status) is
[`../context/decisions/index.md`](../context/decisions/index.md). This file does
not duplicate it — it tracks scope, phases, and forks still open.

## Scope

<!-- What this project is and isn't. A few lines. Note explicitly deferred areas. -->

## Phases

<!-- Coarse sequencing. Mark status inline (done / in progress / next / deferred). -->

- **Phase 0 — Foundation**: <repo setup, docs structure, initial ingests>.
- **Phase 1 — <…>**: <…>.

## Open forks

A *fork* is a decision point — a branch in the plan/design that needs a choice
(see [`../context/CONVENTIONS.md`](../context/CONVENTIONS.md) § Decisions).

Status legend: `OPEN` (undiscussed) · `PROPOSED` (Decision concept written,
`status: proposed`) · `DECIDED` / `DEFERRED` (has an ADR — see the register).

**None open yet.** Genuinely open (undeliberated) forks get a line here until they
have an ADR; once decided they move to the
[register](../context/decisions/index.md) and leave this list. Candidate future
forks (not yet opened) can be listed here too — the single backlog. Incremental
*work* that needs no fork lives in [PROGRESS](./PROGRESS.md) "Next", not here.

<!-- e.g.
- <One-line fork: the question, and what each option weighs>.
-->

## Fork lifecycle

When a fork is deliberated, write a `type: Decision` concept in `../context/`
(`status: proposed` → `accepted`), add its row to the decided register
([`../context/decisions/index.md`](../context/decisions/index.md)), and update
PROGRESS. A genuinely open (undeliberated) fork gets a line under **Open forks**
above until it has an ADR.
