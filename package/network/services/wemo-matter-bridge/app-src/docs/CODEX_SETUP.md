# Codex Setup Notes

This project is now intended to be built from the top-level OpenWrt tree.
Do not clone `openwemo-bridge-core` as a sibling repository for normal work.

From repository root:

```sh
./scripts/feeds update -a
./scripts/feeds install -a
make package/network/services/openwemo-bridge-core/compile V=s -j1
make package/network/services/wemo-matter-bridge/compile V=s -j1
```

For Raspberry Pi release artifacts:

```sh
scripts/release/build_rpi_release.sh --target rpi4
```

Relevant top-level docs:

- `../../../../../../README.md`
- `../../../../../../docs/RELEASE.md`
- `../../../../../../docs/MATTER_ONBOARDING.md`
