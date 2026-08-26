#!/bin/bash
# Copyright 2026 Eric Schneider
# Licensed under the Apache License, Version 2.0
#
# Regression coverage for the release-staging deletion guard. Every pathname
# this test creates is beneath one mktemp directory; the rejection cases never
# call reset on anything outside it.

set -euo pipefail

source_dir=$(realpath -e -- "${1:?usage: packaging_output_guard_test.sh <source-dir>}")
guard="$source_dir/packaging/staging-output-guard.sh"
bundle_script="$source_dir/packaging/make-bundle.sh"
image_script="$source_dir/packaging/make-image.sh"

tmp=$(mktemp -d)
trap 'rm -rf -- "$tmp"' EXIT

fail() {
  echo "packaging output guard test: $*" >&2
  exit 1
}

expect_refusal() {
  local name=$1
  shift
  if "$@" >"$tmp/$name.log" 2>&1; then
    fail "$name unexpectedly succeeded"
  fi
  grep -Eqi 'refus|unsafe' "$tmp/$name.log" ||
    fail "$name did not explain that the output was unsafe"
}

expect_accept() {
  local name=$1
  shift
  "$@" >"$tmp/$name.log" 2>&1 || fail "$name unexpectedly failed"
}

mkdir -p "$tmp/current" "$tmp/home" "$tmp/protected/input" "$tmp/artifacts"
cd "$tmp/current"
HOME="$tmp/home"
export HOME
source "$guard"

# These paths are deliberately only validated: no test action can remove the
# real root, /tmp, current directory, or HOME.
expect_refusal root staging_output_guard_validate /
expect_refusal root_child staging_output_guard_validate /tmp
expect_refusal current staging_output_guard_validate .
expect_refusal current_lexical staging_output_guard_validate "$tmp/current/../current"
expect_refusal home staging_output_guard_validate "$HOME"
expect_refusal protected_parent staging_output_guard_validate "$tmp/protected" "$tmp/protected/input"
expect_refusal protected_child staging_output_guard_validate "$tmp/protected/input/nested" "$tmp/protected/input"

# Source the helper through a disposable repository-shaped symlink.  Its
# BASH_SOURCE-based repository check must reject the fake checkout itself and
# every non-staging child without ever consulting or changing the real checkout.
fake_repo="$tmp/fake-repo"
mkdir -p "$fake_repo/packaging" "$fake_repo/staging"
ln -s "$guard" "$fake_repo/packaging/staging-output-guard.sh"
source "$fake_repo/packaging/staging-output-guard.sh"

expect_refusal fake_repo_root staging_output_guard_validate "$fake_repo"
expect_refusal fake_repo_child staging_output_guard_validate "$fake_repo/not-staging"
expect_refusal fake_repo_lexical staging_output_guard_validate "$fake_repo/staging/../not-staging"
ln -s "$fake_repo" "$tmp/fake-repo-alias"
expect_refusal fake_repo_symlink staging_output_guard_validate "$tmp/fake-repo-alias/not-staging"
expect_refusal fake_repo_staging_itself staging_output_guard_validate "$fake_repo/staging"
expect_accept fake_staging staging_output_guard_validate "$fake_repo/staging/artifact"
expect_accept temp_artifact staging_output_guard_validate "$tmp/artifacts/nested/artifact"

# reset is allowed only for an output under this test's temporary tree.
safe_output="$fake_repo/staging/reset-me"
mkdir -p "$safe_output/old"
touch "$safe_output/old/sentinel"
staging_output_guard_reset "$safe_output"
[ -d "$safe_output" ] || fail "reset did not recreate the safe output directory"
[ -z "$(find "$safe_output" -mindepth 1 -print -quit)" ] ||
  fail "reset did not clear the safe output directory"

# reset must refuse and preserve files when protected inputs overlap.
unsafe_reset="$fake_repo/staging/unsafe-reset"
unsafe_input="$unsafe_reset/protected-input"
mkdir -p "$unsafe_input"
touch "$unsafe_reset/sentinel" "$unsafe_input/input-sentinel"
expect_refusal reset_overlaps_protected_input staging_output_guard_reset "$unsafe_reset" "$unsafe_input"
[ -f "$unsafe_reset/sentinel" ] || fail "reset deleted output despite protected input overlap"
[ -f "$unsafe_input/input-sentinel" ] || fail "reset deleted protected input"

# The scripts must reject before pkg-config or a container engine is touched.
# Their outputs overlap disposable protected inputs, and the sentinels prove no
# deletion happened before the refusal.
bundle_output="$tmp/bundle-output"
bundle_build="$bundle_output/build-input"
image_bundle="$tmp/image-bundle"
image_output="$image_bundle/unsafe-output"
mkdir -p "$bundle_build" "$image_output"
touch "$bundle_output/sentinel" "$image_output/sentinel"

expect_refusal bundle_contains_input "$bundle_script" "$bundle_build" "$bundle_output"
[ -f "$bundle_output/sentinel" ] || fail "make-bundle removed its unsafe output"

expect_refusal image_overlaps_input "$image_script" "$image_bundle" 0.0-test x86_64 "$image_output"
[ -f "$image_output/sentinel" ] || fail "make-image removed its unsafe output"
