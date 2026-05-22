#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 2)}"
TARGETS="rpi4"
PACKAGE_ONLY=0
RESTORE_CONFIG=1

usage() {
  cat <<'EOF'
Usage: scripts/release/build_rpi_release.sh [options]

Options:
  --target rpi4|rpi5|all   Raspberry Pi target to build (default: rpi4)
  --package-only           Build packages only, not full firmware images
  --keep-config            Leave generated .config selected after build
  -j N                     Build parallelism

Environment:
  JOBS=N                   Build parallelism
  VERSION=vX.Y.Z           Version string used in staged release filenames
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --target)
      TARGETS="$2"
      shift 2
      ;;
    --package-only)
      PACKAGE_ONLY=1
      shift
      ;;
    --keep-config)
      RESTORE_CONFIG=0
      shift
      ;;
    -j)
      JOBS="$2"
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown option: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
done

case "$TARGETS" in
  rpi4) TARGET_LIST="rpi4" ;;
  rpi5) TARGET_LIST="rpi5" ;;
  all) TARGET_LIST="rpi4 rpi5" ;;
  *) echo "Unsupported target: $TARGETS" >&2; exit 2 ;;
esac

cd "$ROOT_DIR"

BACKUP_CONFIG=""
if [[ -f .config ]]; then
  BACKUP_CONFIG="$(mktemp .config.release-backup.XXXXXX)"
  cp .config "$BACKUP_CONFIG"
fi

cleanup() {
  if [[ "$RESTORE_CONFIG" -eq 1 && -n "$BACKUP_CONFIG" && -f "$BACKUP_CONFIG" ]]; then
    mv "$BACKUP_CONFIG" .config
  elif [[ "$RESTORE_CONFIG" -eq 1 && -z "$BACKUP_CONFIG" ]]; then
    rm -f .config
  elif [[ -n "$BACKUP_CONFIG" && -f "$BACKUP_CONFIG" ]]; then
    rm -f "$BACKUP_CONFIG"
  fi
}
trap cleanup EXIT

write_rpi4_config() {
  cat > .config <<'EOF'
CONFIG_TARGET_bcm27xx=y
CONFIG_TARGET_bcm27xx_bcm2711=y
CONFIG_TARGET_bcm27xx_bcm2711_DEVICE_rpi-4-wemo-matter-bridge=y
CONFIG_PACKAGE_openwemo-bridge-core=y
CONFIG_PACKAGE_wemo-matter-bridge=y
CONFIG_PACKAGE_wemo-mtd-data=y
CONFIG_PACKAGE_wemo-rootfs-resize=y
CONFIG_PACKAGE_qrencode=y
CONFIG_PACKAGE_wireguard-tools=y
CONFIG_PACKAGE_sqlite3-cli=y
EOF
}

write_rpi5_config() {
  cat > .config <<'EOF'
CONFIG_TARGET_bcm27xx=y
CONFIG_TARGET_bcm27xx_bcm2712=y
CONFIG_TARGET_bcm27xx_bcm2712_DEVICE_rpi-5-wemo-matter-bridge=y
CONFIG_PACKAGE_openwemo-bridge-core=y
CONFIG_PACKAGE_wemo-matter-bridge=y
CONFIG_PACKAGE_wemo-mtd-data=y
CONFIG_PACKAGE_wemo-rootfs-resize=y
CONFIG_PACKAGE_qrencode=y
CONFIG_PACKAGE_wireguard-tools=y
CONFIG_PACKAGE_sqlite3-cli=y
EOF
}

./scripts/feeds update -a
./scripts/feeds install -a

for target in $TARGET_LIST; do
  echo "==> Configuring $target"
  "write_${target}_config"
  make defconfig

  if [[ "$PACKAGE_ONLY" -eq 1 ]]; then
    echo "==> Building bridge packages for $target"
    make -j"$JOBS" package/network/services/openwemo-bridge-core/compile
    make -j"$JOBS" package/network/services/wemo-matter-bridge/compile
    make -j"$JOBS" package/network/services/wemo-rootfs-resize/compile
  else
    echo "==> Building OpenWrt image for $target"
    make -j"$JOBS"
  fi
done

"$ROOT_DIR/scripts/release/package_release_assets.sh"
