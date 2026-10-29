# brisneylander/gaggimate fork

Personal fork of [jniebuhr/gaggimate](https://github.com/jniebuhr/gaggimate) for a
Rancilio Silvia with a GaggiMate Pro. Its purpose is to build and install my own
firmware over Wi-Fi while staying in sync with upstream.

## What's different from upstream

| File                                 | Change                                                                                                                                                                                                                                                                                          |
| ------------------------------------ | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `src/display/plugins/WebUIPlugin.h`  | `RELEASE_URL` points at this fork's releases, so OTA updates come from here.                                                                                                                                                                                                                    |
| `.github/workflows/fork-release.yml` | Fork-only copy of `build.yml`: builds the firmware on `v*-simon*` tags and publishes a GitHub release here. It leaves out the steps that upload to the official update server.                                                                                                                  |
| Boiler control                       | Lag compensation, flush to target, tank estimate, heater safety layer, machine profiles and the Tune this machine routine: goal, bench observations and how the code addresses them in [docs/boiler-control.md](docs/boiler-control.md); bench data and open items in [ROADMAP.md](ROADMAP.md). |
| `FORK.md`                            | This file.                                                                                                                                                                                                                                                                                      |

Upstream's own `Build`, `Build Nightly` and `Deploy PR Preview` workflows are
**disabled** in this fork's Actions settings, because they need upstream's secrets.

## Releasing a build

1. Make the change on `master` and push.
2. Tag it and push the tag:
   ```sh
   git tag v1.9.0-simon002
   git push origin v1.9.0-simon002
   ```
3. The **Fork Release** workflow builds everything (about 10–15 min) and publishes the release.
4. On the machine: **Settings → System & Updates**, channel **Stable**, then _Update Controller_
   and _Update Display_. The display checks for updates every 30 min, or straight away
   after _Save Channel & Refresh_.

Use the **Stable** channel. Nightly looks for a `nightly` release, which this fork doesn't publish.

## Version numbering

The OTA code (`lib/OTA/src/common.cpp` → `update_required`) installs a release only if its
version is **strictly greater** than what's running. Its semver library treats a
suffixed version as **newer** than the plain one (`1.9.0-simon001 > 1.9.0`), and it
only reads the part up to the first `.` after the patch number.

So tag builds as **`v<current upstream release>-simon<3 digits>`**:

- `v1.9.0-simon001`, `v1.9.0-simon002`, … each installs over the previous one.
  Zero-pad the number: suffixes compare as text, and `simon10` would sort before `simon9`.
- Don't put a dot in the suffix (`-simon.1`). Everything after the dot is ignored.
- When upstream releases a new version (e.g. `v1.9.1`), sync the fork and restart at
  `v1.9.1-simon001`.

## Staying in sync with upstream

On GitHub, use **Sync fork → Update branch** on this repo's front page. That brings in
upstream `master` (= official nightly). The changes here are small, so merges should
normally be clean. After syncing, tag a new `-simonNNN` build to install it.

## Going back to official firmware

- **Over Wi-Fi:** once upstream has a release newer than your build (e.g. `v1.9.1`
  vs `v1.9.0-simon00x`), create a release on this fork with that tag and upload upstream's
  release `.bin` files. The machine installs it, and from then on it follows upstream's releases.
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
