# Parrot to KLV

Converts Parrot drone telemetry metadata embedded in MP4 files to MPEG-TS files
with video and MISB 0601 KLV streams. A command-line tool, supporting the v3 and
proto metadata formats; v2 is a deferred add-on and v1 is out of scope. See
[`planning/ROADMAP.md`](planning/ROADMAP.md) § Scope.

> **This repo uses an OKF-based agent documentation workflow.** The sections
> below tell an agent *how to work* here — where knowledge, decisions, and the
> plan live, and the rules that keep those docs from rotting.

> **This file is the canonical, vendor-neutral copy — edit it here.** `CLAUDE.md`
> is a one-line `@AGENTS.md` import, since Claude Code auto-loads that filename.
> Supporting another agent means adding another thin pointer, never a second
> copy of these rules.

## Agent knowledge base

The project's working knowledge lives in `context/` as an
[OKF](https://github.com/GoogleCloudPlatform/knowledge-catalog/blob/main/okf/SPEC.md)
v0.2 bundle.

- Read [`context/index.md`](context/index.md) first — the bundle catalog.
- [`context/CONVENTIONS.md`](context/CONVENTIONS.md) — frontmatter, type
  vocabulary, ingest/query/lint rules. Read before editing `context/`.
- `context/` is agent-owned. `references/` is append-only: read it, add a
  snapshot on a directed ingest, never edit one that's already there. The one
  exception is **withdrawal** — removing a snapshot whose content this project
  may not redistribute — which carries its own requirements (a link file, pinned
  replacements, an ADR); see `context/CONVENTIONS.md` § Source discipline.
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
  for the candidate backlog — it doesn't re-list it. "Now" is where the *work*
  is, **not a feature inventory**: if a sentence would still be true after a
  month of no work, it's durable knowledge — it belongs in a `context/` concept
  or an ADR, with only a pointer here.
- [`planning/ROADMAP.md`](planning/ROADMAP.md) — scope, phases, and the **single
  backlog** of open + candidate *forks* (a **fork** = a decision point that needs
  a choice — a fork in the road, not a git fork; see CONVENTIONS § Decisions).
  Defers the decided register (and the fork count) to `decisions/index.md`; never
  enumerates a fork range.

So:

- **On a significant decision** (a fork resolved or changed): write/update the
  ADR in [`context/decisions/`](context/decisions/index.md) per the lifecycle in
  [`context/CONVENTIONS.md`](context/CONVENTIONS.md) (`decision_status`: proposed
  → accepted / superseded / deferred), add/update its row in the decided register,
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
  from disagreeing on the same page. **`references/` is exempt, and no snapshot
  already there is ever restyled** — it is append-only source-of-truth input:
  an ingest may add a new source, but nothing already deposited is edited,
  reformatted, or restyled to match. So is `external/`: vendored third-party
  code, read it, never restyle it.
- **Commit messages**: Conventional Commits format. No `Co-Authored-By` trailer.
- **Agent authorship on PRs/issues/comments**: when an agent posts a PR body,
  an issue, or a review comment, it identifies *itself* (the generating model)
  as the author — `Author: <agent> (model: <producer>/<model>)` — separate from
  the git commit author, which stays the human/machine that invoked `git`. The
  commit message's author field is not the place for this; it goes in the
  PR/issue/comment prose so a reader can tell which model produced what. A
  comment's author line names the model that wrote *that* comment, not the one
  that opened the PR. More than one model works this repo, so an unattributed
  comment is genuinely ambiguous — and the author line is the only place that
  ambiguity gets resolved, since every agent's commits carry the same git
  author.
  - `<producer>` is the vendor or harness that generated the text, `<model>`
    identifies the model: `claude/opus-5`,
    `opencode-go/muse-spark-1.2-contributor`. One pair, no exceptions.
  - That is the same pair the knowledge bundle's `generated.by` actor form uses
    ([`context/CONVENTIONS.md`](context/CONVENTIONS.md) § Actors), written for a
    narrower case — so an agent spells itself identically in frontmatter and in
    prose. Two spellings for one agent is the failure this rule exists to
    prevent; don't reintroduce it for a single awkward model string.
  - Applies going forward. Comments predating it are left as they are —
    re-signing history would churn many PRs for no gain.
- **Repo layout**:
  - `references/` — source-of-truth inputs, append-only. Read; add a snapshot
    on a directed ingest; never edit what's there. A snapshot may be *withdrawn*
    only when redistributing it would infringe, and only with the link file,
    pinned replacements, and ADR that CONVENTIONS § Source discipline requires.
    Prefer a redistributable source when depositing: check the terms first.
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
  Preferred: `cmake --preset release && cmake --build --preset release && ctest --test-dir build/release`
  (also `debug`, `sanitize`, `release-bundle` — see README.md's preset matrix;
  these are the same presets CI uses). Fallback without presets:
  `cmake -S . -B build && cmake --build build && ctest --test-dir build`.
  libmisbklv is a subproject, so a bare `ctest` runs its suite too — useful as
  early warning when the pin moves. `ctest -L parrot` is this project's tests
  alone, which is what CI runs; label every new test so it is not silently
  absent there.
- **Security — location and identity data**: `data/` holds real recordings with
  live location data. **Never commit a file from `data/`**, and never copy what
  one contains — coordinates, airframe serials, absolute flight times, operator
  or device identity — into code, tests, fixtures, commit messages,
  documentation, or agent memory. Test fixtures are synthesized, never derived
  from a recording, and `parrot-scrub` / `parrot-conform` exist precisely so a
  real flight can yield shareable evidence without either: scrub replaces the
  identifying fields, conform reports what an encoder produced as range checks
  (`"latitude_within_range": true`) rather than as values.
  - **Scope: recordings this project holds, and anything derived from them.**
    The rule protects flight data; it is not a rule about citing external
    material. Published facts about third-party media the project does not hold
    — where a sample came from, who holds the rights, why its terms were judged
    unclear — are bibliography, and belong wherever the reasoning belongs.
  - The sibling repository **libmisbklv carries this same rule, scoped the same
    way**. The same class of data crosses both repositories, so the two are kept
    aligned deliberately — change one and change the other.
