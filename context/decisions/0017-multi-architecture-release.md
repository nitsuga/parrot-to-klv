---
type: Decision
title: ADR 0017 — Ship arm64 alongside x86-64, building both in a pinned 22.04 container on hosted runners
description: Multi-architecture deliverables come from a matrixed native build on GitHub's Linux arm64 hosted runners, with both architectures built inside a pinned ubuntu:22.04 job container so the glibc floor stops depending on a runner label that is being retired; one bundle and one arch-tagged container per architecture, no fat manifest
decision_status: accepted
fork: 15
tags: [decision, release, packaging, ci, arm64, portability, glibc]
generated:
  by: openai/gpt-5
  at: 2026-08-26T05:13:14Z
sources:
  - id: gh-hosted-runners
    resource: https://docs.github.com/en/actions/reference/runners/github-hosted-runners
    title: GitHub — About GitHub-hosted runners (arm64 labels, vCPU by repository visibility)
  - id: ubuntu-2204-deprecation
    resource: https://github.com/actions/runner-images/issues/14254
    title: actions/runner-images#14254 — ubuntu-22.04 and ubuntu-22.04-arm begin deprecation 2026-09-17, fully unsupported 2027-04-17
  - id: checkout-rest-fallback
    resource: https://github.com/actions/checkout/blob/main/adrs/0153-checkout-v2.md
    title: actions/checkout ADR 0153 — REST API fallback when git is unavailable; fails outright when lfs is true
  - id: ubuntu-jammy-libc6
    resource: https://packages.ubuntu.com/jammy/libc6
    title: Ubuntu — libc6 in jammy (glibc 2.35; jammy-updates and -security carry Ubuntu revisions of the same upstream release)
---

# Context

Fork 15 asks for `aarch64` deliverables beside the x86-64 ones. The fork was
originally framed as gated on making this repository public; that framing was
wrong and has already been corrected in [ROADMAP](../../planning/ROADMAP.md) —
arm64 hosted runners reached private repositories in January 2026. Visibility
changes the *resources* (4 vCPU and unmetered minutes public, 2 vCPU against
the included allotment private), not the availability.[^gh-hosted-runners]
**This decision is therefore not blocked on the pending publication work.**

The code side is already done, and by accident rather than by effort. Nothing in
`src/`, `CMakeLists.txt`, or `CMakePresets.json` names an architecture: no
intrinsics, no `-march`, no assumed word size beyond what the fixed-width KLV and
MP4 parsing already spell explicitly. libmisbklv is the same.
`packaging/make-bundle.sh` discovers GStreamer through `pkg-config` and closes
the shared-object set with `ldd`, so it has no architecture-specific path to get
wrong. `packaging/Dockerfile` is `FROM gcr.io/distroless/base-debian12`, a
multi-architecture manifest that resolves to arm64 on an arm host without being
told. What is *not* architecture-clean is `release.yml`, and only in its naming.

**A second, unrelated clock forced its way into this fork.** The x86 build job is
pinned to `ubuntu-22.04` deliberately: [ADR 0009](./0009-release-bundle.md) makes
the oldest supported target the build host, because glibc compatibility runs
forward only, and the 2.35 floor *is* that runner image. Both `ubuntu-22.04` and
its natural arm counterpart `ubuntu-22.04-arm` begin deprecation on 2026-09-17
and are fully unsupported after 2027-04-17, with brownout windows before then
that fail jobs outright.[^ubuntu-2204-deprecation]

That would have been a separate fork, except that resolving it separately means
building the arm leg on a label already scheduled for removal — knowingly writing
a second job that has to be rewritten within months. ADR 0009 anticipated this
exactly, in its own open questions:

> Assumes `ubuntu-22.04` remains available as a GitHub runner. It is deprecated
> eventually; when it goes, the choice is a `container: ubuntu:22.04` build (the
> 20.04 plan, resurrected) or dropping 22.04.

So this ADR resolves both: **which infrastructure builds arm64**, and **what the
glibc floor rests on once the runner image carrying it is gone.**

# Decision

**Build both architectures natively on GitHub's hosted runners, inside a pinned
`ubuntu:22.04` job container, as a matrixed `release.yml` producing a complete
independent deliverable set per architecture.**

Three parts.

## 1. Native arm64, matrixed

