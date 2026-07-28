---
type: Decision
title: ADR 0009 — Ship a relocatable bundle, not a container and not a static binary
description: Build on the oldest supported Ubuntu and ship the tool with private copies of GStreamer and the eight plugins it uses, so a user extracts a tarball and runs it with nothing installed
status: accepted
fork: 9
tags: [decision, packaging, release, gstreamer, ci, phase-3]
timestamp: 2026-07-28T10:45:00Z
---

# Context

The tool is finished enough to use, and using it currently means building it —
CMake, a compiler, GStreamer dev packages, and a deploy key for a private
dependency. That is a reasonable ask of a contributor and an unreasonable one of
someone who just wants to convert a flight.

The constraints, as given:

- **Ubuntu 22.04 and 24.04**, x86-64. (20.04 was in scope briefly and dropped —
  no known systems still on it, and it costs a container and a second compiler.)
- **Assume nothing is installed.** Not GStreamer, not its plugins. The user may
  not be able to `apt install` anything.
- **Avoid Docker**, which the user already knew would work and did not want the
  overhead of.

The hard part is GStreamer: a plugin architecture, loaded at runtime from a
system path, needing a scanner binary and a writable registry cache. The tool
also carries a private static dependency (libmisbklv) and a C++ runtime.

# Decision

**Ship a relocatable directory tree, tarred, built on the oldest supported
release.** The user extracts it anywhere and runs the wrapper. Nothing is
installed, nothing needs root, nothing touches the system GStreamer.

```
parrot-to-klv-<version>-linux-x86_64/
  parrot-to-klv                       wrapper: sets the GStreamer variables
  bin/parrot-to-klv                   rpath $ORIGIN/../lib
  lib/*.so*                           28 libraries
  lib/gstreamer-1.0/*.so              8 plugins, named individually
  libexec/gstreamer-1.0/gst-plugin-scanner
  THIRD-PARTY.md
```

16 MB, ~7 MB compressed. `packaging/make-bundle.sh` assembles it by walking the
`ldd` closure of the binary, the scanner *and* each plugin — plugins pull
libraries the binary never references, so walking the binary alone silently
misses them.

Four things make it relocatable, and each is load-bearing:

- **Built on 22.04.** glibc compatibility runs forward only, so the oldest target
  is the only correct build host. CI checks the resulting binary imports nothing
  above `GLIBC_2.35` rather than trusting the runner label.
- **`-static-libstdc++ -static-libgcc`.** Nothing else in the process uses C++ —
  every bundled library and every GStreamer plugin is C — so no C++ ABI crosses
  the `dlopen` boundary and there is no second runtime to disagree with. This
  removes the host's libstdc++ from the compatibility surface and drops 2.2 MB.
- **rpath `$ORIGIN/../lib`**, so the binary finds its own libraries even when the
  wrapper is bypassed.
- **The wrapper replaces the GStreamer search path** rather than adding to it —
  `GST_PLUGIN_SYSTEM_PATH`, the scanner, a private registry cache, and
  `GST_PLUGIN_PATH` unset. A process that loaded some plugins from the bundle and
  some from the host would be worse than one that failed, because the version
  mismatch surfaces as a crash somewhere unrelated.

**The plugin list is explicit**, not "copy plugins-bad". That package carries
patent-encumbered and oddly-licensed elements this tool has no use for; the eight
named plugins are LGPL. Shipping only what is used is both smaller and a licence
position we can state.

**Libraries are linked dynamically**, which satisfies the LGPL's relinking right
directly: replace any `lib/*.so*` with your own build of the same SONAME.
`THIRD-PARTY.md` says so and points at the sources.

# Alternatives considered

## Docker

**Rejected by the constraint**, and rightly. But worth separating two things that
sound alike: this decision still *builds* in CI on a GitHub runner, and the smoke
test runs in a bare `ubuntu:24.04` container. Using a container as a build and
test environment is invisible to the user; requiring one at runtime is the
overhead being avoided.

## Static everything, including GStreamer

**Rejected, though it is not the crazy idea it sounds like** — meson's `gst-full`
builds a monolithic static GStreamer with a chosen plugin set. Two reasons. It is
a large, fragile build to own. And LGPL relinking becomes genuinely hard: with
dynamic bundling a user swaps a `.so`; with static linking we would owe object
files or a relink path. Dynamic bundling is both less work and the cleaner
licence position.

## AppImage

The obvious "nicer UX" answer — one file, and `linuxdeploy` even has a GStreamer
plugin.

**Rejected for this constraint.** Type-2 AppImages need FUSE2, and 22.04/24.04
ship FUSE3, so the user would have to `apt install libfuse2` — exactly the
assumption we were told we cannot make. `--appimage-extract-and-run` works around
it, but handing someone a binary that needs a magic flag is worse than handing
them a directory. Reconsider as a convenience alongside the tarball, never
instead of it.

## Require the user to install GStreamer

**Rejected by the constraint**, and it is the option the constraint exists to
rule out: matching plugin sets across two Ubuntu releases is exactly the problem
being solved.

# Consequences

- **This forced a real fix upstream.** With only the plugins this tool uses,
  conversion failed: `parsebin` decides a stream is fully parsed by asking
  whether any *decoder* in the registry accepts its caps — one it never
  instantiates. Every ordinary system has one, so the dependency was invisible
  until packaging made it visible. The decoders that satisfy it are `openh264`
  (H.264 patents), `libde265` and `faad2` (GPL) — precisely what an Apache-2.0
  distribution should not carry to have it sit unused in a registry. libmisbklv's
  ADR 0025 replaced parsebin with an explicit demuxer instead. Packaging found a
  latent defect that would have hit any minimal deployment.
- The bundle is **x86-64 only** and CI can only prove **24.04**. 22.04 rests on
  the glibc floor, which is a sound argument and not a test. Real coverage would
  need a self-hosted runner.
- The container list the tool can read is now libmisbklv's explicit table
  (MP4/MOV, MPEG-TS, Matroska) rather than whatever GStreamer could autoplug.
  Irrelevant for Parrot MP4s; worth knowing.
- The bundle pins a *build-time* GStreamer version. Security updates to the
  host's GStreamer do not reach it — a rebuild does. That is the cost of not
  depending on the system, and it should be stated in release notes rather than
  discovered.
- `make-bundle.sh` must run on a machine with GStreamer installed; it copies from
  the system. It is a packaging script, not a cross-build.

# Assumptions / open questions

- Assumes the eight plugins remain sufficient. They are empirically derived, not
  guessed — but a future feature (a new container, a new codec) can add a
  requirement, and the failure mode is a runtime "missing plugin" that only the
  smoke test would catch. That is why the smoke test converts rather than merely
  running `--help`.
- Assumes `ubuntu-22.04` remains available as a GitHub runner. It is deprecated
  eventually; when it goes, the choice is a `container: ubuntu:22.04` build (the
  20.04 plan, resurrected) or dropping 22.04.
- The version string comes from the tag, and nothing verifies it matches anything
  in the source. There is no version in the code to check it against yet.
- Untested: whether a user's inherited `GST_*` environment can still interfere.
  The wrapper replaces the paths it knows about and unsets `GST_PLUGIN_PATH`,
  but the variable surface is large.

# Citations

- `packaging/make-bundle.sh` — the closure walk, the plugin list and its reasons
- `.github/workflows/release.yml` — the build/smoke/publish split
- [[0007-h265-vs-st0604-generation]] — the H.265 path the bundle also carries
- libmisbklv ADR 0025 — the parsebin dependency this work uncovered
