# Parrot to KLV

Converts Parrot drone telemetry metadata embedded in MP4 files to MPEG-TS files
with video and MISB 0601 KLV streams. A command-line tool, supporting the v3 and
proto metadata formats; v2 is a deferred add-on and v1 is out of scope. See
[`planning/ROADMAP.md`](planning/ROADMAP.md) § Scope.

> **This repo uses an OKF-based agent documentation workflow.** The sections
> below tell an agent *how to work* here — where knowledge, decisions, and the
> plan live, and the rules that keep those docs from rotting.

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

## Before every commit checklist

When you implement a significant change (new feature, phase start, decision):

1. ✅ Code changes staged
2. ✅ `context/log.md` updated (append dated entry)
3. ✅ `planning/PROGRESS.md` updated (Now/In-progress/Next reflect current state)
4. ✅ If decision made: ADR written, `context/decisions/index.md` updated

**Never commit code without updating the docs that describe it.** If you're about
to commit and haven't touched `log.md` or `PROGRESS.md`, stop and update them first.

What goes in each doc — and the rules behind this checklist — is **Planning
hygiene** below.

## Planning hygiene

Each doc has one job — keep them from re-narrating each other. **The *why* behind
each rule is in
[`context/workflow-rationale.md`](context/workflow-rationale.md) — read it before
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
  and refresh PROGRESS's present state — a thin pointer, not a history. **DO THIS
  IN THE SAME COMMIT** as the implementation. Never commit code without updating
  both docs.
- **Closing scrubs the future**: when you resolve a fork or complete a
  candidate / "Next" item, *delete its forward-looking mentions* (ROADMAP
  candidates, PROGRESS "Next") — not just the present-state bullet you're editing.
  Open work has one home; a resolved item leaves it.
- **No live tallies in durable prose**: never bake a running count (test counts,
  item totals, "N of M done", fork ranges like "1–N") into present-tense docs
  (PROGRESS, ROADMAP) or ADR bodies — it drifts. Describe the state ("all tests
  green", "none open — see the register"), don't tally it. A specific number
  belongs only in a dated `log.md` snapshot (history, frozen at write time).

## Project preferences

- **Spelling: American English**, everywhere prose appears — code comments, doc
  strings, CLI help and error text, test messages, Markdown, commit messages.
  `color` not `colour`, `center` not `centre`, `behavior` not `behaviour`,
  `labeled` not `labelled`, `-ize`/`-ization` not `-ise`/`-isation`, `meters`
  not `metres`, `license` (noun and verb) not `licence`, `judgment`,
  `acknowledgment`, `analyze`. This is consistency, not correctness — the
  standards this project implements (MISB ST 0601, "Frame **Center**") are
  American-spelled, so matching them keeps our prose and our quoted item names
  from disagreeing on the same page. **`references/` and `external/` are exempt
  and must not be touched**: the first is immutable source-of-truth input, the
  second is vendored third-party code.
- **Commit messages**: Conventional Commits format. No `Co-Authored-By` trailer.
- **Repo layout**:
  - `references/` — immutable source-of-truth inputs. Read, never modify.
  - `context/` — agent knowledge bundle (maintain this).
  - `docs/` — human-facing guides (terse).
  - `planning/` — live plan + progress. Read first for "what now".
  - `src/` — C++ source code.
  - `test/` — tests, synthetic MP4 fixtures (`make_fixture.py` regenerates them;
    the two with a real H.264 track need ffmpeg to regenerate, never to use),
    and `conformance/` reports. **No test may require anything from `data/`**: the
    suite must pass on a machine that has never held footage. One test
    (`conformance_real_data`) *uses* `data/` when it is there and skips when it is
    not — that is the only permitted relationship.
  - `data/` — test input files (contains sensitive videos — never commit).
  - `external/` — vendored dependencies (minimp4 and xxHash, both
    single-header).
- **Build / test / run**: C++ with CMake. FetchContent pulls libmisbklv at an
  exact commit — bumping that pin is its own commit, never a drive-by.
  Standard CMake workflow: `cmake -S . -B build && cmake --build build && ctest --test-dir build`.
  libmisbklv is a subproject, so a bare `ctest` runs its suite too — useful as
  early warning when the pin moves. `ctest -L parrot` is this project's tests
  alone, which is what CI runs; label every new test so it is not silently
  absent there.
- **Security**: `data/` contains sensitive test videos with real location data.
  Never commit files from `data/`. Never store or mention identifying information
  (e.g., embedded GPS coordinates) in memory, logs, or documentation.
