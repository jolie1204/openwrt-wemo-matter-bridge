# Raspberry Pi Quick Start

This guide is for normal users who want a Raspberry Pi to act as a local
WeMo-to-Matter bridge.

The bridge runs locally on your LAN. It discovers legacy Belkin WeMo devices
using UPnP and exposes them as bridged Matter endpoints to Apple Home, Google
Home, or another Matter controller.

## Supported Images

Primary target:

- Raspberry Pi 4: `bcm27xx/bcm2711`, device `rpi-4-wemo-matter-bridge`

Experimental target:

- Raspberry Pi 5: `bcm27xx/bcm2712`, device `rpi-5-wemo-matter-bridge`

Release image names contain the target name, for example:

```text
openwrt-bcm27xx-bcm2711-rpi-4-wemo-matter-bridge-squashfs-factory.img.gz
openwrt-bcm27xx-bcm2711-rpi-4-wemo-matter-bridge-squashfs-sysupgrade.img.gz
```

## Flash a Raspberry Pi Image

1. Download the latest Raspberry Pi image from GitHub Releases.
2. Verify `SHA256SUMS` when possible:

```sh
sha256sum -c SHA256SUMS
```

3. Flash the `factory.img.gz` file with Raspberry Pi Imager or balenaEtcher.
4. Boot the Raspberry Pi with Ethernet connected.
5. Find the IP address in your router DHCP table.
6. SSH into OpenWrt:

```sh
ssh root@<bridge-ip>
```

7. Check service status:

```sh
wemo-matter-bridge status
```

8. Print the Matter QR code:

```sh
wemo-matter-bridge qr
```

9. In Apple Home or Google Home, add a Matter accessory and scan the QR code.

The QR code pairs the bridge itself. Individual WeMo switches, plugs, and
dimmers appear as bridged endpoints after commissioning.

## Install on Existing OpenWrt

Copy the package artifact to the target:

```sh
scp wemo-matter-bridge* root@<openwrt-ip>:/tmp/
scp openwemo-bridge-core* root@<openwrt-ip>:/tmp/
```

Install using the package manager used by your OpenWrt version:

```sh
# opkg-based images
opkg install /tmp/openwemo-bridge-core*.ipk /tmp/wemo-matter-bridge*.ipk

# apk-based images
apk add --allow-untrusted /tmp/openwemo-bridge-core*.apk /tmp/wemo-matter-bridge*.apk
```

Enable and start services:

```sh
/etc/init.d/wemo_ctrl enable
/etc/init.d/wemo-matter-bridge enable
/etc/init.d/wemo_ctrl start
/etc/init.d/wemo-matter-bridge start
wemo-matter-bridge qr
```

## Useful Commands

```sh
wemo-matter-bridge status
wemo-matter-bridge logs
wemo-matter-bridge scan-wemo
wemo-matter-bridge qr
wemo-matter-bridge restart
wemo-matter-bridge reset-onboarding
```

`reset-onboarding` creates new Matter onboarding credentials. Controllers that
already commissioned the bridge may need the bridge removed and added again.
