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
  shift 2

  {
    echo "# $title"
    echo
    echo "Version: $VERSION"
    echo
    sed "s/vX.Y.Z/$VERSION/g"
    echo
    echo "## Artifacts"
    find "$DIST_DIR/$group" -maxdepth 1 -type f ! -name 'README.txt' -printf '- %f\n' | sort
  } > "$DIST_DIR/$group/README.txt"
}

reset_group openwrt-rpi
copy_group_matches openwrt-rpi "openwemo-rpi*-sysupgrade.img.gz" || true
write_readme openwrt-rpi "WeMo to Matter Bridge for Raspberry Pi" <<'EOF'
Use this release when you want a Raspberry Pi to become a dedicated local
bridge for legacy Belkin WeMo LAN devices. The Raspberry Pi boots a small
OpenWrt-based firmware image, discovers WeMo devices on the LAN, and exposes
them to Apple Home, Google Home, or another Matter controller as bridged Matter
endpoints.

## Which File Should I Download?

- Raspberry Pi 4, Raspberry Pi 400, or Compute Module 4:
  `openwemo-rpi4-vX.Y.Z-sysupgrade.img.gz`
- Raspberry Pi 5 or Compute Module 5:
  `openwemo-rpi5-vX.Y.Z-sysupgrade.img.gz`

The filename says `sysupgrade` because it is an OpenWrt image format. For this
release, that is also the file normal users should flash to an SD card or eMMC.

## Step-by-Step

1. Download the correct `openwemo-rpi*-vX.Y.Z-sysupgrade.img.gz` file.
2. Optionally download `SHA256SUMS` and verify the file.
3. Flash the `.img.gz` file with Raspberry Pi Imager or balenaEtcher.
4. Boot the Raspberry Pi with Ethernet connected.
5. Find the Raspberry Pi IP address from your router DHCP table.
6. SSH into the bridge: `ssh root@<bridge-ip>`.
7. Run: `wemo-matter-bridge status`.
8. Run: `wemo-matter-bridge health`.
9. Run: `wemo-matter-bridge qr`.
10. In Apple Home or Google Home, add a Matter accessory and scan the QR code.

The QR code pairs the bridge once. Individual WeMo switches, plugs, and dimmers
then appear as bridged endpoints behind the bridge.

## Notes

- The image generates unique Matter onboarding credentials on first boot.
- The ext4 root filesystem expands on first boot to use the SD card or eMMC.
- The OpenWrt watchdog restarts the bridge stack after repeated runtime health
  failures.
- WeMo discovery is local to your LAN and requires multicast/SSDP visibility.
- This project does not claim official Matter certification.
EOF
write_checksums openwrt-rpi

reset_group openwrt-packages
copy_group_matches openwrt-packages "*.apk" || true
copy_group_matches openwrt-packages "*.ipk" || true
write_readme openwrt-packages "WeMo to Matter Bridge Packages for OpenWrt" <<'EOF'
Use this release only when you already have compatible OpenWrt installed and
want to add the WeMo-to-Matter bridge without flashing the full Raspberry Pi
firmware image.

Most users should use the Raspberry Pi firmware release instead.

## Which Files Should I Download?

Download all bridge packages for one target architecture:

- Raspberry Pi 4 or Compute Module 4: files ending in `aarch64_cortex-a72.apk`
- Raspberry Pi 5 or Compute Module 5: files ending in `aarch64_cortex-a76.apk`

You normally need:

- `openwemo-bridge-core-...apk`
- `wemo-matter-bridge-...apk`
- `wemo-rootfs-resize-...apk`, only for Raspberry Pi ext4 rootfs expansion

## Step-by-Step

1. Download the package files for your OpenWrt target architecture.
2. Copy them to the OpenWrt device: `scp *.apk root@<openwrt-ip>:/tmp/`.
3. SSH into OpenWrt: `ssh root@<openwrt-ip>`.
4. Install packages: `apk add --allow-untrusted /tmp/*.apk`.
5. Enable services: `/etc/init.d/wemo_ctrl enable`.
6. Enable services: `/etc/init.d/wemo-matter-bridge enable`.
7. Enable services: `/etc/init.d/wemo-matter-bridge-watchdog enable`.
8. Start services: `/etc/init.d/wemo_ctrl start`.
9. Start services: `/etc/init.d/wemo-matter-bridge start`.
10. Start services: `/etc/init.d/wemo-matter-bridge-watchdog start`.
11. Run: `wemo-matter-bridge health`.
12. Run: `wemo-matter-bridge qr`.
13. In Apple Home or Google Home, add a Matter accessory and scan the QR code.

The QR code pairs the bridge once. Individual WeMo devices appear as bridged
Matter endpoints.
EOF
write_checksums openwrt-packages

reset_group raspios-deb
copy_group_matches raspios-deb "*.deb" || true
write_readme raspios-deb "WeMo to Matter Bridge for Raspberry Pi OS" <<'EOF'
Use this release when you want to run the bridge as systemd services on
Raspberry Pi OS or Debian instead of booting an OpenWrt firmware image.

This path is experimental until validated on more Raspberry Pi OS installs.

## Which File Should I Download?

Download:

- `openwemo-matter-bridge-vX.Y.Z-arm64.deb`

## Step-by-Step

1. Download the `.deb` package to the Raspberry Pi.
2. Install it: `sudo apt install ./openwemo-matter-bridge-vX.Y.Z-arm64.deb`.
3. Check services: `wemo-matter-bridge status`.
4. Print the pairing code: `wemo-matter-bridge qr`.
5. In Apple Home or Google Home, add a Matter accessory and scan the QR code.

The package stores bridge state under `/var/lib/wemo-matter-bridge` and Matter
onboarding configuration under `/etc/wemo-matter-bridge`.
EOF
write_checksums raspios-deb

echo "Usage release assets staged in $DIST_DIR:"
find "$DIST_DIR" -mindepth 2 -maxdepth 2 -type f -printf '  %P\n' | sort
