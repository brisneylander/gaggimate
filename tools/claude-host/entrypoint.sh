#!/bin/bash
# Container start: install Claude Code into the home volume on first run (it updates itself there), clone the fork,
# then serve Remote Control sessions. Until setup.sh has been run once (sign-in and the one-time prompts), it waits.
set -u
REPO_DIR=$HOME/gaggimate

if [ ! -x "$HOME/.local/bin/claude" ]; then
  echo "installing Claude Code"
  curl -fsSL https://claude.ai/install.sh | bash || echo "Claude Code install failed; retrying on next start"
fi

if [ ! -d "$REPO_DIR/.git" ]; then
  git clone -q -b la-simona https://github.com/brisneylander/gaggimate.git "$REPO_DIR" \
    && git -C "$REPO_DIR" remote rename origin fork \
    && git -C "$REPO_DIR" remote add origin https://github.com/jniebuhr/gaggimate.git
fi

if [ ! -f "$HOME/.claude-host-ready" ]; then
  echo "Not set up yet. From the NAS: docker exec -it claude-code setup.sh"
  exec sleep infinity
fi

cd "$REPO_DIR" || exit 1
# Brings back the sessions the last server here was serving (if it stopped less than ~4 h ago), else starts fresh.
# Exits after ~10 min without internet; the restart policy starts it again.
exec claude remote-control --spawn worktree
