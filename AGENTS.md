# Repository Guidance

- This tree tracks OpenWrt for the Wemo MT7628 bridge work. Prefer small, reviewable changes that follow existing OpenWrt target patterns.
- The default bridge target is `wemo-matter-bridge` and assumes 16M SPI NOR. Use `wemo-matter-bridge-8m` only for confirmed 8M units.
- The bridge bootloader uses boot A only. Do not add or depend on boot B, dual-image, `kernel_2`, `rootfs_2`, `Firmware_2`, or fallback-slot partitions unless explicitly requested.
- The bridge bootloader loads the kernel from flash offset `0x50000` and passes `root=/dev/mtdblock2`; keep the DTS partition order with `kernel` as MTD 1 and `rootfs` as MTD 2.
- Keep the kernel/rootfs image split at `KERNEL_SIZE := 2048k` so rootfs starts at flash offset `0x250000`.
- The bridge target has no antenna. Keep Wi-Fi disabled for `wemo-matter-bridge` unless explicitly requested.
- The 16M bridge DTS includes an MTD partition named `data`. Mount it at `/data` and keep Matter/OpenWeMo state under `/data/wemo-matter`.
- Matter packages live under `package/network/services/wemo-matter-bridge` and use local sources from `../sources`. Do not enable or build them unless explicitly requested.
- `uboot-snsv2` is a separate repository and may contain user edits. Inspect it when needed, but do not modify or reset it unless explicitly asked.
