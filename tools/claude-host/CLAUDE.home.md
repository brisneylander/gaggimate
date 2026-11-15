# Claude Code on the OMV NAS (claude-code container)

You are running inside the `claude-code` container on the owner's OMV NAS, reached through Remote
Control from the Claude app, claude.ai/code or the laptop. This file is `~/.claude/CLAUDE.md`; it was installed from
`tools/claude-host/CLAUDE.home.md` in the gaggimate repo, so update that copy too when something here changes.

## What's here

- The fork: `~/gaggimate` (branch `la-simona`). Remotes: `fork` = brisneylander/gaggimate (push here), `origin` =
  upstream jniebuhr/gaggimate. Each Remote Control session gets its own git worktree of it.
- The coffee machine (GaggiMate Pro on a Rancilio Silvia V4): the display's address is in `$GM_HOST` (set in the
  stack's environment file), websocket `ws://$GM_HOST/ws`, HTTP `/api/settings`, `/api/status`, `/api/history`. It
  may sit on an isolated network where only port 80 from the NAS is let through.
- The logger's output, read-only: `/logs` (daily `YYYY-MM-DD.csv`, `settings-YYYY-MM-DD.json`, `valve-timing.csv`).
  The logger itself runs in its own stack (`gaggimate-logger`); this container can't restart it.
- No Docker socket and no SSH into the NAS host: other stacks are out of reach on purpose. If something needs the host
  (restarting the logger, OMV config), ask the owner to do it.

## Commands

- Firmware: `pio run -e display`, `pio run -e controller`, `pio run -e display-headless`.
- Unit tests: `pio test -e native` (or one suite: `pio test -e native -f test_flush_to_target`).
- C++ formatting: `clang-format -i <files>`. Web: `cd web && npx prettier --write <files>`.
- Release, after the owner says "release" and the tag `v1.9.0-lasimonaNNN` is pushed to `fork`:
  `tools/release/fork-release.sh <tag>` (waits for the Fork Release workflow, then OTA display then controller, and
  checks both report the tag). Run it in the background; it takes ~15 min. OTA only installs newer versions; the
  version parser keeps three dot-separated parts, so use suffixes like `008r1`, never `008.1`.
- Benches: `node scripts/bench/<script>.mjs` directly (they used to run as separate node containers).

## Standing rules (from the owner)

- Ask before releasing; release only when asked. Commit or push only when asked ("do it" / "release it" has covered
  commit and push).
- Draft Discord text only, never post it. Never enter passwords or tokens; the owner signs in.
- Don't change the favourite profiles without asking.
- Every GitHub Pages page has a "Last updated" footer (stamped by fork-pages.yml), and `docs/boiler-control.md`'s
  footer date is updated by hand on each edit.
- During the soak (from 2026-11-06), hold behaviour changes to the boiler control unless asked.
