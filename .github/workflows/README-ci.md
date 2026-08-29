# CI setup — no secrets required

`build.yml` and `release.yml` fetch **libmisbklv** through `FetchContent` in
`CMakeLists.txt`, pinned to an exact commit. Since libmisbklv became public on
2026-08-25 the clone goes over anonymous HTTPS
(`https://github.com/nitsuga/libmisbklv.git`) and needs no credential at all.

Neither workflow reads a secret. The automatic `GITHUB_TOKEN` is enough for
everything else they do, and a fork's pull request builds without any setup —
which was not true before.

The regular build and sanitizer jobs use two repository-scoped runners labeled
`parrot-to-klv` for trusted pushes and same-repository pull requests. Each
normal job claims one of two shared host capacity slots and builds with three
processes, so both parrot jobs can run together without exceeding the host
budget even if a libmisbklv job overlaps. Fork pull requests use
`ubuntu-latest`, so untrusted code never reaches the persistent runners.

Both regular jobs run the public, commit-tagged
`ghcr.io/nitsuga/libmisbklv-ci` image; the tag in `build.yml` is deliberately
pinned rather than `latest`. The image supplies the compiler, CMake, ccache,
Git LFS, and GStreamer development and runtime packages, so these jobs do not
run apt against the runner. The lock files live in the existing shared
`/home/eric/ci-cache` mount; hosted fork jobs use an ephemeral `/tmp` path.

## What this replaces

While libmisbklv was private, `GITHUB_TOKEN` could not clone it, so each of the
three build jobs ran an "Authorize the private dependency" step that installed a
read-only deploy key from the `MISBKLV_DEPLOY_KEY` secret and rewrote
`~/.ssh/config` to use it. `GIT_REPOSITORY` was `git@github.com:` accordingly.

All of that is gone: the three steps, the secret, and the deploy key on
libmisbklv. A deploy key was the right shape for the problem — read access to
exactly one repository, where a personal key carries whatever its owner can
reach — but the problem no longer exists, and a secret that any workflow can
read is worth not having.

The rejected alternatives are recorded for the same reason. A fine-grained PAT
plus a `url.<https>.insteadOf` rewrite also worked and avoided SSH, but a PAT is
account-scoped and expires. Vendoring or a submodule would have removed the
credential problem too, at the cost of the exact-commit pin in `CMakeLists.txt`
being the single record of which libmisbklv this project builds against — that
pin survives the transport change unaltered.

## If libmisbklv ever goes private again

Restore the deploy key per the history of this file (`git log -p --
.github/workflows/README-ci.md`) and switch `GIT_REPOSITORY` back to
`git@github.com:`. Nothing else in this repository depends on the transport.
