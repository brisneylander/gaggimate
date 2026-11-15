#!/bin/bash
# After a release tag is pushed: wait for the "Fork Release" workflow to publish it, then update the machine over
# the air (display first, then controller) and check both boards report the new version.
# usage: tools/release/fork-release.sh <tag, e.g. v1.9.0-lasimona009> [host]
# Needs curl, jq and node 22. No GitHub login: the fork's releases are public.
set -euo pipefail
TAG=${1:?usage: fork-release.sh <tag> [host]}
HOST=${2:-${GM_HOST:-gaggimate.local}}
REPO=brisneylander/gaggimate
DIR=$(cd "$(dirname "$0")" && pwd)
t() { date +%H:%M:%S; }

echo "$(t) waiting for release $TAG"
n=0
for _ in $(seq 1 90); do # up to 30 min; the workflow usually takes ~10
  n=$(curl -fsS "https://api.github.com/repos/$REPO/releases/tags/$TAG" 2>/dev/null | jq '.assets | length' 2>/dev/null || echo 0)
  [ "${n:-0}" -gt 0 ] && break
  sleep 20
done
[ "${n:-0}" -gt 0 ] || { echo "$(t) release $TAG has no assets after 30 min"; exit 1; }
sleep 60 # let the last uploads land
echo "$(t) release $TAG published, $(curl -fsS "https://api.github.com/repos/$REPO/releases/tags/$TAG" | jq '.assets | length') assets"

echo "$(t) OTA display"
node "$DIR/gmota.mjs" display "$TAG" "$HOST"
echo "$(t) OTA controller"
node "$DIR/gmota.mjs" controller "$TAG" "$HOST"
echo "$(t) updated"
