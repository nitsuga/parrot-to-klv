# Project setup (one-time)

<!-- This file guides an AGENT through turning a fresh copy of
     okf-project-template into a real project, via a short dialog with the user.
     It removes itself when done (step 5). A user who prefers to set up by hand
     can follow README § "Manual setup" instead. -->

You (the agent) are helping the user customize this freshly-copied template. Run
it as a **short, conversational dialog** — ask a few grouped questions, use the
answers to fill in the placeholders, then delete the example artifacts. Don't
interrogate: offer sensible defaults, let the user answer loosely or say "you
decide", and skip anything they don't care about yet.

**Find every fill-in** first:

```
grep -rnE '<PROJECT>|<[a-z][^>]*>|\[EXAMPLE|<!--' --include='*.md' .
```

That surfaces the placeholders (`<PROJECT>`, `<…>`), example artifacts
(`[EXAMPLE]`), and setup guidance comments (`<!-- … -->`) — everything to replace
or strip.

## 1. Ask (group these — 2–4 at a time, not all at once)

- **Identity** — What's the project called, and a one-line description of what it
  is? Its scope (what's in, and anything explicitly out)?
- **Preferences** — Commit-message convention? The main code/asset directories
  and what each holds? How do you build / test / run it? Any coding style or
  constraints an agent must respect?
- **Domain types** *(optional)* — Beyond the starter concept types (`Reference`,
  `Concept`, `Component`, `Decision`), any domain-specific type to add or rename
  (e.g. `Dataset`, `Standard Reference`)? Fine to skip and add later.
- **Starting point** — What's the first phase or two, and the first 1–3 tasks?
  Any external sources/specs to pull into `references/` up front?
- **Optional CI** — a dormant GitHub Action that fails on broken internal doc
  links ships disabled. Offer to enable it; if yes:
  `git mv .github/workflows/link-check.yml.disabled .github/workflows/link-check.yml`
  (otherwise leave it — it never runs while disabled).

## 2. Fill in (from the answers)

- `CLAUDE.md` — the `# <PROJECT>` heading + description; the **Project
  preferences** section (commit policy, repo layout, build/test/run, constraints).
- `README.md` — this becomes *their project's* README: replace the template's
  title/intro and the **adoption sections** (Quick Start, Manual setup, the
  template's own Structure block) with content about their project. Keep "The doc
  taxonomy" / "Why these rules" if useful for their contributors (or move them to
  `docs/`). Keep the Quick Start only if they want others to re-template from
  their repo.
- `context/CONVENTIONS.md` — set the real `timestamp`; trim/rename the type
  vocabulary to their domain.
- `context/index.md` — the `# <PROJECT>` title.
- `planning/ROADMAP.md` — Scope + Phases from their answers.
- `planning/PROGRESS.md` — the `# <PROJECT>` title, "Now", and the first "Next"
  tasks.
- `LICENSE` — the template ships **0BSD** (permissive, no attribution). Ask
  whether they want to keep it for their project or swap in their own; update the
  README license line to match.

As you edit each file, also **delete its `<!-- … -->` guidance comments** — those
are setup hints, not project content.

## 3. Delete the example artifacts

- `context/decisions/0001-example-decision.md` — or **keep it** as their real ADR
  0001 recording that they adopted this workflow (ask which they prefer).
- The `[EXAMPLE]` entries in `context/log.md`, `context/index.md`,
  `context/decisions/index.md`, `planning/ROADMAP.md`, `planning/PROGRESS.md`.
- `references/README.md` / `docs/README.md` once real content lands (fine to leave
  for now).

## 4. Ingest any starting sources (optional)

If they named specs/repos to ingest, follow `context/CONVENTIONS.md` § Operations
(Ingest): read the source into `references/`, write a concept in `context/`, add
an `index.md` entry, append a `log.md` line.

## 5. Self-clean

When setup is complete:

- **Delete this `SETUP.md`.**
- **Delete the "First run — set up this project" section from `CLAUDE.md`.**
- **Verify nothing's left** — with this file gone, this should return nothing:

  ```
  grep -rnE '<PROJECT>|\[EXAMPLE|<!--' --include='*.md' .
  ```

- Commit it all (e.g. "Set up <project> from okf-project-template").

The onboarding scaffolding is now gone; the project runs on its own docs. From
here, follow the ongoing rules in `CLAUDE.md` § Planning hygiene.
