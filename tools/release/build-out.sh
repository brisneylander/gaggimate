#!/bin/bash
# Rebuild the flashable files in out/ from the checked-out commit, the same way the Fork Release workflow does.
# usage: tools/release/build-out.sh   (from the repo root, clean working tree)
set -euo pipefail
LOG=${LOG:-$HOME/.cache/gm-out}
mkdir -p "$LOG"
[ -z "$(git status --porcelain --untracked-files=no)" ] || { echo "working tree not clean"; exit 1; }
echo "commit $(git rev-parse --short HEAD) on $(git branch --show-current)"

# Web UI, embedded into the display firmware
(cd web && npm ci --no-audit --no-fund && npm run build) > "$LOG/web.log" 2>&1
gzip -f web/dist/assets/*.js web/dist/assets/*.css web/dist/*.html
mkdir -p data/p
python3 -I scripts/embed_webui.py --src web/dist --out src/display/webassets >> "$LOG/web.log" 2>&1
echo "web ui embedded"

# Each env in turn, copying its output straight away: PlatformIO may clean .pio/build between envs.
rm -f out/*.bin out/version.txt
mkdir -p out
pio run -e display > "$LOG/display.log" 2>&1
pio run -t buildfs -e display >> "$LOG/display.log" 2>&1
B=.pio/build/display
cp $B/firmware.bin out/display-firmware.bin
cp $B/partitions.bin out/display-partitions.bin
cp $B/bootloader.bin out/display-bootloader.bin
cp $B/littlefs.bin out/display-filesystem.bin
echo "display built"
pio run -e controller > "$LOG/controller.log" 2>&1
B=.pio/build/controller
cp $B/firmware.bin out/board-firmware.bin
cp $B/partitions.bin out/board-partitions.bin
cp $B/bootloader.bin out/board-bootloader.bin
echo "controller built"

git describe --tags --dirty --match "v*" > out/version.txt
(cd out && sha256sum ./*.bin > SHA256SUMS)
echo "version $(cat out/version.txt)"
