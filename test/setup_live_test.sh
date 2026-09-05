#!/bin/bash
# Copyright 2026 Eric Schneider
# Licensed under the Apache License, Version 2.0

set -euo pipefail

source_dir=$(realpath -e -- "${1:?usage: setup_live_test.sh <source-dir>}")
tmp=$(mktemp -d)
trap 'rm -rf -- "$tmp"' EXIT
stage="$tmp/release"
fakebin="$tmp/bin"
mkdir -p "$stage" "$fakebin"

fail() {
  echo "setup-live test: $*" >&2
  exit 1
}

tag=parrot-to-klv:0.0-test-x86_64
image_file=parrot-to-klv-0.0-test-x86_64-image.tar.gz
sed -e "s|@IMAGE_TAG@|$tag|g" -e "s|@IMAGE_FILE@|$image_file|g" \
  "$source_dir/packaging/setup-live" > "$stage/setup-live"
chmod +x "$stage/setup-live"
sed -e "s|@IMAGE_TAG@|$tag|g" \
  "$source_dir/packaging/compose.yml" > "$stage/compose.yml"
touch "$stage/$image_file"

cat > "$fakebin/docker" <<'EOF'
#!/bin/sh
printf '%s\n' "$*" >> "$DOCKER_LOG"
if [ "$1 $2" = 'image inspect' ]; then
  if [ "${3-}" = --format ]; then
    if [ -f "$DOCKER_STATE" ]; then echo sha256:new; else echo sha256:old; fi
  fi
  exit 0
fi
if [ "$1 $2" = 'image ls' ]; then
  echo 'parrot-to-klv:0.0-test-x86_64'
  echo 'parrot-to-klv:0.0-old-x86_64'
  echo 'parrot-to-klv:0.0-old-aarch64'
  exit 0
fi
if [ "$1" = ps ]; then
  echo 'old123 parrot-to-klv:0.0-old-x86_64 old_ingest_1'
  exit 0
fi
case "$1 $2" in
  'compose version'|'compose config'|'compose up'|'stop --timeout'|'rm old123'|'image rm') exit 0 ;;
  'load -i') touch "$DOCKER_STATE"; exit 0 ;;
  *) exit 1 ;;
esac
EOF
chmod +x "$fakebin/docker"

capture_dir="$tmp/captures"
printf '%s\n' \
  y \
  'rtsp://user:p$ss@host/live' \
  '' \
  "$capture_dir" \
  '' \
  '' \
  '' \
  y \
  y \
  y | PATH="$fakebin:$PATH" DOCKER_LOG="$tmp/docker.log" \
    DOCKER_STATE="$tmp/docker.state" "$stage/setup-live" \
    > "$tmp/output.log"

[ -d "$capture_dir" ] || fail "capture directory was not created"
[ "$(stat -c '%a' "$stage/.env")" = 600 ] || fail ".env is not mode 0600"
grep -Fqx "RTSP_URI='rtsp://user:p\$ss@host/live'" "$stage/.env" ||
  fail "credential characters were not preserved literally"
grep -Fqx "OUTPUT_DIR='$capture_dir'" "$stage/.env" ||
  fail "capture directory was not written"
grep -Fqx "HOST_UID=$(id -u)" "$stage/.env" || fail "caller UID was not written"
grep -Fqx "HOST_GID=$(id -g)" "$stage/.env" || fail "caller GID was not written"
grep -Fqx "load -i $stage/$image_file" "$tmp/docker.log" || fail "image was not reloaded"
grep -Fqx 'compose config' "$tmp/docker.log" || fail "compose was not validated"
grep -Fqx 'stop --timeout 330 old123' "$tmp/docker.log" || fail "old service was not stopped"
grep -Fqx 'rm old123' "$tmp/docker.log" || fail "old service was not removed"
grep -Fqx 'compose up -d --force-recreate' "$tmp/docker.log" || fail "service was not recreated"
grep -Fqx 'image rm parrot-to-klv:0.0-old-x86_64' "$tmp/docker.log" ||
  fail "old image tag was not removed"
if grep -Fqx 'image rm parrot-to-klv:0.0-old-aarch64' "$tmp/docker.log"; then
  fail "upgrade removed the other architecture's image"
fi
grep -Fqx 'image rm sha256:old' "$tmp/docker.log" || fail "replaced image was not removed"

sed -i 's/^IMAGE_TAG=.*/IMAGE_TAG=parrot-to-klv:0.0-old-x86_64/' "$stage/.env"
cp "$stage/.env" "$tmp/env.before"
printf '%s\n' y y y '' n | PATH="$fakebin:$PATH" DOCKER_LOG="$tmp/docker.log" \
  DOCKER_STATE="$tmp/docker.state" "$stage/setup-live" > "$tmp/reuse-output.log"
sed 's/^IMAGE_TAG=.*/IMAGE_TAG=/' "$tmp/env.before" > "$tmp/env.expected"
cmp -s "$tmp/env.expected" "$stage/.env" || fail "upgrade changed retained settings"
