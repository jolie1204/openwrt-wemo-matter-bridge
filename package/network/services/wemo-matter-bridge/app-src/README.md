# WeMo Matter Bridge App Source

This directory contains the Matter bridge application source that is packaged by
the OpenWrt tree.

Public releases are built from the top-level OpenWrt repository. Users should
not clone a separate `openwemo-bridge-core` repository or assemble sibling
source trees for normal Raspberry Pi/OpenWrt installs.

Primary docs:

- `../../../../../README.md`
- `../../../../../docs/RASPBERRY_PI_QUICKSTART.md`
- `../../../../../docs/MATTER_ONBOARDING.md`
- `../../../../../docs/RELEASE.md`

The local `scripts/systemd/` and CMake files remain useful for developer
experiments on non-OpenWrt Linux hosts, but the supported public path is the
OpenWrt firmware/package workflow.