The `build` job matrices over an x86-64 runner and an arm64 one, with the
architecture carried as a matrix variable and substituted into the bundle name in
place of the literal `linux-x86_64`. `smoke` and `smoke-image` matrix the same
way and run **natively on their own architecture** — `smoke-image` especially,
since it does a real `docker load` and executes the launcher, which cannot be
emulated honestly.

Artifacts get architecture-qualified names. `upload-artifact` on its current
major rejects a duplicate artifact name rather than merging into it, so two
matrix legs uploading `bundle` is an error; `publish` collects them by pattern
and attaches every asset to the one release.

## 2. The glibc floor moves into a container

The build job runs on `ubuntu-24.04` / `ubuntu-24.04-arm` with
`container: ubuntu:22.04` pinned by digest. The runner image becomes irrelevant
to the produced binary; what determines the floor is a base image we name.

**What the digest pins is the starting filesystem and apt configuration — not
the toolchain.** The job still runs `apt-get update` and installs CMake, GCC,
and the GStreamer development packages from the live Jammy repositories, so
those can move between builds. That is fine, and deliberately not worth fixing.
What the container selects is a **distribution line**, and Jammy's repositories
carry glibc 2.35 — `jammy-updates` and `jammy-security` ship Ubuntu revisions of
that same upstream release, not a newer one.[^ubuntu-jammy-libc6] So the moving
packages cannot raise the floor.

That is an assumption about a distribution's update policy, though, not a
guarantee this repository controls — which is exactly why the GLIBC symbol check
is the thing that **enforces** the floor. If Jammy ever did serve a newer glibc,
the check fails the build rather than shipping a binary that dies on a user's
machine. Freezing the toolchain outright would mean snapshot repositories or
pinned package versions, which buys a reproducibility this decision does not
need.

This **keeps the 22.04 support commitment intact** rather than dropping it, and
it is strictly more honest than the status quo: today the floor is asserted by a
runner label that GitHub controls and is retiring. After this it is asserted by a
pinned digest, and the existing glibc check still verifies the result rather than
trusting either.

The check itself is **unchanged**. It reads the highest `GLIBC_<version>` symbol
the binary imports and fails above 2.35 — a comparison of version strings, not of
architectures, correct on aarch64 as written. It remains the thing that would
catch this decision being implemented wrongly.

## 3. The container image tag becomes architecture-qualified

`packaging/make-image.sh` tags the image `parrot-to-klv:<version>`, and
`packaging/parrot-to-klv` (the launcher) loads the tarball **only if that tag is
not already present**:

```sh
if ! "$ENGINE" image inspect "$IMAGE_TAG" >/dev/null 2>&1; then
  ... "$ENGINE" load -i "$IMAGE_FILE"
fi
```

That check is right for its original purpose — load once, not on every
invocation. With two architectures publishing the same tag it becomes a trap: on
a machine that has loaded either image, the other architecture's launcher finds
the tag, **skips its own load**, and runs the wrong binary. The failure is silent
at load time and surfaces as an exec-format error or an emulated run, neither of
which points at the cause.

Qualifying the tag (`parrot-to-klv:<version>-<arch>`) costs one `sed`
substitution in `make-image.sh`, preserves the launcher's caching behavior, and
makes the two deliverables genuinely independent — which is what
[ADR 0010](./0010-container-deliverable.md) already claims they are.

# Alternatives considered

**QEMU emulation on x86 runners.** Rejected. A bundle is not just a compile:
`make-bundle.sh` runs `pkg-config`, `ldd`, and `strip` against the target's own
libraries, and `smoke`/`smoke-image` exist precisely to execute the result. Under
emulation the release's central claim — "this runs on a clean target" — would be
tested by an emulator rather than by the architecture, and the GStreamer-heavy
build would run several times slower for the privilege. Native runners are
available and free-tier eligible.

**Cross-compilation with a sysroot.** Rejected more firmly. It would require
assembling an aarch64 GStreamer/GLib sysroot and teaching `make-bundle.sh` to
resolve against it instead of the host, converting a script with no
architecture-specific logic into one that has plenty. It also cannot run what it
produces, so it removes the smoke tests rather than adapting them.

**Self-hosted arm64 runners.** Rejected. It solves a problem this project does
not have (custom hardware, sustained throughput) and adds ones it does not want:
a machine to maintain and secure, and — once this repository is public — a
self-hosted runner reachable from fork pull requests, a well-known foot-gun.

