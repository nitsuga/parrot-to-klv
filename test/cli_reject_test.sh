#!/bin/bash
# Copyright 2026 Eric Schneider
# Licensed under the Apache License, Version 2.0
#
# Asserts that a CLI command fails with a non-zero exit status AND
# produces the expected diagnostic message on stdout/stderr.
#
# Usage: cli_reject_test.sh <expected-regex> <command...>

set -euo pipefail

pattern="${1:?usage: cli_reject_test.sh <expected-regex> <command...>}"
shift

out=$(mktemp)
trap 'rm -f "$out"' EXIT

set +e
"$@" >"$out" 2>&1
status=$?
set -e

if [ $status -eq 0 ]; then
  echo "FAIL: command unexpectedly succeeded (exit status 0): $*" >&2
  echo "Output was:" >&2
  cat "$out" >&2
  exit 1
fi

if ! grep -qiE -e "$pattern" "$out"; then
  echo "FAIL: command failed with exit status $status, but output did not match '$pattern': $*" >&2
  echo "Output was:" >&2
  cat "$out" >&2
  exit 1
fi

exit 0
