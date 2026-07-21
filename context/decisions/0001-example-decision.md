---
type: Decision
title: Adopt the OKF-based documentation workflow
status: accepted
tags: [decision, meta, docs]
timestamp: 2000-01-01T00:00:00Z
fork: 1
---

<!-- [EXAMPLE — keep or replace] This ADR demonstrates the format (frontmatter +
     the body sections below). It also happens to record a real decision: that
     this project uses this doc workflow. Keep it as your ADR 0001, or delete it
     and write your own first decision. -->

# Context

A new project needs a place for its working knowledge, its decisions, and its
plan that an AI agent can maintain across sessions without the docs drifting out
of sync. Ad-hoc notes rot: history, decisions, and open work get restated in
several files, and the copies diverge.

# Decision

Adopt the `okf-project-template` workflow: an [OKF](https://github.com/GoogleCloudPlatform/knowledge-catalog/blob/main/okf/SPEC.md)
knowledge bundle in `context/`, a decision register (ADRs) under
`context/decisions/`, a volatile plan in `planning/`, human docs in `docs/`, and
immutable inputs in `references/` — governed by the planning-hygiene rules in
`CLAUDE.md` (one job per doc; history → `log.md`; no live tallies; closing an
item scrubs its forward-looking mentions).

# Alternatives considered

- **Ad-hoc Markdown notes** — no structure; duplication and drift set in quickly.
- **A wiki / external tool** — lives outside the repo, so it desyncs from the code
  and isn't in the agent's default context.
- **Only ADRs, no living plan** — records decisions but loses "what now / what
  next" and present state.

# Consequences

- Knowledge, decisions, and plan each have one home; an agent knows where to read
  and write.
- Some upfront discipline (write the ADR, update the register + log + PROGRESS).
- The rules must be understood, not just followed — see `README.md` § "Why these
  rules".

# Assumptions / open questions

- Targets OKF v0.1; revisit on a new OKF version.

# Citations

[1] `README.md` — the template's rationale and setup.
[2] OKF spec — the `context/` bundle format.