**One fat multi-architecture container manifest via `buildx` + a registry.**
Rejected as a *different deliverable*, not a better one. ADR 0010's container is
deliberately a `docker save` tarball plus a launcher: downloadable from the
release page, loadable with no registry, no login, and no network after download.
A multi-architecture manifest is a registry construct — producing one means
either publishing to a registry (a new distribution channel, new credentials, its
own decision) or hand-assembling a manifest list into a tarball that `docker
load` handles inconsistently across engines. Two clearly-labeled
single-architecture tarballs match what a user downloads: one, for their machine.

**Ship arm64 as bundle-only.** Rejected as an arbitrary asymmetry. The image is
built *from* the bundle (ADR 0010) and costs one extra job leg.

**Move to `ubuntu-24.04` and raise the floor to glibc 2.39.** Rejected, and this
is the alternative that most deserved to win. It is the simplest possible answer
to the deprecation — change two labels, delete the container wiring, done — and
Ubuntu 22.04 leaves standard support in April 2027 anyway, weeks before the
runner image does. What it is not is a CI change: it silently drops every 22.04
user, and it does so as a side effect of a workflow edit rather than as a stated
support decision. The bundle exists so that someone who cannot `apt install`
anything can still convert a flight, and that user is disproportionately on the
older LTS. Dropping them is defensible, but it should be a decision someone makes
on purpose, with release notes — not a consequence of a runner label expiring.

**Keep `ubuntu-22.04` until it breaks.** Rejected. It works until 2026-09-17,
degrades through brownouts that fail releases at unpredictable times, and the
work still has to be done — just later, under time pressure, possibly during a
release.

# Consequences

- **The glibc floor stops depending on a runner label GitHub controls.** This is
  the durable win, and it outlives fork 15: the same wiring survives the 24.04
  and 26.04 deprecations without another decision. The digest fixes which
  distribution release the build runs against, that release's repositories fix
  the available glibc, and the GLIBC symbol check remains what actually enforces
  the result — as it already did when the floor rested on a label.
- **This resolves ADR 0009's open question** about `ubuntu-22.04` availability,
  choosing the `container: ubuntu:22.04` branch it named over dropping 22.04.
  ADR 0009 is not amended — it recorded what was true when taken.
- **The build job's dependency install changes shape.** A bare `ubuntu:22.04`
  container is not a runner image: `sudo` is absent (steps run as root, so it is
  also unnecessary), and `git` plus `git-lfs` must be installed *before*
  `actions/checkout`. Without a usable git, checkout falls back to downloading a
  REST archive — and that path **fails outright** rather than degrading, because
  LFS objects are not in the archive and this repository checks out with
  `lfs: true`.[^checkout-rest-fallback] A loud failure is the better of the two
  outcomes, but it is still a failed release. The smoke job already demonstrates
  the container-job pattern in this workflow, so this is not new ground.
- **The `libunwind-*-dev` workaround is removed deliberately.** It existed
  because the 22.04 *runner image* shipped LLVM's `libunwind-*-dev`, conflicting
  with the `libunwind-dev` that `libgstreamer1.0-dev` needs. Inspection of the
  pinned stock container found no installed libunwind development package, and
  the isolated x86 container run installed `libunwind-dev` cleanly. The old
  `apt-get remove ... || true` therefore does not survive as cargo.
- **Release deliverables double; runner minutes roughly double; wall-clock does
  not.** Both architectures build, smoke, and smoke-image in parallel, so
  wall-clock is bounded by the slower leg rather than by the sum — that leg being
  arm, at 2 vCPU while this repository stays private. Building in a container
  adds a dependency install to both legs that the runner image partly
  preinstalled today, so the increase is that install plus the arm/x86 gap.
- **The smoke tests get stronger, not merely wider.** The bundle smoke job's real
  claim is that nothing leaks in from the host; running it on a second
  architecture is an independent trial of that claim, and a host dependency that
  slipped into the bundle is far likelier to surface as a missing or mismatched
  object on arm than as a subtle bug on x86.
- Documentation now names both architectures: [ADR 0009](./0009-release-bundle.md)
  § Consequences records the expansion, README and the generated usage guides
  tell the user to select the `uname -m` spelling, and PROGRESS distinguishes
  the last x86-only release from the matrixed release workflow.
- A user now picks an architecture at download time. The `-linux-x86_64` /
  `-linux-aarch64` suffixes are the whole of that guidance, so they stay in that
  spelling rather than `-amd64` / `-arm64`: `uname -m` prints the former pair,
  which is what a user checks with.
- The `IR`/thermal and real-footage gaps are untouched. Architecture coverage is
  not evidence coverage.

