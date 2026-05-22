#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
DIST_DIR="${DIST_DIR:-$ROOT_DIR/dist}"
VERSION="${VERSION:-${1:-$(git -C "$ROOT_DIR" describe --tags --always --dirty 2>/dev/null || echo snapshot)}}"

case "$VERSION" in
  openwrt-rpi-v*|openwrt-packages-v*|raspios-deb-v*)
    VERSION="v${VERSION##*-v}"
    ;;
esac

cd "$ROOT_DIR"

if [[ ! -d "$DIST_DIR" || -z "$(find "$DIST_DIR" -maxdepth 1 -type f -print -quit)" ]]; then
  VERSION="$VERSION" "$ROOT_DIR/scripts/release/package_release_assets.sh"
fi

reset_group() {
  local group="$1"
  rm -rf "$DIST_DIR/$group"
  mkdir -p "$DIST_DIR/$group"
}

copy_group_matches() {
  local group="$1"
  local pattern="$2"
  local found=0

  while IFS= read -r -d '' file; do
    cp "$file" "$DIST_DIR/$group/"
    found=1
  done < <(find "$DIST_DIR" -maxdepth 1 -type f -name "$pattern" -print0)

  [[ "$found" -eq 1 ]]
}

write_checksums() {
  local group="$1"
  if find "$DIST_DIR/$group" -maxdepth 1 -type f ! -name 'SHA256SUMS' | grep -q .; then
    (
      cd "$DIST_DIR/$group"
      sha256sum * > SHA256SUMS
    )
  fi
}

write_readme() {
  local group="$1"
  local title="$2"
  local body="$3"

  {
    echo "$title"
    echo
    echo "Version: $VERSION"
    echo
    printf '%s\n' "$body"
    echo
    echo "Artifacts:"
    find "$DIST_DIR/$group" -maxdepth 1 -type f ! -name 'README.txt' -printf '  %f\n' | sort
  } > "$DIST_DIR/$group/README.txt"
}

reset_group openwrt-rpi
copy_group_matches openwrt-rpi "openwemo-rpi*-sysupgrade.img.gz" || true
write_readme openwrt-rpi "OpenWeMo Raspberry Pi OpenWrt Firmware" \
"Use these images when you want a Raspberry Pi to boot directly into OpenWrt as a WeMo-to-Matter bridge appliance.

Flash the rpi4 image for Raspberry Pi 4, 400, or Compute Module 4. Use the rpi5 image for Raspberry Pi 5 or CM5. The image uses ext4 rootfs and includes first-boot root expansion for SD card or eMMC."
write_checksums openwrt-rpi

reset_group openwrt-packages
copy_group_matches openwrt-packages "*.apk" || true
copy_group_matches openwrt-packages "*.ipk" || true
write_readme openwrt-packages "OpenWeMo OpenWrt Package Artifacts" \
"Use these packages only when you already have a compatible OpenWrt installation and want to install the bridge without flashing the full Raspberry Pi firmware image.

Install all bridge packages for your target architecture. Raspberry Pi 4/CM4 uses aarch64_cortex-a72. Raspberry Pi 5/CM5 uses aarch64_cortex-a76."
write_checksums openwrt-packages

reset_group raspios-deb
copy_group_matches raspios-deb "*.deb" || true
write_readme raspios-deb "OpenWeMo Raspberry Pi OS Debian Package" \
"Use this package when you want to run the bridge on Raspberry Pi OS or Debian instead of OpenWrt.

The Debian package is built natively on Raspberry Pi OS/Debian arm64. This path is experimental until validated on more Raspberry Pi OS installations."
write_checksums raspios-deb

echo "Usage release assets staged in $DIST_DIR:"
find "$DIST_DIR" -mindepth 2 -maxdepth 2 -type f -printf '  %P\n' | sort
