#!/bin/bash
# Copyright 2026 Eric Schneider
# Licensed under the Apache License, Version 2.0
#
# Assemble a relocatable bundle: the tool plus private copies of every shared
# library and GStreamer plugin it needs, so it runs on a machine with no
# GStreamer installed.
#
# Build it on the OLDEST supported release (22.04). glibc compatibility runs
# forward only, so a binary built against 2.35 runs on 24.04, and one built on
# 24.04 does not run on 22.04. That is the whole reason this script does not
# just run wherever.
#
#   ./packaging/make-bundle.sh <build-dir> <staging-dir>
#
# See context/decisions/0009-release-bundle.md for why this shape.

set -euo pipefail

BUILD_DIR=${1:?usage: make-bundle.sh <build-dir> <staging-dir>}
OUT=${2:?usage: make-bundle.sh <build-dir> <staging-dir>}

# The plugins a conversion actually loads. Deliberately a list rather than "copy
# plugins-bad": that package carries patent-encumbered and oddly-licensed
# elements this tool has no use for, and shipping them would be someone else's
# problem to explain. Every entry here is LGPL.
#
#   coreelements      queue, filesrc/filesink, the plumbing
#   typefindfunctions sniffing the container, before a demuxer is chosen
#   app               appsrc — how the KLV branch is pushed in
#   isomp4            qtdemux, the MP4 side
#   mpegtsmux         the MPEG-TS the tool exists to write
#   videoparsersbad   h264parse / h265parse
#
# Derived by removing each and checking every video fixture still converts, not
# by reading the pipeline. It was eight until libmisbklv stopped using parsebin
# (its ADR 0025): `playback` was there only to provide parsebin, and
# `audioparsers` only because parsebin insisted on parsing an audio pad the
# muxer then dropped. A demuxer exposes that pad and we send it to a fakesink,
# so nothing needs to understand AAC any more.
#
# Re-derive this list when the pipeline changes. A plugin that is no longer
# needed is dead weight; one that is newly needed fails at runtime, and only the
# release smoke test would catch it.
PLUGINS=(
  coreelements typefindfunctions app
  isomp4 mpegtsmux videoparsersbad
)

# Never bundle these. They are the ABI floor of the host, and shipping our own
# copy of the C library alongside the host's dynamic loader is how you get
# something that segfaults in a way nobody can debug.
EXCLUDE_RE='/(libc|libm|libpthread|libdl|librt|libresolv|ld-linux[^/]*)\.so'

plugin_dir=$(pkg-config --variable=pluginsdir gstreamer-1.0)

# Debian and Ubuntu put the scanner under the multiarch libdir, while
# pkg-config's pluginscannerdir reports upstream's /usr/libexec — which does not
# exist here. Try what pkg-config says, then where the distro actually puts it,
# then look for it. Without the scanner GStreamer silently finds no plugins at
# all, so this is worth being thorough about.
scanner=""
for candidate in \
  "$(pkg-config --variable=pluginscannerdir gstreamer-1.0)/gst-plugin-scanner" \
  "$(pkg-config --variable=libdir gstreamer-1.0)/gstreamer1.0/gstreamer-1.0/gst-plugin-scanner" \
  "$(dirname "$plugin_dir")/gstreamer1.0/gstreamer-1.0/gst-plugin-scanner"
do
  [ -x "$candidate" ] && { scanner=$candidate; break; }
done
if [ -z "$scanner" ]; then
  scanner=$(find /usr/lib /usr/libexec -name gst-plugin-scanner -type f 2>/dev/null | head -1)
fi
[ -n "$scanner" ] && [ -x "$scanner" ] || {
  echo "gst-plugin-scanner not found; is libgstreamer1.0-0 installed?" >&2; exit 1; }
echo "scanner: $scanner"

rm -rf "$OUT"
mkdir -p "$OUT/bin" "$OUT/lib/gstreamer-1.0" "$OUT/libexec/gstreamer-1.0"

cp "$BUILD_DIR/parrot-to-klv" "$OUT/bin/"
cp "$scanner" "$OUT/libexec/gstreamer-1.0/"
for p in "${PLUGINS[@]}"; do
  src="$plugin_dir/libgst$p.so"
  [ -f "$src" ] || { echo "missing plugin: $src" >&2; exit 1; }
  cp "$src" "$OUT/lib/gstreamer-1.0/"
done

