# Claude Code host on the NAS

An always-on container on the OMV NAS that runs Claude Code in Remote Control server mode, so this fork can be worked
on from the Claude app, claude.ai/code or the laptop without the laptop being on. Everything runs on the NAS: firmware
builds, unit tests, releases with OTA updates to the machine, benches, and reading the logger's CSVs.

| File             | What it is                                                                                             |
| ---------------- | ------------------------------------------------------------------------------------------------------ |
| `Dockerfile`     | Node 22 on Debian trixie, PlatformIO, build tools, clang-format, git, GitHub CLI                       |
| `compose.yml`    | The stack: home volume, the logger's logs read-only, 6 GB memory cap, no Docker socket                 |
| `entrypoint.sh`  | Run from the clone: installs Claude Code on first start, then `claude remote-control --spawn worktree` |
| `setup.sh`       | One-time interactive sign-in (Claude, GitHub) and Remote Control's prompts                             |
| `CLAUDE.home.md` | Installed as `~/.claude/CLAUDE.md`: what the container can reach, commands, standing rules             |

## Install

Stacks on the NAS are made in OMV's web UI, not over SSH:

1. **Services → Compose → Files → Add.** Name `claude-code`, paste [`compose.yml`](compose.yml) as the file, and put
   `GM_HOST=<the GaggiMate display's address>` in the Environment field. Save.
2. Select it and press **Up**. The first time, this builds the image from the fork's Dockerfile on GitHub (a few
   minutes), then starts the container, which installs Claude Code into `appdata/claude-code/home` and clones the fork.
3. **Once, by you** (it asks for sign-ins), from a terminal on the NAS or `ssh -t omv`:

   ```bash
   docker exec -it claude-code setup.sh
   ```

   1. **Claude:** opens a sign-in link for your claude.ai account.
   2. **GitHub:** paste a fine-grained token limited to `brisneylander/gaggimate` with Contents and Workflows
      read/write and Actions read (github.com → Settings → Developer settings → Fine-grained tokens).
   3. **Remote Control:** answer `y` to trust the folder and to enable Remote Control, wait for the session URL, then
      Ctrl+C.

4. Restart the stack (**Down**, then **Up**). From then on the server comes up by itself after every restart; sessions
   show up in the Claude app and at claude.ai/code under Remote Control.

`setup.sh` also installs [`CLAUDE.home.md`](CLAUDE.home.md) as `~/.claude/CLAUDE.md` if there isn't one yet.

The image holds only a small bootstrap that clones the fork into `home/gaggimate` on first start and runs
`entrypoint.sh` from there, so changes to the scripts arrive with a `git pull` in the container and a restart. Only a
Dockerfile change needs the stack's **Build**, then **Up**, in OMV. Claude Code itself updates in place in the home
volume.

## Notes

- If the coffee machine is on an isolated network, the firewall rule that lets the NAS reach it (TCP 80) covers this
  container too, since its traffic leaves with the NAS's address.
- The server exits after about 10 minutes without internet (the overnight Wi-Fi drops are the machine's, not the
  NAS's, but a NAS outage would do it); the restart policy brings it back, and sessions can be resumed for about four
  hours after it stopped.
