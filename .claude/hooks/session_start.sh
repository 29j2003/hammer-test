#!/bin/bash
# SessionStart hook for Claude Code cloud sessions: installs the build tools (CMake, Ninja,
# Qt 6, Mesa, Xvfb) from the Ubuntu archive so the editor and its tests build right away.
# Does nothing on local machines.
set -euo pipefail

if [ "${CLAUDE_CODE_REMOTE:-}" != "true" ]; then
  exit 0
fi

need=()
for pkg in cmake ninja-build qt6-base-dev qt6-multimedia-dev libgl1-mesa-dev libgl1-mesa-dri xvfb; do
  dpkg -s "$pkg" > /dev/null 2>&1 || need+=("$pkg")
done
if [ ${#need[@]} -gt 0 ]; then
  apt-get update -qq
  DEBIAN_FRONTEND=noninteractive apt-get install -y -qq --no-install-recommends "${need[@]}" > /dev/null
fi
