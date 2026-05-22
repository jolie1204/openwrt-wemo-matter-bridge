# Release Process

This repository can produce two release artifact types:

- Raspberry Pi OpenWrt firmware images for users who want an appliance image
- OpenWrt package artifacts for compatible existing installations
- experimental Raspberry Pi OS 64-bit Debian package artifacts

Newer OpenWrt trees may emit `.apk` packages. Older OpenWrt trees emit `.ipk`
packages for `opkg`. The release packaging script collects either format.

## Local Build

Build Raspberry Pi 4 image and package artifacts:

```sh
scripts/release/build_rpi_release.sh --target rpi4
```

Build Raspberry Pi 5 as well:

```sh
scripts/release/build_rpi_release.sh --target all
```

Build packages only:

```sh
scripts/release/build_rpi_release.sh --target rpi4 --package-only
```

Artifacts are staged in:

```text
dist/
```

The staged directory includes `SHA256SUMS` and `RELEASE_MANIFEST.txt`.
Firmware image staging intentionally keeps only the versioned ext4 sysupgrade
images:

```text
openwemo-rpi4-vX.Y.Z-sysupgrade.img.gz
openwemo-rpi5-vX.Y.Z-sysupgrade.img.gz
```

These are the public Raspberry Pi download artifacts. Factory and squashfs
variants are build outputs, not release assets.

Raspberry Pi images include the `wemo-rootfs-resize` first-boot service. It
expands an ext4 root partition to the available SD/eMMC size and skips layouts
with additional partitions after rootfs.

## Build Raspberry Pi OS Debian Package

The Debian package must be built natively on Raspberry Pi OS 64-bit. It is not
created from OpenWrt `.apk` files.

```sh
scripts/release/build_debian_package.sh --version v0.1.0 -j "$(nproc)"
TAG=v0.1.0 scripts/release/package_release_assets.sh
```

See [Raspberry Pi OS Debian Package](RASPBERRY_PI_OS_DEBIAN.md).

## Package Existing Build Outputs

If images/packages were already built:

```sh
scripts/release/package_release_assets.sh
```

Group artifacts by user path:

```sh
VERSION=v0.1.2 scripts/release/stage_usage_release_assets.sh
```

This creates:

```text
dist/openwrt-rpi/
dist/openwrt-packages/
dist/raspios-deb/
```

Use separate GitHub releases for each usage:

```sh
gh release create openwrt-rpi-v0.1.2 dist/openwrt-rpi/* \
  --title "WeMo to Matter Bridge for Raspberry Pi v0.1.2"

gh release create openwrt-packages-v0.1.2 dist/openwrt-packages/* \
  --title "WeMo to Matter Bridge Packages for OpenWrt v0.1.2"

gh release create raspios-deb-v0.1.2 dist/raspios-deb/* \
  --title "WeMo to Matter Bridge for Raspberry Pi OS v0.1.2"
```

## Create a Release Tag

Use usage-prefixed tags so the GitHub Releases page stays readable:

```sh
git tag -a openwrt-rpi-v0.1.0 -m "OpenWrt Raspberry Pi firmware v0.1.0"
git push origin openwrt-rpi-v0.1.0
```

`openwrt-rpi-vX.Y.Z` tag pushes trigger `.github/workflows/release-rpi.yml`.
Package-only and Debian releases are currently uploaded locally after their
artifacts are staged.

## Upload Release Assets

Using GitHub CLI from a local machine, first stage and group artifacts:

```sh
VERSION=v0.1.0 scripts/release/package_release_assets.sh
VERSION=v0.1.0 scripts/release/stage_usage_release_assets.sh
```

Then create or upload to the usage-specific release shown above.

## GitHub Actions

The release workflow:

```text
.github/workflows/release-rpi.yml
```

Behavior:

- triggers on tags like `openwrt-rpi-v0.1.0`
- builds Raspberry Pi 4 and Raspberry Pi 5 release artifacts by default
- supports manual `rpi4`, `rpi5`, or `all` dispatch
- supports manual package-only dispatch
- stages files in `dist/`
- generates `SHA256SUMS`
- uploads assets to the GitHub Release on tag builds

Full OpenWrt image builds are heavy. If GitHub hosted runners are too slow or
run out of space, build locally and upload with `create_github_release.sh`.
Debian packages should currently be built locally on Raspberry Pi OS and then
uploaded with the same release helper.

## Release Notes

Start from:

```text
docs/release-notes-template.md
```

Be conservative in wording. Do not claim official Matter certification.
