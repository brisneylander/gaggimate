# Claude Code host on the NAS

An always-on container on the OMV NAS that runs Claude Code in Remote Control server mode, so this fork can be worked
on from the Claude app, claude.ai/code or the laptop without the laptop being on. Everything runs on the NAS: firmware
builds, unit tests, releases with OTA updates to the machine, benches, and reading the logger's CSVs.

| File             | What it is                                                                                          |
| ---------------- | --------------------------------------------------------------------------------------------------- |
| `Dockerfile`     | Node 22 on Debian trixie, PlatformIO, build tools, clang-format, git, GitHub CLI                    |
| `compose.yml`    | The stack: home volume, the logger's logs read-only, 6 GB memory cap, no Docker socket              |
| `entrypoint.sh`  | Installs Claude Code on first start, clones the fork, then `claude remote-control --spawn worktree` |
| `setup.sh`       | One-time interactive sign-in (Claude, GitHub) and Remote Control's prompts                          |
| `CLAUDE.home.md` | Installed as `~/.claude/CLAUDE.md`: what the container can reach, commands, standing rules          |

## Install

On the NAS, as `claude`:

```bash
A=/srv/dev-disk-by-uuid-ede720f0-2423-4796-b76c-473aa9f581ae/compose/appdata
git clone -b la-simona https://github.com/brisneylander/gaggimate.git /tmp/gm-src
docker build -t gaggimate-claude:latest /tmp/gm-src/tools/claude-host
mkdir -p $A/claude-code/home/.claude && cp /tmp/gm-src/tools/claude-host/compose.yml $A/claude-code/
cp /tmp/gm-src/tools/claude-host/CLAUDE.home.md $A/claude-code/home/.claude/CLAUDE.md
cd $A/claude-code && docker compose up -d
```

Then, once, by you (it asks for sign-ins):

```bash
docker exec -it claude-code setup.sh
```

1. **Claude:** opens a sign-in link for your claude.ai account.
2. **GitHub:** paste a fine-grained token limited to `brisneylander/gaggimate` with Contents and Workflows read/write
   and Actions read (create it at github.com → Settings → Developer settings → Fine-grained tokens).
3. **Remote Control:** answer `y` to trust the folder and to enable Remote Control, wait for the session URL, then
   Ctrl+C.

Finally `docker restart claude-code`. The server comes up by itself after every restart; sessions show up in the
Claude app and at claude.ai/code under Remote Control.

To update the image after changing these files: rebuild with `docker build`, then `docker compose up -d`. Claude Code
itself updates in place in the home volume.

## Notes

- The coffee machine is on the isolated IoT LAN; the UDR7 policy "OMV -> GaggiMate logger" (NAS → 192.168.20.34,
  TCP 80) covers this container too, since its traffic leaves with the NAS's address.
- The server exits after about 10 minutes without internet (the overnight Wi-Fi drops are the machine's, not the
  NAS's, but a NAS outage would do it); the restart policy brings it back, and sessions can be resumed for about four
  hours after it stopped.
- Register the stack in OMV's Compose page like the logger, using the same compose file, so OMV knows about it.
