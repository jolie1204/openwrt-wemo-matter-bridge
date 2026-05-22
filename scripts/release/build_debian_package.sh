#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
VERSION="${VERSION:-$(git -C "$ROOT_DIR" describe --tags --always --dirty 2>/dev/null || echo snapshot)}"
PKG_VERSION="${VERSION#v}"
ARCH="${ARCH:-$(dpkg --print-architecture 2>/dev/null || uname -m)}"
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 2)}"
OUT_DIR="${OUT_DIR:-$ROOT_DIR/bin/debian}"
WORK_DIR="${WORK_DIR:-$ROOT_DIR/build_dir/debian/openwemo-matter-bridge-$PKG_VERSION-$ARCH}"
CHIP_ROOT="${CHIP_ROOT:-}"
SKIP_MATTER_BUILD=0
KEEP_WORK=0
WORK_DIR_EXPLICIT=0

OPENWEMO_PKG="$ROOT_DIR/package/network/services/openwemo-bridge-core"
BRIDGE_PKG="$ROOT_DIR/package/network/services/wemo-matter-bridge"
DEBIAN_FILES="$ROOT_DIR/scripts/debian"

usage() {
  cat <<'EOF'
Usage: scripts/release/build_debian_package.sh [options]

Builds a native Raspberry Pi OS / Debian package. Run this on Raspberry Pi OS
64-bit for the public arm64 package.

Options:
  --version VERSION        Package version, e.g. v0.1.2 or 0.1.2
  --chip-root PATH         Use an existing connectedhomeip checkout
  --work-dir PATH          Build working directory
  --out-dir PATH           Output directory (default: bin/debian)
  --skip-matter-build      Reuse an already built wemo-bridge-app in work dir
  --keep-work              Do not delete/recreate the work dir
  -j N                     Build parallelism
  -h, --help               Show this help

Environment:
  VERSION                  Package version
  CHIP_ROOT                Existing connectedhomeip checkout
  JOBS                     Build parallelism
  OUT_DIR                  Output directory
  WORK_DIR                 Build working directory
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --version)
      VERSION="$2"
      PKG_VERSION="${VERSION#v}"
      shift 2
      ;;
    --chip-root)
      CHIP_ROOT="$(cd "$2" && pwd -P)"
      shift 2
      ;;
    --work-dir)
      WORK_DIR="$2"
      WORK_DIR_EXPLICIT=1
      shift 2
      ;;
    --out-dir)
      OUT_DIR="$2"
      shift 2
      ;;
    --skip-matter-build)
      SKIP_MATTER_BUILD=1
      shift
      ;;
    --keep-work)
      KEEP_WORK=1
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

if [[ "$WORK_DIR_EXPLICIT" -eq 0 ]]; then
  WORK_DIR="$ROOT_DIR/build_dir/debian/openwemo-matter-bridge-$PKG_VERSION-$ARCH"
fi

require_cmd() {
  command -v "$1" >/dev/null 2>&1 || {
    echo "Missing required command: $1" >&2
    exit 1
  }
}

require_cmd dpkg-deb
require_cmd git
require_cmd make
require_cmd patch
require_cmd rsync

if [[ "$ARCH" != "arm64" ]]; then
  echo "WARNING: building architecture '$ARCH'. Public Raspberry Pi OS releases should be built on arm64." >&2
fi

CHIP_URL="$(awk -F:= '/^PKG_SOURCE_URL:=/ { print $2; exit }' "$BRIDGE_PKG/Makefile")"
CHIP_REV="$(awk -F:= '/^PKG_SOURCE_VERSION:=/ { print $2; exit }' "$BRIDGE_PKG/Makefile")"
CHIP_DATE="$(awk -F:= '/^PKG_SOURCE_DATE:=/ { print $2; exit }' "$BRIDGE_PKG/Makefile")"
BRIDGE_RELEASE="$(awk -F:= '/^PKG_RELEASE:=/ { print $2; exit }' "$BRIDGE_PKG/Makefile")"
OPENWEMO_RELEASE="$(awk -F:= '/^PKG_RELEASE:=/ { print $2; exit }' "$OPENWEMO_PKG/Makefile")"

mkdir -p "$OUT_DIR"
if [[ "$KEEP_WORK" -eq 0 ]]; then
  rm -rf "$WORK_DIR"
fi
mkdir -p "$WORK_DIR/src" "$WORK_DIR/pkg"

SRC_ROOT="$WORK_DIR/src"
OPENWEMO_BUILD="$SRC_ROOT/openwemo-bridge-core"
BRIDGE_BUILD="$SRC_ROOT/wemo-matter-bridge"
CHIP_BUILD="$SRC_ROOT/connectedhomeip"
PKG_DIR="$WORK_DIR/pkg/openwemo-matter-bridge"

