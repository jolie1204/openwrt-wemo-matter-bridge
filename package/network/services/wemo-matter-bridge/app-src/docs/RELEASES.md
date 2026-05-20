# Release Notes

## v0.1.0 (2026-02-18)

First public baseline focused on WeMo survival/migration to Matter with stable
bridged control and reproducible dependency pins.

### Dependency Pins
- `wemo-matter-bridge`: `cb9a49676af2c4eb8143beeac3705973a0434c34`
- `openwemo-bridge-core`: `4c173e6b15eb487dd75e4da62b2ec358b1677ce4`
- `connectedhomeip`: `v1.5.0.1` (`8effa808dd9fa195ec0294f0ad67c80a86dd4975`)
- `pupnp`: `1124f692772f673a0dc8d5371f50c0d334905b1c`

### Highlights
- WeMo LAN integration via `openwemo-bridge-core/wemo_engine`.
- Dimmable WeMo devices exposed with Matter Level Control.
- Suppression logic for transient synthetic level writes during On/Off settle.
- Deterministic endpoint publish order for more stable controller mapping.
- Installer script: `scripts/install_bridge_stack.sh`.
- Runtime reliability additions:
  - `systemd` service templates under `scripts/systemd/`
  - health-check script: `scripts/wemo_bridge_health.sh`
- Setup/onboarding docs for Google Home and Apple Home.

### Known Limitations
- If bridge capabilities/identity change between builds, controller apps may
  keep stale endpoint metadata. Workaround: remove bridge and recommission.
- Requires LAN multicast/UPnP visibility for WeMo discovery and stable control.
- Current bridge profile is Ethernet-oriented (no BLE/Thread/Wi-Fi transport
  profile in this app build configuration).
- No packaged installer artifacts yet; deployment is source-based using scripts.

### Upgrade Notes
- Treat this as a pinned stack release. Avoid upgrading one repo in isolation.
- If moving to a different CHIP revision, retest commissioning and endpoint
  behavior before production use.
