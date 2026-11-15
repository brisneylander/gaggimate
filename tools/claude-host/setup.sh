#!/bin/bash
# One-time interactive setup, run by you (not Claude): docker exec -it claude-code setup.sh
# 1. Sign in to Claude (your claude.ai subscription).  2. Sign in to GitHub with a fine-grained token for the fork.
# 3. Accept Remote Control's prompts once.  Then the container serves sessions by itself after every restart.
set -e
cd "$HOME/gaggimate"

echo "== 1/3 Claude sign-in"
claude auth status >/dev/null 2>&1 || claude auth login

echo "== 2/3 GitHub sign-in (paste a fine-grained token: repository brisneylander/gaggimate, Contents read/write,"
echo "   Workflows read/write, Actions read)"
gh auth status >/dev/null 2>&1 || gh auth login --hostname github.com --git-protocol https
gh auth setup-git

echo "== 3/3 Remote Control: answer y to trust this folder and to enable Remote Control, wait for the URL, then Ctrl+C"
claude remote-control --spawn worktree || true

touch "$HOME/.claude-host-ready"
echo "Done. Restart the container to start serving: docker restart claude-code"
