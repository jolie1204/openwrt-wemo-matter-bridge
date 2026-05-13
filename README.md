# OpenWrt for Wemo Matter Bridge

This repository is a device-focused OpenWrt fork for the Belkin Wemo Bridge
hardware based on MT7628. Its primary job is to build firmware for a Wemo to
Matter bridge appliance, not to serve as a generic OpenWrt distribution tree.

The tree carries custom board support, bridge packages, persistent state
handling, and operational defaults needed to replace a host-based Wemo Matter
bridge with a self-contained OpenWrt image.

## Scope

This fork adds Wemo bridge specific behavior on top of upstream OpenWrt:

- `wemo-matter-bridge` target profile for the Belkin MT7628 bridge hardware
- `openwemo-bridge-core` package from local sibling sources
- `wemo-matter-bridge` package from local sibling sources
- `wemo-mtd-data` package to mount the `data` MTD partition at `/data`
- Matter and OpenWeMo state persistence under `/data/wemo-matter`
- built-in WireGuard tools for remote management
- built-in `fw_printenv` / `fw_setenv` support for the bridge U-Boot env area
- LED/button behavior for basic health and service control

This repo does not try to support the original vendor firmware layout, dual
boot slots, or Wi-Fi-first router use cases.

## Supported targets

- `wemo-matter-bridge`
  - default and recommended target
  - 16 MiB SPI NOR
  - includes dedicated `data` partition for persistent state
- `wemo-matter-bridge-8m`
  - secondary target for confirmed 8 MiB units
  - more space constrained

The default platform assumption is 16 MiB flash.

## Device assumptions

- bootloader uses boot A only
- kernel must stay at flash offset `0x50000`
- kernel/rootfs remain separate, with rootfs as `mtd2`
- no dual partitions, no `kernel_2`, no `rootfs_2`
- no vendor `vendor_rootfs_data`
- no usable Wi-Fi antenna; Wi-Fi packages are removed from the bridge profile
- factory MAC is expected at `factory + 0x28`

## Flash layout

### 16 MiB target

The current 16 MiB layout is:

- `u-boot` at `0x000000`
- `u-boot-env` at `0x030000`
- `factory` at `0x040000`
- `kernel` at `0x050000`, size `0x200000`
- `rootfs` at `0x250000`, size `0x7d0000`
- `data` at `0xa20000`, size `0x5d0000`
- `config` at `0xff0000`

The aggregate `firmware` region is `0x9d0000`.

### 8 MiB target

The 8 MiB target keeps the same bootloader expectations but has a smaller
firmware region and less room for growth. Use it only when the hardware is
confirmed to be 8 MiB.

## Runtime layout

The bridge stores runtime state here:

- Matter and OpenWeMo state root: `/data/wemo-matter`
- CHIP state: `/data/wemo-matter/chip`
- compatibility symlink: `/etc/wemo-matter -> /data/wemo-matter`

On 16 MiB systems, `wemo-mtd-data` mounts the `data` MTD partition as JFFS2 at
`/data` during boot, migrates legacy state if needed, and prepares the state
directories with restrictive permissions.

## Included bridge packages

The `wemo-matter-bridge` image profile currently includes:

- `openwemo-bridge-core`
- `wemo-matter-bridge`
- `wemo-mtd-data`
- `wireguard-tools`
- `uboot-envtools`
- `libsqlite3`
- `libstdcpp`
- `libupnp`

The installed bridge tooling includes:

- `/usr/sbin/wemo_ctrl`
- `/usr/sbin/wemo_client`
- `/usr/sbin/wemo-bridge-app`
- `/usr/sbin/fw_printenv`
- `/usr/sbin/fw_setenv`

## Service behavior

Bridge services are wired for the current appliance behavior:

- `br-lan` is used as a DHCP client uplink
- `wemo_ctrl` and `wemo-matter-bridge` wait for `lan` / `br-lan` readiness
- Matter bridge state is passed through `/data/wemo-matter`
- WireGuard can be enabled through normal OpenWrt `network` config

## LED and button behavior

Current bridge LED/button behavior is intentionally conservative:

