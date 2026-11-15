# Always-on Claude Code host for this fork, on the OMV NAS: firmware builds, unit tests, releases, OTA updates and
# benches, driven from the Claude app or claude.ai/code through Remote Control. See README.md.
FROM node:22-trixie

ARG UID=1003
ARG GID=100

# Build tools (PlatformIO needs python3-venv; the native unit tests need a host C++ compiler), git and the GitHub CLI.
RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      build-essential clang-format curl ca-certificates git gh jq less openssh-client procps \
      python3 python3-pip python3-venv tmux tzdata unzip \
 && rm -rf /var/lib/apt/lists/*

# PlatformIO Core in its own venv; toolchains and packages go under $HOME/.platformio (a volume), so they survive
# image rebuilds.
# Nanopb's generator needs protobuf and grpcio-tools in the same venv; PlatformIO installs more on demand, so the venv
# belongs to the claude user.
RUN python3 -m venv /opt/pio && /opt/pio/bin/pip install --no-cache-dir platformio protobuf grpcio-tools \
 && ln -s /opt/pio/bin/pio /usr/local/bin/pio && ln -s /opt/pio/bin/platformio /usr/local/bin/platformio \
 && chown -R ${UID}:${GID} /opt/pio

# The node image ships a "node" user on 1000; ours matches the NAS's claude user (1003:users) so files written to
# the volumes belong to it on the host.
RUN useradd -m -u ${UID} -g ${GID} -s /bin/bash claude \
 && echo 'case ":$PATH:" in *":$HOME/.local/bin:"*) ;; *) PATH="$HOME/.local/bin:$PATH" ;; esac' > /etc/profile.d/local-bin.sh
# (the home volume hides /etc/skel's .profile, which would normally add ~/.local/bin for login shells)

# Fetched by URL rather than COPY, so the image builds from this Dockerfile's URL alone (OMV's Compose UI, no git or
# checkout on the NAS). BRANCH picks which branch's scripts.
ARG BRANCH=la-simona
ADD --chmod=755 https://raw.githubusercontent.com/brisneylander/gaggimate/${BRANCH}/tools/claude-host/entrypoint.sh /usr/local/bin/entrypoint.sh
ADD --chmod=755 https://raw.githubusercontent.com/brisneylander/gaggimate/${BRANCH}/tools/claude-host/setup.sh /usr/local/bin/setup.sh

USER claude
ENV HOME=/home/claude \
    PATH=/home/claude/.local/bin:/usr/local/bin:/usr/bin:/bin \
    TZ=Australia/Brisbane \
    LANG=C.UTF-8
WORKDIR /home/claude
ENTRYPOINT ["/usr/local/bin/entrypoint.sh"]
