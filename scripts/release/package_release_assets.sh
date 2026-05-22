#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
DIST_DIR="${DIST_DIR:-$ROOT_DIR/dist}"
TAG="${TAG:-$(git -C "$ROOT_DIR" describe --tags --always --dirty 2>/dev/null || echo snapshot)}"
VERSION="${VERSION:-$TAG}"

case "$VERSION" in
  openwrt-rpi-v*|openwrt-packages-v*|raspios-deb-v*)
    VERSION="v${VERSION##*-v}"
    ;;
esac

cd "$ROOT_DIR"
rm -rf "$DIST_DIR"
mkdir -p "$DIST_DIR"

copy_matches() {
  local pattern="$1"
  local found=0

  while IFS= read -r -d '' file; do
    cp "$file" "$DIST_DIR/"
    found=1
  done < <(find . -path "$pattern" -type f -print0 2>/dev/null)

  [[ "$found" -eq 1 ]]
}

copy_image_matches() {
  local pattern="$1"
  local target="$2"
  local variant="$3"
  local found=0

  while IFS= read -r -d '' file; do
    cp "$file" "$DIST_DIR/openwemo-${target}-${VERSION}-${variant}.img.gz"
    found=1
  done < <(find . -path "$pattern" -type f -print0 2>/dev/null)

  [[ "$found" -eq 1 ]]
}

copy_package_matches() {
  local pattern="$1"
  local found=0
  local arch
  local base
  local stem
  local ext

  while IFS= read -r -d '' file; do
    arch="$(basename "$(dirname "$(dirname "$file")")")"
    case "$arch" in
      aarch64_cortex-a72|aarch64_cortex-a76) ;;
      *) continue ;;
    esac
    base="$(basename "$file")"
    ext="${base##*.}"
    stem="${base%.*}"
    stem="${stem//\~/.}"
    cp "$file" "$DIST_DIR/${stem}-${arch}.${ext}"
    found=1
  done < <(find ./bin/packages -path "$pattern" -type f -print0 2>/dev/null)

  [[ "$found" -eq 1 ]]
}

copy_image_matches './bin/targets/bcm27xx/bcm2711/*rpi-4-wemo-matter-bridge*ext4-sysupgrade.img.gz' rpi4 sysupgrade || true
copy_image_matches './bin/targets/bcm27xx/bcm2712/*rpi-5-wemo-matter-bridge*ext4-sysupgrade.img.gz' rpi5 sysupgrade || true

openwemo_release="$(awk -F:= '/^PKG_RELEASE:=/ { print $2; exit }' package/network/services/openwemo-bridge-core/Makefile)"
wemo_release="$(awk -F:= '/^PKG_RELEASE:=/ { print $2; exit }' package/network/services/wemo-matter-bridge/Makefile)"
resize_release="$(awk -F:= '/^PKG_RELEASE:=/ { print $2; exit }' package/network/services/wemo-rootfs-resize/Makefile)"
copy_package_matches "*/*/openwemo-bridge-core*-r${openwemo_release}.[ia]pk" || true
copy_package_matches "*/*/wemo-matter-bridge*-r${wemo_release}.[ia]pk" || true
copy_package_matches "*/*/wemo-rootfs-resize*-r${resize_release}.[ia]pk" || true
copy_matches './bin/debian/openwemo-matter-bridge-*.deb' || true

if [[ -f docs/release-notes-template.md ]]; then
  cp docs/release-notes-template.md "$DIST_DIR/RELEASE_NOTES_TEMPLATE.md"
fi

{
  echo "Release: $TAG"
  echo "Version: $VERSION"
  echo "Generated: $(date -u '+%Y-%m-%dT%H:%M:%SZ')"
  echo
  echo "Artifacts:"
  find "$DIST_DIR" -maxdepth 1 -type f -printf '%f\n' | sort
} > "$DIST_DIR/RELEASE_MANIFEST.txt"

if find "$DIST_DIR" -maxdepth 1 -type f ! -name 'SHA256SUMS' | grep -q .; then
  (
    cd "$DIST_DIR"
    sha256sum * > SHA256SUMS
  )
fi

echo "Release assets staged in $DIST_DIR"
find "$DIST_DIR" -maxdepth 1 -type f -printf '  %f\n' | sort