- white slow blink: booting or waiting for LAN
- white solid: healthy
- amber slow blink: uncommissioned or no fabric
- amber fast blink: fault
- short button press: status pulse
- hold button for 8 seconds: restart `wemo_ctrl` and `wemo-matter-bridge`

There is no destructive reset action bound to the front button. These front
panel mappings are specific to the Belkin MT7628 bridge hardware; Raspberry Pi
profiles do not provide the same LED/button layout.

## Local source dependencies

This tree expects sibling source repositories under `../sources`:

- `../sources/openwemo-bridge-core`
- `../sources/wemo-matter-bridge`

The OpenWrt packages copy sources from those directories during build. If they
are missing, the package build will fail by design.

For the current working build, the OpenWrt tree was checked out from:

```text
https://github.com/jolie1204/openwrt-wemo-matter-bridge
```

The sibling package sources were checked out from:

```text
https://github.com/jolie1204/openwemo-bridge-core
https://github.com/jolie1204/wemo-matter-bridge
```

## Current build status

As of the latest local build, the 16 MiB `wemo-matter-bridge` profile builds
successfully end to end for `ramips/mt76x8`.

The selected target is:

```text
CONFIG_TARGET_ramips=y
CONFIG_TARGET_ramips_mt76x8=y
CONFIG_TARGET_ramips_mt76x8_DEVICE_wemo-matter-bridge=y
```

The 8 MiB profile is intentionally not selected:

```text
# CONFIG_TARGET_ramips_mt76x8_DEVICE_wemo-matter-bridge-8m is not set
```

The verified output files are:

```text
bin/targets/ramips/mt76x8/openwrt-ramips-mt76x8-wemo-matter-bridge-initramfs-kernel.bin
bin/targets/ramips/mt76x8/openwrt-ramips-mt76x8-wemo-matter-bridge-squashfs-sysupgrade.bin
```

Current hashes from the completed build:

```text
92f01e0bbfa640f04acb11bf743e7b3367ec4cd6b7fbdcb434ec058c6e2eba16  openwrt-ramips-mt76x8-wemo-matter-bridge-initramfs-kernel.bin
55e6cad640263759355fc648f519d8f249f265b16762987d038349a3d39a17e1  openwrt-ramips-mt76x8-wemo-matter-bridge-squashfs-sysupgrade.bin
```

The package outputs were also generated:

```text
bin/packages/mipsel_24kc/base/openwemo-bridge-core-0.1-r10.apk
bin/packages/mipsel_24kc/base/wemo-matter-bridge-2025.12.01~8effa808-r10.apk
```

The `wemo-matter-bridge` package hash from this build is:

```text
71bd3f97c9c7f14d23e70c767f4869f4893fb575faf5ef3e7cd3a70f231e60ae  wemo-matter-bridge-2025.12.01~8effa808-r10.apk
```

## What was fixed during bring-up

Several local build fixes were needed to make this tree reproduce the Wemo
Matter bridge firmware on the current OpenWrt base:

- `include/download.mk`
  - removed inner shell quoting from the git timestamp command so custom
    download recipes survive OpenWrt's `flock -c` wrapping
- `include/git-with-metadata.mk`
  - added a local, flattened custom downloader so `git-with-metadata` archives
    can be generated with selected submodules
- `package/network/services/wemo-matter-bridge/Makefile`
  - switched the Matter package source to `git-with-metadata`
  - skipped mirror hash verification for that locally generated archive
  - included the Matter feed downloader helper
- `package/network/services/wemo-matter-bridge/files/wemo-matter-bridge.init`
  - sets both `WEMO_DEVICE_DB_PATH` and `WEMO_STATE_DB_PATH` in one procd
    environment block so the Matter process can read the OpenWeMo device and
    state databases
- `package/network/services/openwemo-bridge-core/Makefile`
  - added the missing `libopenssl` dependency because `wemo_client` uses
    OpenSSL headers and links libcrypto
- `package/network/services/openwemo-bridge-core/patches/002-health-snapshot-fallback.patch`
  - fixed the health snapshot fallback to use the existing Wemo device database
    and `GlobalDeviceList` instead of non-existent list APIs