echo "==> Preparing openwemo-bridge-core"
rm -rf "$OPENWEMO_BUILD"
rsync -a "$OPENWEMO_PKG/src/" "$OPENWEMO_BUILD/"
install -m 0644 "$OPENWEMO_PKG/files/atomic_compat.h" "$OPENWEMO_BUILD/atomic_compat.h"
install -m 0644 "$OPENWEMO_PKG/files/ithread.h" "$OPENWEMO_BUILD/wemo_ctrl/ithread.h"
for p in "$OPENWEMO_PKG"/patches/*.patch; do
  [[ -f "$p" ]] || continue
  (cd "$OPENWEMO_BUILD" && patch -p1 --forward --silent < "$p") || {
    if (cd "$OPENWEMO_BUILD" && patch -p1 --reverse --dry-run --silent < "$p" >/dev/null 2>&1); then
      echo "Patch already applied: $(basename "$p")"
    else
      echo "Failed to apply patch: $p" >&2
      exit 1
    fi
  }
done

echo "==> Building openwemo-bridge-core"
make -C "$OPENWEMO_BUILD" clean >/dev/null 2>&1 || true
make -C "$OPENWEMO_BUILD" -j"$JOBS" \
  CC="${CC:-cc}" \
  CFLAGS="${CFLAGS:-} -O2 -g -include $OPENWEMO_BUILD/atomic_compat.h" \
  RPATH_FLAGS=

if [[ "$SKIP_MATTER_BUILD" -eq 0 ]]; then
  echo "==> Preparing Matter bridge source"
  rm -rf "$BRIDGE_BUILD"
  rsync -a "$BRIDGE_PKG/app-src/" "$BRIDGE_BUILD/"

  if [[ -n "$CHIP_ROOT" ]]; then
    rm -rf "$CHIP_BUILD"
    ln -s "$CHIP_ROOT" "$CHIP_BUILD"
  elif [[ ! -d "$CHIP_BUILD/.git" ]]; then
    echo "==> Cloning connectedhomeip $CHIP_REV"
    git clone "$CHIP_URL" "$CHIP_BUILD"
    git -C "$CHIP_BUILD" checkout "$CHIP_REV"
  fi

  for p in "$BRIDGE_PKG"/patches/*.patch; do
    [[ -f "$p" ]] || continue
    if git -C "$CHIP_BUILD" apply --check "$p" >/dev/null 2>&1; then
      echo "Applying CHIP patch: $(basename "$p")"
      git -C "$CHIP_BUILD" apply "$p"
    elif git -C "$CHIP_BUILD" apply --reverse --check "$p" >/dev/null 2>&1; then
      echo "CHIP patch already present: $(basename "$p")"
    else
      echo "WARNING: CHIP patch not applicable, skipping: $p" >&2
    fi
  done

  ln -sfn ../../connectedhomeip "$BRIDGE_BUILD/matter-bridge-app/connectedhomeip"

  echo "==> Building Matter bridge app"
  (
    cd "$BRIDGE_BUILD/matter-bridge-app"
    WEMO_CHIP_STATE_DIR=/var/lib/wemo-matter-bridge/chip \
    WEMO_ENGINE_RPATH=/usr/lib \
    HOME=/tmp \
    ./build_wemo_bridge.sh
  )
fi

BRIDGE_BIN="$BRIDGE_BUILD/matter-bridge-app/out/ethernet/wemo-bridge-app"
[[ -x "$BRIDGE_BIN" ]] || {
  echo "Missing built Matter bridge binary: $BRIDGE_BIN" >&2
  exit 1
}

echo "==> Staging Debian package"
rm -rf "$PKG_DIR"
install -d \
  "$PKG_DIR/DEBIAN" \
  "$PKG_DIR/etc/default" \
  "$PKG_DIR/etc/systemd/system" \
  "$PKG_DIR/etc/wemo-matter-bridge" \
  "$PKG_DIR/usr/libexec/wemo-matter-bridge" \
  "$PKG_DIR/usr/lib" \
  "$PKG_DIR/usr/sbin" \
  "$PKG_DIR/var/lib/wemo-matter-bridge/chip"

install -m 0755 "$BRIDGE_BIN" "$PKG_DIR/usr/sbin/wemo-bridge-app"
install -m 0755 "$OPENWEMO_BUILD/wemo_ctrl/wemo_ctrl" "$PKG_DIR/usr/sbin/wemo_ctrl"
install -m 0755 "$OPENWEMO_BUILD/wemo_client/wemo_client" "$PKG_DIR/usr/sbin/wemo_client"
install -m 0644 "$OPENWEMO_BUILD/wemo_engine/libwemoengine.so" "$PKG_DIR/usr/lib/libwemoengine.so"
install -m 0755 "$BRIDGE_PKG/files/wemo-matter-bridge" "$PKG_DIR/usr/sbin/wemo-matter-bridge"
install -m 0755 "$DEBIAN_FILES/start-bridge" "$PKG_DIR/usr/libexec/wemo-matter-bridge/start-bridge"
install -m 0644 "$DEBIAN_FILES/wemo-matter-bridge.default" "$PKG_DIR/etc/default/wemo-matter-bridge"
install -m 0644 "$DEBIAN_FILES/wemo_ctrl.conf" "$PKG_DIR/etc/wemo_ctrl.conf"
install -m 0644 "$DEBIAN_FILES/wemo_ctrl.service" "$PKG_DIR/etc/systemd/system/wemo_ctrl.service"
install -m 0644 "$DEBIAN_FILES/wemo-matter-bridge.service" "$PKG_DIR/etc/systemd/system/wemo-matter-bridge.service"

cat > "$PKG_DIR/DEBIAN/control" <<EOF
Package: openwemo-matter-bridge
Version: $PKG_VERSION
Architecture: $ARCH
Maintainer: openwemo-matter-bridge maintainers
Section: net
Priority: optional
Depends: libc6, libstdc++6, libatomic1, libsqlite3-0, libupnp13 | libupnp17, libssl3, iproute2, qrencode, systemd
Description: WeMo LAN to Matter bridge for Raspberry Pi OS
 Bridges legacy Belkin WeMo LAN devices into Matter as bridged endpoints.
 This package generates unique Matter onboarding credentials on first start.
Built-From-OpenWemo-Release: $OPENWEMO_RELEASE
Built-From-CHIP-Revision: $CHIP_REV
Built-From-CHIP-Date: $CHIP_DATE
Built-From-Bridge-Release: $BRIDGE_RELEASE
EOF

cat > "$PKG_DIR/DEBIAN/conffiles" <<'EOF'
/etc/default/wemo-matter-bridge
/etc/wemo_ctrl.conf
EOF

cat > "$PKG_DIR/DEBIAN/postinst" <<'EOF'
#!/bin/sh
set -e

install -d -m 0700 -o root -g root /var/lib/wemo-matter-bridge /var/lib/wemo-matter-bridge/chip /etc/wemo-matter-bridge
ldconfig || true

if command -v systemctl >/dev/null 2>&1; then
	systemctl daemon-reload || true
	systemctl enable wemo_ctrl.service wemo-matter-bridge.service >/dev/null 2>&1 || true
	if [ "${WEMO_DEB_NO_START:-0}" != "1" ]; then
		systemctl restart wemo_ctrl.service wemo-matter-bridge.service >/dev/null 2>&1 || true
	fi
fi

echo "OpenWeMo Matter bridge installed."
echo "Run: wemo-matter-bridge qr"
EOF

cat > "$PKG_DIR/DEBIAN/prerm" <<'EOF'
#!/bin/sh
set -e

if [ "$1" = "remove" ] && command -v systemctl >/dev/null 2>&1; then
	systemctl stop wemo-matter-bridge.service wemo_ctrl.service >/dev/null 2>&1 || true
	systemctl disable wemo-matter-bridge.service wemo_ctrl.service >/dev/null 2>&1 || true
fi
EOF

cat > "$PKG_DIR/DEBIAN/postrm" <<'EOF'
#!/bin/sh
set -e

ldconfig || true
if command -v systemctl >/dev/null 2>&1; then
	systemctl daemon-reload >/dev/null 2>&1 || true
fi
EOF

chmod 0755 "$PKG_DIR/DEBIAN/postinst" "$PKG_DIR/DEBIAN/prerm" "$PKG_DIR/DEBIAN/postrm"

if command -v strip >/dev/null 2>&1; then
  strip --strip-unneeded \
    "$PKG_DIR/usr/sbin/wemo-bridge-app" \
    "$PKG_DIR/usr/sbin/wemo_ctrl" \
    "$PKG_DIR/usr/sbin/wemo_client" \
    "$PKG_DIR/usr/lib/libwemoengine.so" 2>/dev/null || true
fi

DEB_NAME="openwemo-matter-bridge-${VERSION}-${ARCH}.deb"
DEB_PATH="$OUT_DIR/$DEB_NAME"
dpkg-deb --build --root-owner-group "$PKG_DIR" "$DEB_PATH"

echo "Built: $DEB_PATH"