# The dependency closure, taken from everything that will actually be loaded:
# the binary, the scanner, and each plugin. Plugins pull in libraries the binary
# never references directly (libgstvideo via videoparsersbad, say), so walking
# the binary alone is not enough.
{
  ldd "$OUT/bin/parrot-to-klv"
  ldd "$OUT/libexec/gstreamer-1.0/gst-plugin-scanner"
  for so in "$OUT"/lib/gstreamer-1.0/*.so; do ldd "$so"; done
} | awk '/=> \// {print $3}' | sort -u | grep -Ev "$EXCLUDE_RE" > "$OUT/.closure"

# Copy by SONAME, dereferencing: the loader asks for libfoo.so.0, and shipping a
# dangling symlink to a versioned file we did not copy is a silent failure that
# only shows up on the target machine.
while read -r lib; do
  [ -n "$lib" ] || continue
  soname=$(objdump -p "$lib" 2>/dev/null | awk '/SONAME/ {print $2; exit}')
  cp -L "$lib" "$OUT/lib/${soname:-$(basename "$lib")}"
done < "$OUT/.closure"
rm -f "$OUT/.closure"

chmod -R u+w "$OUT/lib" "$OUT/libexec"
# NO_STRIP=1 keeps symbols, for when a bundle misbehaves and the stripping is
# the thing you want to rule out.
if [ "${NO_STRIP:-0}" != "1" ]; then
  strip --strip-unneeded "$OUT"/lib/*.so* "$OUT"/lib/gstreamer-1.0/*.so \
        "$OUT/libexec/gstreamer-1.0/gst-plugin-scanner" 2>/dev/null || true
fi

# The launcher. Everything here is about making GStreamer look inside the bundle
# and nowhere else — a half-bundled process that loads some plugins from the
# host is worse than one that fails outright, because the version mismatch
# surfaces as a crash somewhere unrelated.
cat > "$OUT/parrot-to-klv" <<'WRAPPER'
#!/bin/sh
# Launcher for the relocatable bundle. Run it from anywhere; it locates its own
# libraries and GStreamer plugins relative to itself.
set -eu

HERE=$(CDPATH= cd -- "$(dirname -- "$(readlink -f -- "$0")")" && pwd)

# The binary carries -rpath $ORIGIN/../lib, so it finds its own libraries even
# if this wrapper is bypassed. LD_LIBRARY_PATH is still needed for the plugins
# and the scanner: those are dlopen'd, and the scanner is a separate process.
LD_LIBRARY_PATH="$HERE/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export LD_LIBRARY_PATH

# Replace the system plugin path rather than adding to it. The *_1_0 variants
# take precedence over the unsuffixed ones, so both are set.
GST_PLUGIN_SYSTEM_PATH="$HERE/lib/gstreamer-1.0"
GST_PLUGIN_SYSTEM_PATH_1_0="$GST_PLUGIN_SYSTEM_PATH"
GST_PLUGIN_SCANNER="$HERE/libexec/gstreamer-1.0/gst-plugin-scanner"
GST_PLUGIN_SCANNER_1_0="$GST_PLUGIN_SCANNER"
export GST_PLUGIN_SYSTEM_PATH GST_PLUGIN_SYSTEM_PATH_1_0
export GST_PLUGIN_SCANNER GST_PLUGIN_SCANNER_1_0

# GST_PLUGIN_PATH is additive, so a value inherited from the user's shell would
# pull host plugins into this process alongside ours. Drop it.
unset GST_PLUGIN_PATH GST_PLUGIN_PATH_1_0

# The registry cache must be writable and must not collide with a system
# GStreamer's, which describes a different set of plugins entirely.
cache=${XDG_CACHE_HOME:-${HOME:-/tmp}/.cache}/parrot-to-klv
mkdir -p "$cache" 2>/dev/null || cache=$(mktemp -d)
GST_REGISTRY="$cache/registry.bin"
GST_REGISTRY_1_0="$GST_REGISTRY"
export GST_REGISTRY GST_REGISTRY_1_0

exec "$HERE/bin/parrot-to-klv" "$@"
WRAPPER
chmod +x "$OUT/parrot-to-klv"

# Usage, assembled from the shared body plus this deliverable's invocation
# section — so the substance cannot drift between the tarball and the image.
here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
cat "$here/usage-head-bundle.md" > "$OUT/USAGE.md"
printf '\n' >> "$OUT/USAGE.md"
cat "$here/usage-common.md" >> "$OUT/USAGE.md"

cat > "$OUT/THIRD-PARTY.md" <<'NOTICE'
# Bundled third-party libraries

This bundle ships GStreamer, GLib and their dependencies under `lib/`, so the
tool runs without installing them. Those libraries are licensed under the **GNU
Lesser General Public License, version 2.1 or later** (see
<https://www.gnu.org/licenses/lgpl-2.1.html>); their sources are available from
<https://gstreamer.freedesktop.org/src/> and from Ubuntu's `apt-get source`.

They are linked **dynamically** and shipped as ordinary shared objects, so the
LGPL's relinking right is satisfied directly: replace any `lib/*.so*` with your
own build of the same SONAME and the tool will use it.

`parrot-to-klv` itself is Apache-2.0. The C++ runtime is linked statically under
the GCC Runtime Library Exception.
NOTICE

du -sh "$OUT" | sed 's/^/bundle size: /'
echo "libraries: $(find "$OUT/lib" -maxdepth 1 -name '*.so*' | wc -l)"
echo "plugins:   $(find "$OUT/lib/gstreamer-1.0" -name '*.so' | wc -l)"