- `package/network/services/openwemo-bridge-core/patches/003-libupnp-init2-signature.patch`
  - updated the `UpnpInit2` call for the libupnp headers used by OpenWrt
- `package/network/services/openwemo-bridge-core/patches/004-add-device-list-api.patch`
  - added `we_list_devices()` and exported `struct we_device_list` /
    `struct we_device_info`, which are required by the Matter bridge's
    OpenWeMo adapter

The key integration issue was that `wemo-matter-bridge` expected a device list
API from `openwemo-bridge-core`, but the local core source did not provide it.
The added `we_list_devices()` implementation reads the OpenWeMo device database
and state database, then returns a bounded list of devices for the Matter bridge
adapter.

A later runtime issue made Google Home show the Matter bridge but hide the child
Wemo devices. The Matter fabric and pairing data were still present under
`/data/wemo-matter/chip`; the problem was that the bridge app started before the
OpenWeMo list API was ready, and the init script only delivered one of the two
database environment variables to the process. The app now retries
`we_list_devices()` during startup, the init script exports both database paths,
and the app reports the descriptor `PartsList` after publishing bridged
endpoints.

Useful checks on a running bridge:

```sh
tr '\0' '\n' </proc/$(pidof wemo-bridge-app)/environ | grep WEMO
logread | grep -E 'WeMo bind|Added device|WeMo bridge published'
```

## Reproduced build flow

On the build host, install the basic OpenWrt host prerequisites first. On
Debian/Ubuntu, the missing packages encountered locally were:

```bash
sudo apt install gawk libncurses-dev
```

Prepare feeds and the minimal 16 MiB target config:

```bash
./scripts/feeds update -i
./scripts/feeds install -p packages libsqlite3 libupnp wireguard-tools
./scripts/feeds install -p matter gn python3-host-ssl

cat > .config <<'EOF'
CONFIG_TARGET_ramips=y
CONFIG_TARGET_ramips_mt76x8=y
CONFIG_TARGET_ramips_mt76x8_DEVICE_wemo-matter-bridge=y
EOF

make defconfig
```

Build the host tools, toolchain, bridge packages, then the full image:

```bash
make tools/install V=s -j"$(nproc)"
make toolchain/install V=s -j"$(nproc)"
make package/network/services/openwemo-bridge-core/compile V=s -j"$(nproc)"
make package/network/services/wemo-matter-bridge/compile V=s -j"$(nproc)"
make -j"$(nproc)" V=s
```

The full build was run twice after the fixes and completed cleanly both times.
Warnings about unrelated optional feed dependencies such as `libpam`, `libtirpc`,
`libev`, `libnetsnmp`, and `ruby/host` appeared during metadata scanning, but
they did not block the selected Wemo bridge target.

## Build quick start

Typical OpenWrt preparation still applies:

```bash
./scripts/feeds update -a
./scripts/feeds install -a
```

Then select the bridge target in `make menuconfig`:

- Target System: `MediaTek Ralink MIPS`
- Subtarget: `MT76x8`
- Target Profile: `wemo-matter-bridge` or `wemo-matter-bridge-8m`

Build with:

```bash
make -j"$(nproc)"
```

Resulting artifacts are written under:

```text
bin/targets/ramips/mt76x8/
```

The main output for the 16 MiB bridge is typically:

```text
openwrt-ramips-mt76x8-wemo-matter-bridge-squashfs-sysupgrade.bin
```

Useful companion files:

- `openwrt-ramips-mt76x8-wemo-matter-bridge-initramfs-kernel.bin`
- `openwrt-ramips-mt76x8-wemo-matter-bridge.manifest`
- `sha256sums`

## Building for Raspberry Pi 4 and 5

This tree also includes dedicated Raspberry Pi appliance profiles so the Wemo
bridge package set is selected by the target profile instead of by ad hoc local
`.config` edits.

In `make menuconfig`, choose one of these profile flows:

- Raspberry Pi 4
  - Target System: `Broadcom BCM27xx`
  - Subtarget: `BCM2711 boards (64 bit)`
  - Target Profile: `Raspberry Pi 4B/400/CM4 (64bit, Wemo Matter Bridge)`
