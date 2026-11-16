#!/usr/bin/env python3
"""Install this fork's firmware on a GaggiMate over USB.

Only needed once: stock GaggiMate updates only from upstream, so the first install of this fork has to be over USB.
After that the machine updates over Wi-Fi from this fork's releases (Settings -> System & Updates).

    pip install esptool
    python3 flash.py                 # display, then controller, from the latest La Simona release
    python3 flash.py --nightly       # the nightly pre-release instead
    python3 flash.py display         # just one board: display, headless or controller

Downloads the release's .bin files from GitHub, reads the flash layout from each board's partitions.bin and runs
esptool. It also blanks the otadata partition, so a board that last booted from its second app slot after a Wi-Fi
update starts the newly flashed firmware instead of the old one.

Flashing the display replaces its filesystem, which can hold your profiles: export them first.

Use at your own risk: unofficial, experimental firmware, provided as is with no warranty of any kind. The author takes
no responsibility for any damage to your machine, its boards or anything else, for injury, or for any other loss.
Unplug the machine from the wall before opening it or connecting anything inside it.
"""

import argparse
import json
import shutil
import struct
import subprocess
import sys
import tempfile
import urllib.request
from pathlib import Path

REPO = "brisneylander/gaggimate"
API = f"https://api.github.com/repos/{REPO}/releases"

BOOTLOADER_OFFSET = 0x0  # ESP32-S3
PARTITIONS_OFFSET = 0x8000

# Release asset prefix and whether the board gets a filesystem image.
BOARDS = {
    "display": ("display", True),
    "headless": ("display-headless", True),
    "controller": ("board", False),
}


def fetch_release(tag):
    url = f"{API}/latest" if tag is None else f"{API}/tags/{tag}"
    req = urllib.request.Request(url, headers={"Accept": "application/vnd.github+json"})
    with urllib.request.urlopen(req) as resp:
        return json.load(resp)


def download(release, names, dest):
    assets = {a["name"]: a["browser_download_url"] for a in release["assets"]}
    for name in names:
        if name not in assets:
            sys.exit(f"Release {release['tag_name']} has no {name}.")
        target = dest / name
        if target.exists():
            continue
        print(f"  downloading {name}")
        with urllib.request.urlopen(assets[name]) as resp, open(target, "wb") as out:
            shutil.copyfileobj(resp, out)


def read_partitions(path):
    """Return {label: (type, subtype, offset, size)} from an ESP-IDF partition table binary."""
    data = path.read_bytes()
    table = {}
    for i in range(0, len(data), 32):
        entry = data[i : i + 32]
        if entry[:2] != b"\xaa\x50":
            break
        ptype, subtype, offset, size = struct.unpack("<BBII", entry[2:12])
        label = entry[12:28].rstrip(b"\0").decode(errors="replace")
        table[label] = (ptype, subtype, offset, size)
    if not table:
        sys.exit(f"{path.name} doesn't look like a partition table.")
    return table


def find(table, ptype, subtypes, what):
    for ptype_, subtype, offset, size in table.values():
        if ptype_ == ptype and subtype in subtypes:
            return offset, size
    sys.exit(f"No {what} partition in the partition table.")


def esptool_cmd():
    try:
        import esptool  # noqa: F401
    except ImportError:
        sys.exit("esptool isn't installed. Run: pip install esptool")
    return [sys.executable, "-m", "esptool"]


def flash_board(board, files, port, dry_run):
    prefix, has_fs = BOARDS[board]
    parts = files / f"{prefix}-partitions.bin"
    table = read_partitions(parts)

    app_offset, _ = find(table, 0, {0x10}, "app0")
    ota_offset, ota_size = find(table, 1, {0x00}, "otadata")
    blank = files / f"blank-{ota_size:x}.bin"
    blank.write_bytes(b"\xff" * ota_size)

    images = [
        (BOOTLOADER_OFFSET, files / f"{prefix}-bootloader.bin"),
        (PARTITIONS_OFFSET, parts),
        (ota_offset, blank),
        (app_offset, files / f"{prefix}-firmware.bin"),
    ]
    if has_fs:
        fs_offset, _ = find(table, 1, {0x82, 0x83}, "filesystem")
        images.append((fs_offset, files / f"{prefix}-filesystem.bin"))

    cmd = esptool_cmd() if not dry_run else ["esptool"]
    cmd += ["--chip", "esp32s3"]
    if port:
        cmd += ["--port", port]
    cmd += ["write_flash"]
    for offset, path in images:
        cmd += [hex(offset), str(path)]

    print(f"\nFlashing the {board}:")
    print("  " + " ".join(cmd))
    if dry_run:
        return
    if subprocess.run(cmd).returncode != 0:
        sys.exit(
            f"\nFlashing the {board} failed. Check it's plugged in with a data cable. If it still won't connect, hold "
            "BOOT while plugging it in, or pass --port."
        )


def main():
    parser = argparse.ArgumentParser(description="Install this fork's firmware on a GaggiMate over USB.")
    parser.add_argument(
        "boards",
        nargs="*",
        metavar="board",
        help="display, headless and/or controller (default: display then controller)",
    )
    group = parser.add_mutually_exclusive_group()
    group.add_argument("--tag", help="release tag to install (default: the latest La Simona release)")
    group.add_argument("--nightly", action="store_true", help="install the nightly pre-release")
    group.add_argument("--dir", type=Path, help="use .bin files already in this folder instead of downloading")
    parser.add_argument("--port", help="serial port, e.g. /dev/ttyACM0, /dev/cu.usbmodem101 or COM3")
    parser.add_argument("--dry-run", action="store_true", help="show the esptool commands without running them")
    args = parser.parse_args()

    boards = args.boards or ["display", "controller"]
    for board in boards:
        if board not in BOARDS:
            parser.error(f"unknown board {board!r}: choose display, headless or controller")
    if "display" in boards and "headless" in boards:
        parser.error("choose display or headless, not both")

    if not args.dry_run:
        esptool_cmd()

    tmp = None
    if args.dir:
        files = args.dir
    else:
        tag = "nightly" if args.nightly else args.tag
        release = fetch_release(tag)
        print(f"Release: {release.get('name') or release['tag_name']} ({release['tag_name']})")
        tmp = tempfile.TemporaryDirectory(prefix="gaggimate-")
        files = Path(tmp.name)
        names = []
        for board in boards:
            prefix, has_fs = BOARDS[board]
            names += [f"{prefix}-{part}.bin" for part in ("bootloader", "partitions", "firmware")]
            if has_fs:
                names.append(f"{prefix}-filesystem.bin")
        download(release, names, files)

    try:
        for i, board in enumerate(boards):
            if not args.dry_run:
                swap = f"Unplug the {boards[i - 1]}, plug" if i > 0 else "Plug"
                input(f"\n{swap} in the {board} by USB, then press Enter. ")
            flash_board(board, files, args.port, args.dry_run)
    finally:
        if tmp:
            tmp.cleanup()

    print(
        "\nDone. On the machine, open Settings -> System & Updates and pick the Stable or Nightly channel; "
        "updates from now on come over Wi-Fi."
    )


if __name__ == "__main__":
    main()
