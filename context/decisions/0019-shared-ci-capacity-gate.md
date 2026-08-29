---
type: Decision
title: ADR 0019 — Share a two-slot capacity gate across self-hosted CI
description: Regular parrot and libmisbklv CI jobs share two host capacity slots, allowing two parrot jobs in parallel while capping combined self-hosted workload.
decision_status: accepted
tags: [decision, ci, security, self-hosted, parallelism]
generated:
  by: openai/gpt-5
  at: 2026-08-29T15:12:46Z
sources:
  - id: parrot-to-klv-108
    resource: https://github.com/nitsuga/parrot-to-klv/issues/108
    title: Run build and sanitizer jobs in parallel on self-hosted runners
  - id: libmisbklv-67
    resource: https://github.com/nitsuga/libmisbklv/issues/67
    title: Share a two-slot capacity gate with parrot-to-klv
  - id: github-workflow-syntax
    resource: https://docs.github.com/en/actions/reference/workflows-and-actions/workflow-syntax#concurrency
    title: GitHub Actions workflow syntax — concurrency
---

# Context

The normal build and sanitizer jobs in parrot-to-klv should run concurrently,
but its repository-scoped runners share a seven-CPU host with libmisbklv's
runners. Cross-repository overlap is rare, not impossible. GitHub Actions
concurrency groups are scoped to a repository, so matching group names cannot
protect the shared host.

The libmisbklv CI image and parrot-to-klv use the same toolchain. Each normal
compile is capped at three build processes. Two shared capacity slots therefore
allow the two parrot jobs to run together while bounding combined compile load
to six processes. The image publisher needs both slots because it uses the host
Docker daemon.

# Decision

Add the same small `ci/with-capacity.sh` helper to both repositories. It uses
two `flock` files in the existing `/home/eric/ci-cache` host mount. Normal
build, sanitizer, generated-drift, and parrot jobs claim one slot for each
substantive command; the libmisbklv image publisher claims both. Hosted fork
jobs use an ephemeral `/tmp` lock path. Trusted parrot jobs use two
repository-scoped self-hosted runner registrations labeled `parrot-to-klv`.

# Alternatives considered

- **Rely on manual non-overlap** — rejected because “rare” is not an enforced
  safety property.
- **Use GitHub concurrency groups** — rejected because the groups are scoped to
  a repository, not the shared host.
- **Keep one parrot runner** — rejected because build and sanitizer checks remain
  serialized.
- **Use a separate host** — rejected as unavailable.
- **Put all jobs behind one global lock** — rejected because it would remove the
  desired parallelism. Two slot locks preserve it while bounding load.

# Consequences

Two parrot jobs can run concurrently, and occasional overlap with libmisbklv is
bounded rather than dependent on operator timing. Jobs may occupy a runner
while waiting for a capacity slot. The lock files depend on the existing shared
host mount, and both repositories must keep the helper and slot count aligned.

# Assumptions / open questions

The host continues to provide seven CPUs and the normal builds continue to use
`-j3`. If either changes, the slot count or per-job build parallelism must be
re-evaluated. The gate is a capacity bound, not a cross-repository workflow
queue or fairness guarantee.

# Citations

- [GitHub Actions workflow syntax — concurrency](https://docs.github.com/en/actions/reference/workflows-and-actions/workflow-syntax#concurrency) — documents that concurrency groups operate within the repository.
- [parrot-to-klv #108](https://github.com/nitsuga/parrot-to-klv/issues/108) — records the parrot runner and parallelism requirement.
- [libmisbklv #67](https://github.com/nitsuga/libmisbklv/issues/67) — records the matching sibling-repository gate.
