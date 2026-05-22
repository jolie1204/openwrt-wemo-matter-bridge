# Release Process

This repository can produce two release artifact types:

- Raspberry Pi OpenWrt firmware images
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

## Create a Release Tag

Use semantic version tags:

```sh
git tag -a v0.1.0 -m "v0.1.0"
git push origin v0.1.0
```

Tag pushes trigger `.github/workflows/release-rpi.yml`.

## Upload Release Assets

Using GitHub CLI from a local machine:

```sh
scripts/release/create_github_release.sh v0.1.0
```

To upload into an existing release:

```sh
scripts/release/create_github_release.sh v0.1.0 --upload-only
```

Equivalent raw GitHub CLI commands:

```sh
gh release create v0.1.0 dist/* --generate-notes
gh release upload v0.1.0 dist/* --clobber
```

## GitHub Actions

The release workflow:

```text
.github/workflows/release-rpi.yml
```

Behavior:

- triggers on tags like `v0.1.0`
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
