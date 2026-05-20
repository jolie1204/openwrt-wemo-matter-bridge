#!/usr/bin/env bash
set -euo pipefail

if [[ ! -d .git ]]; then
  echo "Run this script from the repository root." >&2
  exit 1
fi

if [[ -d ../connectedhomeip ]]; then
  echo "Using sibling connectedhomeip checkout: ../connectedhomeip"
else
  echo "missing ../connectedhomeip" >&2
  echo "Clone project-chip/connectedhomeip next to this repo (../connectedhomeip)." >&2
  exit 1
fi

if [[ -d matter-bridge-app ]]; then
  (cd matter-bridge-app && ln -sfn ../../connectedhomeip connectedhomeip && ln -sfn connectedhomeip/build build && ln -sfn connectedhomeip/build_overrides build_overrides)
  echo "matter-bridge-app symlinks refreshed."
fi