- Raspberry Pi 5
  - Target System: `Broadcom BCM27xx`
  - Subtarget: `BCM2712 boards (64 bit)`
  - Target Profile: `Raspberry Pi 5/500/CM5 (Wemo Matter Bridge)`

Example flow:

```bash
./scripts/feeds update -a
./scripts/feeds install -a
make menuconfig
make -j"$(nproc)"
```

The Pi image artifacts are written under:

```text
bin/targets/bcm27xx/bcm2711/
bin/targets/bcm27xx/bcm2712/
```

Typical output files include:

- Pi 4
  - `openwrt-bcm27xx-bcm2711-rpi-4-wemo-matter-bridge-squashfs-factory.img.gz`
  - `openwrt-bcm27xx-bcm2711-rpi-4-wemo-matter-bridge-squashfs-sysupgrade.img.gz`
- Pi 5
  - `openwrt-bcm27xx-bcm2712-rpi-5-wemo-matter-bridge-squashfs-factory.img.gz`
  - `openwrt-bcm27xx-bcm2712-rpi-5-wemo-matter-bridge-squashfs-sysupgrade.img.gz`

Pi runtime notes:

- OpenWrt on Raspberry Pi uses `eth0` as `lan` by default.
- The bridge services and indicator now resolve the actual `lan` device
  dynamically, so they are not tied to `br-lan`.
- On targets without the Wemo-specific MTD `data` partition, `wemo-mtd-data`
  falls back to a normal writable `/data` directory instead of requiring JFFS2.
- Bridge state still lives under `/data/wemo-matter`. On Raspberry Pi, that
  path is backed by the normal writable filesystem unless you mount a separate
  disk or partition at `/data`.

If you want the Pi to behave like the MT7628 appliance on a normal upstream
network, change `lan` to DHCP client after first boot:

```sh
uci set network.lan.proto='dhcp'
uci commit network
service network restart
```

## Raspberry Pi operator checks

After first boot, these checks are enough to prove the appliance path is up:

```sh
ubus call system board
ubus call network.interface.lan status
test -d /data/wemo-matter/chip && ls -ld /data /data/wemo-matter /data/wemo-matter/chip
pidof wemo_ctrl
pidof wemo-bridge-app
logread | grep -E 'wemo_mtd_data|Fabric index|Server Listening'
printf 'listdev\nexit\n' | /usr/sbin/wemo_client
printf 'preflightall\nexit\n' | /usr/sbin/wemo_client
```

If WireGuard is configured on the Pi, also check:

```sh
wg show
```

## Flashing and upgrades

The bridge is intended to be updated with `sysupgrade`.

Typical workflow:

```bash
scp bin/targets/ramips/mt76x8/openwrt-ramips-mt76x8-wemo-matter-bridge-squashfs-sysupgrade.bin root@<bridge>:/tmp/
ssh root@<bridge> sysupgrade /tmp/openwrt-ramips-mt76x8-wemo-matter-bridge-squashfs-sysupgrade.bin
```

When preserving configuration, bridge state under `/data/wemo-matter` survives
reboot as long as the `data` partition layout is unchanged.

## Remote management

The bridge profile includes WireGuard tooling so the device can be managed
without relying on a stable LAN address.

Useful checks on a running bridge:

```bash
ssh root@<bridge> ubus call system board
ssh root@<bridge> wg show
ssh root@<bridge> fw_printenv
ssh root@<bridge> 'printf "listdev\nexit\n" | /usr/sbin/wemo_client'
```

## Relationship to upstream OpenWrt

This repository is still an OpenWrt build tree, so normal upstream OpenWrt
documentation remains relevant for generic build-system topics:

- https://openwrt.org/docs/guide-developer/start
- https://openwrt.org/docs/guide-user/start

For generic OpenWrt source and history, see upstream:

- https://github.com/openwrt/openwrt

## Related repositories

- `openwemo-bridge-core`: local WeMo discovery/control daemon and client
- `wemo-matter-bridge`: Matter bridge application sources

## License

This repository remains based on OpenWrt and is therefore primarily governed by
the licensing of upstream OpenWrt and the packages included in the tree.
