# NetCam sensor bridge image build verification

Developer deployment record, 2026-10-09. This is not a public feature announcement.
Installation and Google Home acceptance are separate from the build checks below.

## Reproduction inputs

- Profile: `ramips/mt76x8`, `wemo-matter-bridge` 16M.
- Configuration: `configs/wemo-matter-bridge-16m.config`.
- Pinned feeds: `configs/wemo-matter-bridge.feeds.conf`.
- Connectedhomeip: `8effa808dd9fa195ec0294f0ad67c80a86dd4975` with the four
  explicitly selected submodules in the package Makefile.
- Download archive SHA256:
  `f5c2d53253864eb4b7fbe5cab101a37521cb4416a21ec8f6fd954a2056359cc3`.
  Two downloads produced the same checksum. Pinning it replaces `skip`, which
  caused the download rule to fetch the source again on every invocation.
- Initial workspace had no active configuration, toolchain, staging directory,
  target build directory or images. `make defconfig`, downloads and the full
  toolchain/kernel/package build were performed.

The first quiet full build failed at the Matter application. The packaging
`files/BUILD.gn`, which replaces the development BUILD.gn during preparation,
omitted `endpoint_identity_registry.cpp`. Both build lists now include it.
The repaired full `make -j6 V=s` completed with exit status 0; the application
completed all 468 work items including the identity source, main and final link.
Final package metadata and image manifest both identify Matter r24.

## Image results

Output directory: `bin/targets/ramips/mt76x8/` (ignored generated artifacts).

| Image | Bytes | SHA256 |
| --- | ---: | --- |
| `openwrt-ramips-mt76x8-wemo-matter-bridge-squashfs-sysupgrade.bin` | 7799119 | `87599c56948b3309c50236332fac652123def39fdfe1ff89cb5517e4011f4448` |
| `openwrt-ramips-mt76x8-wemo-matter-bridge-initramfs-kernel.bin` | 7603013 | `af27e49fcbbabbedc4f1332a9bc2a02dc8c21b1c5ccd3b7876af0bf51ba568e5` |

The manifest includes core `0.1-r22`, Matter `2025.12.01~8effa808-r24`, data
service `1-r3`, board utilities `2`, SQLite CLI `3.52.0-r1` and OpenSSL `3.5.5-r1`.
The base OpenWrt release remains `25.12.2`, `r32802-f505120278`.

Checks performed on the sysupgrade image:

- Legacy uImage header and payload CRC32 pass; Linux/MIPS/kernel/LZMA fields and
  load address `0x80000000` are correct.
- Kernel including its header is 1945725 bytes, within the 2097152-byte slot.
- Squashfs starts at image offset `0x200000`; total image fits `0x9d0000`.
- Decompressed kernel ends with the exact compiled 16M profile DTB. Its eight
  partition labels, offsets and sizes match the operating device, including
  the separate data partition. Metadata supports `belkin,wemo-matter-bridge`
  with compatibility version `1.0`.
- The extracted application is 1262343 bytes, SHA256
  `ee2de26c2894c3f68112f647bb472817569cb87d84248f8124aff10ac42f6f82`.
  All 24 allocated sections match the corresponding unstripped debug ELF.
- The built boot service runs defaults at START10, before data START12,
  controller START90, application START98 and watchdog START99. This supports
  the reviewed first-boot service hold, whose actual effect must be checked
  after installation.
- Strict SSH/SCP transfer to the operating bridge matched the image SHA256.
  The operating device accepted `sysupgrade -T` with exit status 0. No flash was
  performed during these build checks.

Six isolated host suites and twenty Python helper tests passed before image
construction. Build, manifest, ELF, metadata and DTB evidence are private local
records; they contain no claim of live sensor or Google Home acceptance.

For installation and recovery use
[NETCAM_BRIDGE_FIRMWARE_UPGRADE.md](NETCAM_BRIDGE_FIRMWARE_UPGRADE.md).
The full initramfs memory budget and actual UART RAM boot are checked separately
before relying on that recovery image.
