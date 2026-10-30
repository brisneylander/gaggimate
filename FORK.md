# brisneylander/gaggimate fork

Personal fork of [jniebuhr/gaggimate](https://github.com/jniebuhr/gaggimate) for a
Rancilio Silvia with a GaggiMate Pro. Its purpose is to build and install my own
firmware over Wi-Fi while staying in sync with upstream.

## What's different from upstream

| File                                        | Change                                                                                                                                                                                                                                                                                          |
| ------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `src/display/plugins/WebUIPlugin.h`         | `RELEASE_URL` points at this fork's releases, so OTA updates come from here.                                                                                                                                                                                                                    |
| `.github/workflows/fork-release.yml`        | Fork-only copy of `build.yml`: builds the firmware on `v*-thermaltune*` (and the older `v*-simon*`) tags and publishes a GitHub release here. It leaves out the steps that upload to the official update server.                                                                                |
| `.github/workflows/fork-nightly.yml`        | Same build once a day if `master` has moved (3 am Brisbane), published as the `nightly` pre-release for the Nightly update channel.                                                                                                                                                             |
| `site/`, `.github/workflows/fork-pages.yml` | The fork's front page, published to GitHub Pages at https://brisneylander.github.io/gaggimate/ on pushes that change it.                                                                                                                                                                        |
| Boiler control                              | Lag compensation, flush to target, tank estimate, heater safety layer, machine profiles and the Tune this machine routine: goal, bench observations and how the code addresses them in [docs/boiler-control.md](docs/boiler-control.md); bench data and open items in [ROADMAP.md](ROADMAP.md). |
| `FORK.md`                                   | This file.                                                                                                                                                                                                                                                                                      |

Upstream's own `Build`, `Build Nightly` and `Deploy PR Preview` workflows are
**disabled** in this fork's Actions settings, because they need upstream's secrets.

## Releasing a build

Releases are called **Thermal Tune N** (from 34; 1–33 were tagged `simonNNN`).

1. Make the change on `master` and push.
2. Tag it and push the tag:
   ```sh
   git tag v1.9.0-thermaltune035
   git push fork v1.9.0-thermaltune035
   ```
3. The **Fork Release** workflow builds everything (about 10–15 min) and publishes the release. Set its title to
   **Thermal Tune 35**.
4. On the machine: **Settings → System & Updates**, channel **Stable**, then _Update Controller_
   and _Update Display_. The display checks for updates every 30 min, or straight away
   after _Save Channel & Refresh_.

**Stable** installs this fork's tagged releases. **Nightly** installs the head of `master`: the
**Fork Nightly** workflow (`.github/workflows/fork-nightly.yml`) rebuilds the `nightly` pre-release once a day
at 3 am Brisbane time, only if `master` has moved; **Actions → Fork Nightly → Run workflow** builds it now. Building
on every push tripped GitHub's Actions limit for forks (2026-10-30). A head build reports e.g. `v1.9.0-thermaltune034-3-g6bb7704`,
newer than thermaltune034 and older than thermaltune035, so going back to Stable picks up the next release.
**Upstream** installs the official GaggiMate release, even an older one (see below).

## Version numbering

The OTA code (`lib/OTA/src/common.cpp` → `update_required`) installs a release only if its
version is **strictly greater** than what's running (the Upstream channel excepted). Its semver library treats a
suffixed version as **newer** than the plain one (`1.9.0-thermaltune034 > 1.9.0`), compares suffixes letter by
letter with runs of digits compared as numbers, and only reads the part up to the first `.` after the patch number.

So tag builds as **`v<current upstream release>-thermaltune<3 digits>`**:

- `v1.9.0-thermaltune034`, `v1.9.0-thermaltune035`, … each installs over the previous one.
- A new suffix must sort **after** the old one letter by letter: "thermaltune" follows "simon" because t > s, so
  machines on simonNNN take thermaltune034 as newer. A name starting with a–s (e.g. "silvia": sil < sim) would never be
  offered to them.
- Don't put a dot in the suffix (`-thermaltune.35`). Everything after the dot is ignored.
- When upstream releases a new version (e.g. `v1.9.1`), sync the fork and continue at
  `v1.9.1-thermaltune<next number>`.

## Staying in sync with upstream

On GitHub, use **Sync fork → Update branch** on this repo's front page. That brings in
upstream `master` (= official nightly). The changes here are small, so merges should
normally be clean. After syncing, tag a new `-thermaltuneNNN` build to install it.

## Going back to official firmware

- **Over Wi-Fi:** choose the **Upstream** update channel under Settings → System & Updates, save, then update the
  controller and the display. It installs upstream's latest release even when its version is lower, and from then on
  the machine follows upstream's releases. Coming back to this fork afterwards needs a USB flash.
- **Over USB (always works):** flash upstream's release `.bin` files (bootloader, partitions,
  firmware and filesystem for both the display and the controller board) with esptool or
  the official web flasher. Export profiles first.

## Licence

GaggiMate is licensed CC BY-NC-SA 4.0, and this fork is too. It's for personal, non-commercial use.

## Private standby logo

The fork's standby logo is not in this repository. The release workflow (`fork-release.yml`) decodes the
`STANDBY_LOGO_C_GZ_B64` Actions secret into `src/display/ui/default/eez/images/ui_image_logo.c` before building, and
marks the file `skip-worktree` so the firmware version stays clean. Without the secret the stock logo is built.

To change it: generate the 392x53 `LV_IMG_CF_ALPHA_8BIT` `ui_image_logo.c`, then
`gzip -9 -c ui_image_logo.c | base64 -w0 | gh secret set STANDBY_LOGO_C_GZ_B64 -R brisneylander/gaggimate` and tag a
release. Never commit the generated file or its PNG sources. The firmware binaries attached to releases are public and
contain the image.
