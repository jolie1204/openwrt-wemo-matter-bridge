# Troubleshooting

## Bridge Status

```sh
wemo-matter-bridge status
wemo-matter-bridge logs
```

Expected services:

```text
wemo_ctrl
wemo-bridge-app
```

## No QR Code

Run:

```sh
wemo-matter-bridge qr
```

If ASCII QR output is unavailable, install `qrencode` or use the printed Matter
QR payload/manual pairing code. Raspberry Pi release images include `qrencode`.

## Pairing Fails

Check:

- phone/controller and bridge are on the same LAN
- IPv6 is not blocked on the LAN
- multicast/mDNS is not blocked
- the bridge has an IP address
- `wemo-bridge-app` is running

Show current onboarding data:

```sh
wemo-matter-bridge qr
```

Reset only if you are prepared to remove and re-add the bridge in Apple Home or
Google Home:

```sh
wemo-matter-bridge reset-onboarding
```

## WeMo Devices Missing

Run discovery:

```sh
wemo-matter-bridge scan-wemo
```

Then restart services:

```sh
wemo-matter-bridge restart
```

Common causes:

- WeMo devices are on a different VLAN
- multicast/SSDP is blocked
- WeMo device is offline
- controller app has stale Matter endpoint metadata

If device types or names changed after an upgrade, remove the bridge from the
controller app and commission it again.

## Existing OpenWrt Install Package Issues

Check which package format your OpenWrt build uses:

```sh
command -v opkg || command -v apk
```

Install matching artifacts:

```sh
opkg install /tmp/*.ipk
apk add --allow-untrusted /tmp/*.apk
```

Package artifacts are target-architecture specific. A Raspberry Pi 4 package
will not install on MT7628 hardware.

## Logs to Include in Bug Reports

```sh
logread | grep -Ei 'wemo|matter|chip'
wemo-matter-bridge status
wemo-matter-bridge scan-wemo
cat /etc/wemo-matter-bridge/matter-onboarding.json
```

Do not post private keys or WireGuard configs.