# Assumptions / open questions

- **arm64 now has native release evidence.** The first matrix run compiled the
  code, enforced the glibc floor, assembled both deliverables, and passed the
  bundle and image smoke suites on `ubuntu-24.04-arm` in
  [`workflow_dispatch` run 32932948339](https://github.com/nitsuga/parrot-to-klv/actions/runs/32932948339).
  The earlier architecture-clean claim is therefore no longer source reading
  alone.
- **The container-build wiring was settled on x86 before the arm leg was
  added.** The isolated first implementation commit passed the whole release
  pipeline through [`workflow_dispatch`](https://github.com/nitsuga/parrot-to-klv/actions/runs/32932383100),
  including bundle and image smoke tests. Only then was the arm matrix added, so
  any failure in its first run has one new cause.
- Job containers on arm64 hosted runners are confirmed for this workflow by the
  same run: the pinned multi-architecture Jammy image initialized natively,
  bootstrapped Git/LFS, reached the mounted host Docker daemon, and completed the
  release build.
- The digest needs a deliberate owner. Nothing moves it, which is the point, but
  nothing prompts anyone to move it either — so the base image ages silently
  while the toolchain installed on top of it keeps updating from Jammy. This is a
  weaker version of the trade ADR 0009 made for the bundle's GStreamer, not the
  same one: there the *shipped* libraries froze, here only the build
  environment's starting point does.
- Ubuntu 22.04 leaves standard support in April 2027. The base image stays
  pullable well beyond that, so this decision does not expire then — but the
  *support commitment* it preserves should be revisited on that date rather than
  inherited indefinitely.
- Whether anyone is asking for arm64 deliverables is not established. The case is
  that the cost is now low and the code is already portable — not that a user
  requested it.

# Citations

- [[0009-release-bundle]] — the relocatable bundle, the glibc floor, the
  x86-64-only statement this amends, and the open question this resolves.
- [[0010-container-deliverable]] — the container as a `docker save` tarball plus
  launcher, which is why a multi-architecture manifest is the wrong shape here.
- [`#75`](https://github.com/nitsuga/parrot-to-klv/issues/75) — the tracking
  issue this decision resolves, and the stable record of fork 15 as it was
  stated while open. [ROADMAP](../../planning/ROADMAP.md) no longer carries it:
  a decided fork leaves the open list for the register.
- GitHub's hosted-runner reference[^gh-hosted-runners] supplied the arm64 labels
  and the public/private resource split — which is what establishes that this
  decision does not wait on the publication work.
- `actions/runner-images#14254`[^ubuntu-2204-deprecation] supplied the
  deprecation and retirement dates. It is the reason the glibc-floor question was
  folded into this fork rather than deferred: the arm leg would otherwise have
  been written against a label already scheduled for removal.
- `actions/checkout` ADR 0153[^checkout-rest-fallback] supplied the REST-fallback
  behavior, correcting a first draft of this ADR that described the no-git case
  as degrading to pointer files rather than failing.
- Ubuntu's `libc6` package page for jammy[^ubuntu-jammy-libc6] is what makes the
  distribution line load-bearing rather than merely plausible: it is the reason
  a moving toolchain cannot raise the floor. It is a policy observation, not a
  guarantee — the symbol check is the guarantee.

[^gh-hosted-runners]: GitHub — *About GitHub-hosted runners* § Supported runners
    and hardware resources: the `ubuntu-22.04-arm` / `ubuntu-24.04-arm` labels,
    and arm64 at 4 vCPU for public repositories against 2 vCPU for private ones.

[^ubuntu-2204-deprecation]: `actions/runner-images#14254` — the `ubuntu-22.04`
    and `ubuntu-22.04-arm` labels begin deprecation 2026-09-17 and are fully
    unsupported after 2027-04-17, with brownout windows that fail jobs in
    between.

[^checkout-rest-fallback]: `actions/checkout` ADR 0153 § Fallback to GitHub API
    — the REST archive path is taken when git is unavailable or too old, and
    "LFS files are not included in the archive. Therefore fail if LFS is set to
    true."

[^ubuntu-jammy-libc6]: Ubuntu — `libc6` in jammy: upstream glibc **2.35**, with
    `jammy-updates` / `jammy-security` carrying Ubuntu revisions
    (`2.35-0ubuntu3`, `2.35-0ubuntu3.14`) of that same upstream release rather
    than a newer one.
