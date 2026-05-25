# Raspberry Pi OS Debian Package

This is an experimental packaging path for users who want to run the bridge on
Raspberry Pi OS instead of flashing the OpenWrt firmware image.

The Debian package is not built from OpenWrt `.apk` artifacts. It must be built
natively on Raspberry Pi OS 64-bit so the binaries link against Debian/glibc
libraries.

Current status: experimental. The Raspberry Pi OS `.deb` does not yet include
the OpenWrt/procd watchdog service added to the OpenWrt firmware and package
releases. Treat the OpenWrt Raspberry Pi image as the recommended release path
until the Debian package has equivalent systemd watchdog coverage and more
Raspberry Pi OS validation.

## Supported Host

- Raspberry Pi OS 64-bit: experimental
- Raspberry Pi 4: preferred first target
- Raspberry Pi 5: expected to work, not yet validated here
- 32-bit Raspberry Pi OS: not supported for the public package yet

## Build Dependencies

Install build tools and runtime headers:

```sh
sudo apt update
sudo apt install -y \
  build-essential git rsync patch python3 python3-venv pkg-config \
  ninja-build clang lld libsqlite3-dev libssl-dev libupnp-dev \
  libavahi-client-dev libavahi-common-dev libglib2.0-dev \
  libgio-2.0-dev-bin \
  qrencode dpkg-dev
```

The Matter SDK build may require additional packages depending on the Raspberry
Pi OS release. If the CHIP bootstrap reports a missing tool, install that
package and rerun the build script.

Debian 13/trixie currently uses Python 3.13. The CHIP bootstrap dependencies
may still carry Python bindings that only declare support through Python 3.12,
so the release build script sets `PYO3_USE_ABI3_FORWARD_COMPATIBILITY=1` while
building the Matter SDK tools.

Matter SDK C++ compilation is memory intensive on 2 GB Raspberry Pi boards.
The Debian package build defaults to `WEMO_NINJA_JOBS=1`; override it only on a
host with enough RAM.

## Build The Package

From the repository root on Raspberry Pi OS 64-bit:

```sh
scripts/release/build_debian_package.sh --version v0.1.2 -j "$(nproc)"
```

To reuse an existing `connectedhomeip` checkout:

```sh
scripts/release/build_debian_package.sh \
  --version v0.1.2 \
  --chip-root /path/to/connectedhomeip \
  -j "$(nproc)"
```

Output is written under:

```text
bin/debian/
```

The release packager will include matching Debian packages automatically:

```sh
TAG=v0.1.2 scripts/release/package_release_assets.sh
```

## Install

Copy the `.deb` to the Raspberry Pi and install it:

```sh
sudo apt install ./openwemo-matter-bridge-v0.1.2-arm64.deb
```

The package installs:

- `/usr/sbin/wemo_ctrl`
- `/usr/sbin/wemo_client`
- `/usr/sbin/wemo-bridge-app`
- `/usr/sbin/wemo-matter-bridge`
- `wemo_ctrl.service`
- `wemo-matter-bridge.service`

Runtime state is stored in:

```text
/var/lib/wemo-matter-bridge
```

Configuration is stored in:

```text
/etc/default/wemo-matter-bridge
/etc/wemo_ctrl.conf
/etc/wemo-matter-bridge/
```

## First Run

The package enables and starts the services during install. To check status:

```sh
systemctl status wemo_ctrl wemo-matter-bridge
wemo-matter-bridge status
```

To show the Matter pairing code:

```sh
wemo-matter-bridge qr
```

Scan the QR code in Apple Home or Google Home. The QR code pairs the bridge;
individual WeMo devices appear as bridged endpoints after commissioning.

## Interface Selection

By default the service binds to the default-route network interface. To force a
specific LAN interface, edit:

```text
/etc/default/wemo-matter-bridge
```

Set:

```sh
WEMO_BRIDGE_INTERFACE=eth0
```

Then restart:

```sh
sudo systemctl restart wemo_ctrl wemo-matter-bridge
```

## Onboarding State

The Debian package uses the same release-mode onboarding policy as the OpenWrt
image:

- no shared fixed passcode is shipped as the release default
- a unique passcode/discriminator is generated on first run
- onboarding data is persisted under `/etc/wemo-matter-bridge/`

To regenerate onboarding:

```sh
sudo wemo-matter-bridge reset-onboarding
```

Already commissioned controllers may need the bridge removed and added again.

## Logs

```sh
wemo-matter-bridge logs
```

or directly:

```sh
journalctl -u wemo_ctrl -u wemo-matter-bridge -f
```

## Limitations

- Experimental packaging path; OpenWrt remains the primary release image.
- The OpenWrt/procd watchdog is not part of the Debian package yet. A future
  Debian release should add an equivalent systemd timer/service or watchdog
  unit before it is presented as feature-equivalent to the OpenWrt image.
- The first Debian release should be labeled experimental until validated on a
  real Raspberry Pi OS 64-bit install.
- The package does not claim Matter certification.
