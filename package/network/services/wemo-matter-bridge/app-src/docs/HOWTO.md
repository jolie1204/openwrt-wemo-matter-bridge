# Developer HOWTO

The old standalone, multi-repo bridge setup has been retired for public use.
This source directory is now vendored into the OpenWrt tree, alongside the
WeMo LAN control package source.

Use the top-level docs instead:

- `../../../../../../README.md`
- `../../../../../../docs/RASPBERRY_PI_QUICKSTART.md`
- `../../../../../../docs/MATTER_ONBOARDING.md`
- `../../../../../../docs/TROUBLESHOOTING.md`
- `../../../../../../docs/RELEASE.md`

For public releases, build Raspberry Pi/OpenWrt artifacts from the repository
root:

```sh
scripts/release/build_rpi_release.sh --target rpi4
scripts/release/package_release_assets.sh
```

For OpenWrt package development:

```sh
make package/network/services/openwemo-bridge-core/compile V=s -j1
make package/network/services/wemo-matter-bridge/compile V=s -j1
```

The `openwemo-bridge-core` name remains as an internal OpenWrt package name.
It no longer implies a separate public source repository.
