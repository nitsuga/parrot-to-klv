#!/bin/bash
# Copyright 2026 Eric Schneider
# Licensed under the Apache License, Version 2.0
#
# Regression coverage for issue #14: a filesystem error mid-scan (here, a
# recording that has vanished between the directory listing and the per-file
# stat -- a dangling symlink reproduces that deterministically, without
# depending on process privileges the way a permission-denied directory
# listing would) used to be silently absorbed into a bogus "file size":
# fs::file_size(p, ec) returns static_cast<uintmax_t>(-1) on error, which the
# unchecked old code then divided and printed as a nonsense multi-terabyte
# figure instead of reporting the real "No such file or directory". This is a
# message-content check (a bare add_test + WILL_FAIL only pins the exit
# code, and the exit code alone does not discriminate here -- both old and
# new code exit non-zero for a scan containing one bad and one good file).

set -euo pipefail

conform=$(realpath -e -- "${1:?usage: conform_stat_failure_test.sh <parrot-conform-binary> <fixtures-dir>}")
fixtures=$(realpath -e -- "${2:?usage: conform_stat_failure_test.sh <parrot-conform-binary> <fixtures-dir>}")

tmp=$(mktemp -d)
trap 'rm -rf -- "$tmp"' EXIT

fail() {
  echo "conform stat-failure test: $*" >&2
  exit 1
}

scan_dir="$tmp/scan"
reports_dir="$tmp/reports"
mkdir -p "$scan_dir"
cp "$fixtures/parrot_v3.mp4" "$scan_dir/real.mp4"
ln -s "$tmp/does-not-exist.mp4" "$scan_dir/ghost.mp4"

log="$tmp/out.log"
if "$conform" --scan "$scan_dir" --reports "$reports_dir" >"$log" 2>&1; then
  fail "unexpectedly succeeded despite one unreadable recording in the scan"
fi

grep -qi 'ghost.mp4: cannot stat' "$log" ||
  fail "did not name the vanished file and report a clear stat failure"
grep -qi 'no such file' "$log" ||
  fail "did not surface the real OS error for the vanished file"
grep -qE '[0-9]{6,} MB' "$log" &&
  fail "printed a bogus multi-megabyte size instead of reporting the stat failure"

grep -q 'real.mp4' "$log" ||
  fail "the vanished file's failure must not stop the scan of the rest of the directory"
[ -n "$(find "$reports_dir" -name '*.json' 2>/dev/null)" ] ||
  fail "the real recording's report should still have been written"

echo "conform stat-failure test: passed"
