# GaggiMate, La Simona edition

Stock [GaggiMate](https://github.com/jniebuhr/gaggimate) firmware with one visible change: the standby screen says
**La Simona** instead of GaggiMate. It is built from upstream unchanged, and the machine updates from this repository's
releases.

## How it works

The [La Simona workflow](.github/workflows/la-simona.yml) runs once a day (3 am Brisbane time) and builds only when
upstream has changed:

| Upstream               | Built here as             | Update channel |
| ---------------------- | ------------------------- | -------------- |
| A new release `vX.Y.Z` | Release `vX.Y.Z-vanilla`  | Stable         |
| `master` moved         | The `nightly` pre-release | Nightly        |

Each build ([la-simona-build.yml](.github/workflows/la-simona-build.yml)) checks out upstream at that release or commit,
adds the La Simona logo (from the `STANDBY_LOGO_C_GZ_B64` secret, kept out of the repository) and points the update
address (`RELEASE_URL`) at this repository, then runs upstream's build steps. Nothing else changes.

**Build now:** Actions → La Simona → Run workflow (tick a force box to rebuild something that already exists).

## Versions

The firmware installs only newer versions. Builds are tagged `vX.Y.Z-vanilla`, and a nightly reports
`vX.Y.Z-vanilla-<commits>-g<hash>`. "vanilla" sorts after the earlier `simonNNN` and `thermaltuneNNN` builds, so a
machine on one of those updates to the first vanilla build over Wi-Fi.

## The earlier fork

The Thermal Tune boiler-control work (lag compensation, flush to target, tank estimate, heater safety layer, machine
profiles) is kept in the [`thermal-tune`](../../tree/thermal-tune) branch.

## Licence

GaggiMate is licensed [CC BY-NC-SA 4.0](https://creativecommons.org/licenses/by-nc-sa/4.0/); these builds are too. Not
affiliated with the GaggiMate project.
