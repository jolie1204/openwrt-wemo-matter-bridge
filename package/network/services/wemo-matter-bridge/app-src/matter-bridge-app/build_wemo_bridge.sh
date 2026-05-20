#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"

if [[ ! -d connectedhomeip ]]; then
  echo "missing sibling path: ../../connectedhomeip" >&2
  exit 1
fi

CHIP_ROOT="$(cd ../../connectedhomeip && pwd -P)"
PATCH_FILE="$(pwd -P)/patches/connectedhomeip/unit-localization-startup.patch"

ensure_chip_submodules() {
  if [[ "${WEMO_SKIP_CHIP_SUBMODULE_BOOTSTRAP:-0}" == "1" ]]; then
    return 0
  fi

  # Fresh CHIP checkouts commonly fail here because activate.sh expects Pigweed.
  if [[ ! -f "${CHIP_ROOT}/third_party/pigweed/repo/pw_env_setup/util.sh" ]]; then
    echo "Bootstrapping required CHIP submodules for Linux bridge build..."
    git -C "${CHIP_ROOT}" submodule update --init --recursive \
      third_party/pigweed/repo \
      third_party/jsoncpp/repo \
      third_party/nlassert/repo \
      third_party/nlio/repo \
      third_party/mbedtls/repo \
      third_party/boringssl/repo/src
  fi
}

ensure_chip_submodules

if [[ -f "$PATCH_FILE" ]]; then
  if git -C "$CHIP_ROOT" apply --check "$PATCH_FILE" >/dev/null 2>&1; then
    echo "Applying local CHIP patch: unit-localization-startup.patch"
    git -C "$CHIP_ROOT" apply "$PATCH_FILE"
  elif git -C "$CHIP_ROOT" apply --reverse --check "$PATCH_FILE" >/dev/null 2>&1; then
    echo "CHIP patch already present: unit-localization-startup.patch"
  else
    echo "WARNING: CHIP patch not applicable for this connectedhomeip revision; skipping: unit-localization-startup.patch" >&2
  fi
fi

HOME=/tmp /bin/bash "${CHIP_ROOT}/scripts/examples/gn_build_example.sh" . out/ethernet

echo "Built: $(pwd)/out/ethernet/wemo-bridge-app"
