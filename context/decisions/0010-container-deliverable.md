---
type: Decision
title: ADR 0010 — A container image as a second deliverable, built from the bundle
description: Ship an exported distroless image alongside the tarball, with a launcher that mounts only the directories in play and runs as the caller
decision_status: accepted
fork: 10
tags: [decision, packaging, release, docker, ci]
generated:
  by: claude/opus-5
  at: 2026-08-26T06:45:50Z
---

# Context

[ADR 0009](./0009-release-bundle.md) shipped a relocatable tarball and
**rejected Docker** — correctly, for the constraint it was given: a user who
wants to convert a flight should not need a container runtime.

That constraint has not changed. What changed is the audience. Some hosts
already orchestrate containers, some operators would rather load an image than
trust an unpacked tree of shared objects, and some environments make extracting
and running a tarball the awkward path rather than the easy one. For them the
overhead ADR 0009 avoided is overhead they are already paying.

So this is *additive*: a second way to get the same binary, not a replacement.
The tarball remains the recommended default.

# Decision

**Ship an exported container image alongside the tarball**, as a directory
containing the image, a launcher, and usage:

```
parrot-to-klv-<version>-linux-x86_64-docker/
  parrot-to-klv                          launcher (docker | podman)
  parrot-to-klv-<version>-x86_64-image.tar.gz   docker load
  USAGE.md
  THIRD-PARTY.md
```

> **Revisited 2026-08-26.** The original x86-64 deliverable named its inner
> image archive `parrot-to-klv-<version>-image.tar.gz`. ADR 0017 added a second
> architecture, so both the image tag and this archive now carry the `uname -m`
> architecture spelling. The deliverable shape is otherwise unchanged.

**Built FROM the bundle.** `make-image.sh` takes the staged bundle as input,
because two independently-built artifacts would eventually describe different
binaries and nobody would notice until a bug reproduced in one and not the
other. One build, two packagings.

**distroless base.** The bundle needs only glibc, so the base can be
`gcr.io/distroless/base-debian12`. Measured, not assumed:

| base | image | `docker save \| gzip -9` |
|---|---|---|
| `ubuntu:22.04` | 94 MB | 34.2 MB |
| `distroless/base-debian12` | 37 MB | **13.8 MB** |

Base size matters more here than usual: layer sharing normally amortizes it, and
`docker save` defeats that entirely — every user downloads the full base every
time. The bundle's shell wrapper is not used in the image; `ENV` does the same
job at build time, so no shell is needed at all.

**The launcher takes arguments first and prompts only as a fallback**, and only
when stdin *and* stdout are terminals. One interface serves both a person at a
prompt and a batch script; a cron job with a missing argument gets usage and a
non-zero exit rather than blocking forever on a question nobody will answer.

Three things it does that are easy to get wrong:

- **`--user "$(id -u):$(id -g)"`.** Without it the container runs as root and
  every output file lands on the host owned by `root:root`, which the user then
  cannot delete without `sudo`. This is the single most common complaint about
  containerized file tools. It works because the tool needs no home directory:
  the GStreamer registry falls back to `mktemp` when the cache path is not
  writable, verified with `HOME` unset and with `HOME` pointing at nothing.
- **Mount the file's directory, not the tree.** Input mounted read-only at
  `/in`, output read-write at `/out`, paths rewritten to match. When both are the
  same directory it is mounted once, read-write, at `/work` — one host path at
  two container paths works but reads as though they were different places.
- **Report the host path on success.** The tool prints the path it was given,
  which is the container's view (`/work/flight.ts`) and means nothing to whoever
  typed a host path. The launcher prints where the file actually landed.

`--network none` is set: the tool never uses the network, so it costs nothing.

# Alternatives considered

## `ubuntu:22.04` base

Familiar, and gives a shell for debugging inside the container.

**Rejected on size**, at 2.5× the download for a deliverable people fetch by
hand. The debugging loss is real and lands on whoever is supporting a frustrated
user — mitigated by `packaging/Dockerfile.debug`, identical but for its `FROM`
line, so "run the debug variant" is a one-line answer.

## Prompt for everything, document raw `docker run` for batch

The shape originally sketched.

**Rejected** in favor of args-first. Two interfaces means two things to
document, keep in step and get wrong; and a script that always prompts cannot be
scripted at all. Raw `docker run` is still documented, but as a fallback for
people who want it rather than the only non-interactive path.

## Publish to a registry instead of exporting a tarball

**Not now.** It needs a registry, an account and a network path from wherever
the tool runs — the last of which is exactly what some of the target
environments do not have. An exported tarball is `scp`-able. Worth revisiting if
the audience turns out to be registry-connected.

# Consequences

- **Two deliverables to keep in step.** Mitigated structurally by building the
  image from the bundle, and by CI smoke-testing both, but it is still two
  things to describe and two things to get wrong.
- The image smoke test asserts something the bundle's cannot: that output is
  owned by the invoking user. That is the `--user` regression test, and without
  it the flag could be dropped and nothing would fail.
- No shell in the image means no `exec` in to look around when a user reports a
  problem. `Dockerfile.debug` exists for that, and the usage notes say so.
- The launcher resolves paths on the host before the container starts, so
  relative paths and symlinks work as users expect. It also means the container
  never sees a path the user did not name.
- `USAGE.md` is assembled from a shared body plus a per-deliverable invocation
  section, so the substantive half cannot drift between the two.

# Assumptions / open questions

- Assumes `docker` or `podman` is on `PATH` and usable by the invoking user. The
  launcher checks and says which is missing, but cannot fix a broken daemon or a
  user outside the `docker` group.
- Podman is detected and passed the same flags, which are portable across both —
  but this is **untested against podman**; only docker is exercised locally and
  in CI.
- Assumes the image is loaded from the tarball beside the launcher. Someone who
  moves the launcher on its own gets a clear error, not a silent failure.
- SELinux relabeling (`:z`/`:Z` on mounts) is not handled. Irrelevant on the
  supported Ubuntu targets; it would matter on Fedora or RHEL.

# Citations

- [ADR 0009](./0009-release-bundle.md) — the bundle this is built from, and
  where Docker was rejected as the *only* deliverable
- `packaging/make-image.sh`, `packaging/Dockerfile`, `packaging/parrot-to-klv`
- `.github/workflows/release.yml` — the `smoke-image` job and its ownership check
