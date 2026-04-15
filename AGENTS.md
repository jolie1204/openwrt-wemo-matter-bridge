# Repository Guidance

- This tree tracks OpenWrt for the Wemo MT7628 bridge work. Prefer small, reviewable changes that follow existing OpenWrt target patterns.
- The default bridge target is `wemo-matter-bridge` and assumes 16M SPI NOR. Use `wemo-matter-bridge-8m` only for confirmed 8M units.
- The bridge bootloader uses boot A only. Do not add or depend on boot B, dual-image, `kernel_2`, `rootfs_2`, `Firmware_2`, or fallback-slot partitions unless explicitly requested.
- The bridge bootloader loads the kernel from flash offset `0x50000` and passes `root=/dev/mtdblock2`; keep the DTS partition order with `kernel` as MTD 1 and `rootfs` as MTD 2.
- Keep the kernel/rootfs image split at `KERNEL_SIZE := 2048k` so rootfs starts at flash offset `0x250000`.
- Matter packages are intentionally out of scope for now. Do not enable or build Matter packages unless explicitly requested.
- `uboot-snsv2` is a separate repository and may contain user edits. Inspect it when needed, but do not modify or reset it unless explicitly asked.
