#!/bin/bash
# Copyright 2026 Eric Schneider
# Licensed under the Apache License, Version 2.0
#
# Build the container image from an already-assembled bundle, and package it as
# the second release deliverable: the exported image, the launcher, and usage.
#
#   ./packaging/make-image.sh <bundle-dir> <version> <architecture> <staging-dir>
#
# The bundle is the input on purpose — one build feeds both deliverables, so
# they cannot describe different binaries. See ADR 0010.

set -euo pipefail

usage='make-image.sh <bundle-dir> <version> <architecture> <staging-dir>'
BUNDLE=${1:?usage: $usage}
VERSION=${2:?usage: $usage}
ARCH=${3:?usage: $usage}
OUT=${4:?usage: $usage}

case "$ARCH" in
  x86_64|aarch64) ;;
  *) echo "unsupported release architecture: $ARCH" >&2; exit 2 ;;
esac

here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
source "$here/staging-output-guard.sh"
staging_output_guard_validate "$OUT" "$BUNDLE"
engine=$(command -v docker || command -v podman) || {
  echo "neither docker nor podman on PATH" >&2; exit 1; }

tag="parrot-to-klv:$VERSION-$ARCH"
image_file="parrot-to-klv-$VERSION-$ARCH-image.tar.gz"

# Build in a scratch context so the Dockerfile's COPY finds the bundle at a
# fixed path regardless of where the caller staged it.
ctx=$(mktemp -d)
trap 'rm -rf -- "$ctx"' EXIT
cp -r "$BUNDLE" "$ctx/bundle"
cp "$here/Dockerfile" "$ctx/Dockerfile"
"$engine" build -t "$tag" -f "$ctx/Dockerfile" "$ctx"

staging_output_guard_reset "$OUT" "$BUNDLE"

# -9 because this is a file people download by hand, and the extra seconds here
# are paid once against everyone's bandwidth.
"$engine" save "$tag" | gzip -9 > "$OUT/$image_file"

# The launcher, with the image's identity baked in so it cannot drift from the
# tarball beside it.
sed -e "s|@IMAGE_TAG@|$tag|g" -e "s|@IMAGE_FILE@|$image_file|g" \
    "$here/parrot-to-klv" > "$OUT/parrot-to-klv"
chmod +x "$OUT/parrot-to-klv"

sed -e "s|@VERSION@|$VERSION|g" -e "s|@ARCH@|$ARCH|g" \
    "$here/usage-head-docker.md" > "$OUT/USAGE.md"
printf '\n' >> "$OUT/USAGE.md"
cat "$here/usage-common.md" >> "$OUT/USAGE.md"

# The compose file for long-running live sessions, with this release's tag as
# the IMAGE_TAG default (overridable in .env) — the same bake-in the launcher
# above carries. USAGE.md needs no extra step: its compose section lives in
# usage-head-docker.md, which the lines above already assemble.
sed -e "s|@IMAGE_TAG@|$tag|g" \
    "$here/compose.yml" > "$OUT/compose.yml"

sed -e "s|@VERSION@|$VERSION|g" -e "s|@ARCH@|$ARCH|g" -e "s|@IMAGE_TAG@|$tag|g" \
    "$here/.env.example" > "$OUT/.env.example"

cp "$BUNDLE/THIRD-PARTY.md" "$OUT/THIRD-PARTY.md"

(cd "$OUT" && sha256sum "$image_file" > "$image_file.sha256")

echo "image:  $tag"
du -sh "$OUT" | sed 's/^/deliverable: /'
