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

There is no destructive reset action bound to the front button.

## Local source dependencies

This tree expects sibling source repositories under `../sources`:

- `../sources/openwemo-bridge-core`
- `../sources/wemo-matter-bridge`

The OpenWrt packages copy sources from those directories during build. If they
are missing, the package build will fail by design.

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
