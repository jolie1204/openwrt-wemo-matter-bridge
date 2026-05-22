# Release Notes

## Version

`vX.Y.Z`

## Highlights

- Intended user path:
- Download this artifact:
- Bridge package:
- WeMo device behavior:
- Matter onboarding:

## Artifacts

- Raspberry Pi 4 sysupgrade image, if this is a firmware release:
- Raspberry Pi 5 sysupgrade image, if this is a firmware release:
- OpenWrt package artifacts, if this is a package release:
- Debian package artifact, if this is a Raspberry Pi OS release:
- `SHA256SUMS`

## Upgrade Notes

- Existing Matter controllers may retain stale endpoint metadata after bridge
  capability changes. Remove and re-add the bridge if devices are missing or
  displayed with old capabilities.
- Release images generate unique Matter onboarding credentials on first boot.
  They do not ship with one shared setup passcode.

## Known Limitations

- This project is not described as an official Matter certified product.
- WeMo discovery requires LAN multicast/SSDP visibility.
- Raspberry Pi 5 support is experimental unless this release explicitly marks
  it validated.

## Checksums

See `SHA256SUMS`.
