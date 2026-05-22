# Downloads

The GitHub Releases page is organized by how the bridge will be used:

```text
https://github.com/jolie1204/openwrt-wemo-matter-bridge/releases
```

## Recommended: Raspberry Pi OpenWrt Firmware

Use this when you want a Raspberry Pi to become a dedicated WeMo-to-Matter
bridge appliance.

Release title:

```text
OpenWeMo Raspberry Pi OpenWrt Firmware vX.Y.Z
```

Download one image:

- Raspberry Pi 4, 400, or Compute Module 4:
  `openwemo-rpi4-vX.Y.Z-sysupgrade.img.gz`
- Raspberry Pi 5 or Compute Module 5:
  `openwemo-rpi5-vX.Y.Z-sysupgrade.img.gz`

Flash the image with Raspberry Pi Imager or balenaEtcher, boot with Ethernet
connected, SSH into the device, and run:

```sh
wemo-matter-bridge qr
```

Scan that QR code in Apple Home or Google Home. The QR code pairs the bridge;
the individual WeMo devices appear as bridged Matter endpoints.

## Existing OpenWrt Install

Use this only if compatible OpenWrt is already installed and you do not want to
replace the whole firmware image.

Release title:

```text
OpenWeMo OpenWrt Packages vX.Y.Z
```

Install all packages for the target architecture:

- Raspberry Pi 4/CM4: `aarch64_cortex-a72`
- Raspberry Pi 5/CM5: `aarch64_cortex-a76`

Package installs are more manual than the firmware image. Most users should use
the Raspberry Pi OpenWrt firmware release instead.

## Raspberry Pi OS / Debian

Use this when you want to run the bridge as services on Raspberry Pi OS or
Debian instead of booting OpenWrt.

Release title:

```text
OpenWeMo Raspberry Pi OS Debian Package vX.Y.Z
```

Download and install:

```sh
sudo apt install ./openwemo-matter-bridge-vX.Y.Z-arm64.deb
wemo-matter-bridge qr
```

This path is experimental until validated on more Raspberry Pi OS installs.

## What Not To Download

- Do not install random package assets unless you are intentionally managing an
  existing OpenWrt system.
- Do not expect one QR code per WeMo device. Matter pairs the bridge once, and
  the WeMo devices appear behind it.
- This project does not claim official Matter certification.
