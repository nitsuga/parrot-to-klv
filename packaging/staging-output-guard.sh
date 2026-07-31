#!/bin/bash
# Copyright 2026 Eric Schneider
# Licensed under the Apache License, Version 2.0
#
# Shared safety checks for disposable packaging staging directories.
#
# Call staging_output_guard_validate before doing expensive packaging work, then
# staging_output_guard_reset immediately before clearing the directory.

staging_output_guard_canonicalize() {
  realpath -m -- "$1"
}

staging_output_guard_is_same_or_descendant() {
  local path=$1 parent=$2
  [ "$path" = "$parent" ] || [[ "$path" == "$parent"/* ]]
}

staging_output_guard_refuse() {
  echo "refusing unsafe staging output '$1': $2" >&2
  return 1
}

staging_output_guard_validate() {
  local output=$1
  shift

  [ -n "$output" ] || {
    staging_output_guard_refuse "${output:-<empty>}" "path is empty"
    return 1
  }

  local canonical_output canonical_cwd canonical_home canonical_repo canonical_staging
  canonical_output=$(staging_output_guard_canonicalize "$output") || return 1
  canonical_cwd=$(pwd -P) || return 1
  canonical_repo=$(staging_output_guard_canonicalize "$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)") || return 1
  canonical_staging="$canonical_repo/staging"
  if [ -n "${HOME:-}" ]; then
    canonical_home=$(staging_output_guard_canonicalize "$HOME") || return 1
  else
    canonical_home=""
  fi

  [ "$canonical_output" != "/" ] || {
    staging_output_guard_refuse "$output" "path is the filesystem root"
    return 1
  }
  [ "$(dirname -- "$canonical_output")" != "/" ] || {
    staging_output_guard_refuse "$output" "path is a direct child of the filesystem root"
    return 1
  }
  if staging_output_guard_is_same_or_descendant "$canonical_repo" "$canonical_output"; then
    staging_output_guard_refuse "$output" "path is the repository root or one of its ancestors"
    return 1
  fi
  if staging_output_guard_is_same_or_descendant "$canonical_cwd" "$canonical_output"; then
    staging_output_guard_refuse "$output" "path is the current directory or one of its ancestors"
    return 1
  fi
  if [ -n "$canonical_home" ] \
    && staging_output_guard_is_same_or_descendant "$canonical_home" "$canonical_output"; then
    staging_output_guard_refuse "$output" "path is HOME or one of its ancestors"
    return 1
  fi
  if staging_output_guard_is_same_or_descendant "$canonical_output" "$canonical_repo" \
    && ! { [ "$canonical_output" != "$canonical_staging" ] \
      && staging_output_guard_is_same_or_descendant "$canonical_output" "$canonical_staging"; }; then
    staging_output_guard_refuse "$output" "path is inside the repository but not beneath staging/"
    return 1
  fi

  local protected_input canonical_input
  for protected_input in "$@"; do
    canonical_input=$(staging_output_guard_canonicalize "$protected_input") || return 1
    if staging_output_guard_is_same_or_descendant "$canonical_output" "$canonical_input" \
      || staging_output_guard_is_same_or_descendant "$canonical_input" "$canonical_output"; then
      staging_output_guard_refuse "$output" "path overlaps protected input '$protected_input'"
      return 1
    fi
  done
  return 0
}

staging_output_guard_reset() {
  local output=$1
  shift
  staging_output_guard_validate "$output" "$@" || return 1
  rm -rf -- "$output"
  mkdir -p -- "$output"
}
