# WeMo Survival HOWTO (Post-Belkin Support)

This guide is for users who still have working WeMo devices on LAN but need a
local Matter bridge so those devices remain usable from modern home apps.

## 1. What You Are Building
You are running two local services:
1. `wemo_ctrl` (LAN WeMo discovery/control engine)
2. `wemo-bridge-app` (Matter bridge exposing WeMo devices to controllers)

`wemo-bridge-app` talks to `wemo_ctrl` over local IPC, then publishes bridged
Matter endpoints for apps like Google Home.

## 2. Recommended Baseline
Use a pinned CHIP baseline rather than bleeding edge.

Recommended:
1. `connectedhomeip` on `v1.5.0.1` or a pinned commit on a stable release branch
2. `wemo-matter-bridge` current `master`
3. `openwemo-bridge-core` matching bridge integration expectations

If you change CHIP versions, expect recommissioning and re-validation.

## 3. Workspace Layout
Place repos side-by-side:

```text
<workspace>/
  connectedhomeip/
  openwemo-bridge-core/
  wemo-matter-bridge/
```

`wemo-matter-bridge/matter-bridge-app` expects `../../connectedhomeip`.

### Clean-machine quick start (reproducible)
```bash
export WORKSPACE="${WORKSPACE:-$HOME/wemo-stack}"
mkdir -p "$WORKSPACE" && cd "$WORKSPACE"
git clone https://github.com/jolie1204/wemo-matter-bridge.git
git clone https://github.com/jolie1204/openwemo-bridge-core.git
git clone https://github.com/jolie1204/connectedhomeip.git
git clone https://github.com/jolie1204/pupnp.git
```

Check out pinned baselines:
1. `wemo-matter-bridge`: `v0.1.0`
2. `openwemo-bridge-core`: `4c173e6b15eb487dd75e4da62b2ec358b1677ce4`
3. `connectedhomeip`: `v1.5.0.1`
4. `pupnp`: `1124f692772f673a0dc8d5371f50c0d334905b1c`

Notes:
1. `matter-bridge-app/build_wemo_bridge.sh` now bootstraps required CHIP
   submodules for Linux bridge builds on fresh clones.
2. If your environment does not have `$WORKSPACE/bin/bridge_stack.sh`, use
   `wemo-matter-bridge/scripts/install_bridge_stack.sh` and systemd services.
3. If `openwemo-bridge-core` build fails with:
   `error: too many arguments to function ‘UpnpInit2’`, patch once:
```bash
sed -i 's/UpnpInit2(ifname, port, DeviceUDN)/UpnpInit2(ifname, port)/' \
  "$WORKSPACE/openwemo-bridge-core/wemo_ctrl/wemo_ctrl.c"
```

### Raspberry Pi deployment note
You can run this stack on Raspberry Pi. Recommended for reliability:
1. Prefer wired Ethernet over Wi-Fi when available.
2. Use `systemd` units (`wemo-ctrl.service`, `wemo-bridge-app.service`) so both
   daemons survive reboot and transient failures.
3. On Pi Zero-class hardware, expect slower builds and discovery bursts.

## 4. Build Steps

### 4.1 Build `wemo-bridge-app` (Matter bridge)
```bash
cd wemo-matter-bridge/matter-bridge-app
./build_wemo_bridge.sh
```

Expected output:
`wemo-matter-bridge/matter-bridge-app/out/ethernet/wemo-bridge-app`

Notes:
1. Build script may apply bridge-managed CHIP patches from
   `matter-bridge-app/patches/connectedhomeip/`.
2. If a patch is not applicable to your pinned CHIP revision, script may warn
   and continue.

### 4.2 Build `wemo_ctrl`
Build from `openwemo-bridge-core` using its documented build steps.

### 4.3 One-command build and deploy (recommended)
From `wemo-matter-bridge` repo root:

```bash
./scripts/install_bridge_stack.sh
```

This script:
1. Builds `wemo-bridge-app`
2. Builds `openwemo-bridge-core` (`wemo_ctrl`) by default
3. Copies binaries into `<workspace>/bin`
4. Restarts stack via `<workspace>/bin/bridge_stack.sh` when present

Common variants:
```bash
# do not restart services
./scripts/install_bridge_stack.sh --no-restart

# only rebuild and deploy bridge app
./scripts/install_bridge_stack.sh --skip-openwemo-build
```

### 4.4 Optional: install systemd services for auto-restart
Service templates in this repo:
1. `scripts/systemd/wemo-ctrl.service`
2. `scripts/systemd/wemo-bridge-app.service`
3. `config/wemo-bridge.env.example`

Install (requires root):
```bash
sudo mkdir -p /etc/wemo-bridge
sudo cp config/wemo-bridge.env.example /etc/wemo-bridge/wemo-bridge.env
# edit /etc/wemo-bridge/wemo-bridge.env to match your install paths

sudo cp scripts/systemd/wemo-ctrl.service /etc/systemd/system/
sudo cp scripts/systemd/wemo-bridge-app.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now wemo-ctrl.service wemo-bridge-app.service
sudo systemctl status wemo-ctrl.service wemo-bridge-app.service
```

Health checks:
```bash
./scripts/wemo_bridge_health.sh
./scripts/wemo_bridge_health.sh --systemd
```

## 5. Deploy and Start

If your environment includes a helper script (for example `bin/bridge_stack.sh`):
```bash
<workspace>/bin/bridge_stack.sh stop
cp wemo-matter-bridge/matter-bridge-app/out/ethernet/wemo-bridge-app <workspace>/bin/wemo-bridge-app
<workspace>/bin/bridge_stack.sh start
<workspace>/bin/bridge_stack.sh status
```

Verify:
1. `wemo_ctrl` is running
2. `wemo-bridge-app` is running
3. Bridge log path is known (typically `var/log/wemo_bridge.log`)

## 6. Commission to Google Home
1. Put bridge in commissionable mode (normal startup for bridge-app on fresh fabric).
2. Add Matter device in Google Home.
3. Wait until all endpoints appear.
4. Validate each important device:
   - On/off for switches
   - Level control for dimmers

Detailed app flow:
1. Google Home -> `+` -> `Set up device` -> `New device`.
2. Choose `Matter-enabled device`.
3. Scan QR code (or enter pairing code).
4. Assign rooms/names, then run quick validation on each room.

## 7. Commission to Apple Home (iPhone)
1. Ensure Apple Home is set up with a Home hub (HomePod or Apple TV) for stable
   Matter control.
2. In Home app on iPhone, tap `+` -> `Add Accessory`.
3. Scan Matter QR code or enter the setup code manually.
4. Assign each bridged accessory to room/category.
5. Validate:
   - Switches toggle correctly.
   - Dimmers expose brightness slider and retain user-selected level behavior.
6. If capabilities are stale after upgrades, remove the bridge accessory from
   Home app, restart bridge services, and commission again.

## 8. Post-Commission Validation
Use logs to confirm endpoint binding:

```bash
rg -n "WeMo bind|Added device|Device\\[" var/log/wemo_bridge.log | tail -n 200
```

You should see entries like:
1. `WeMo bind (dimmable): Kitchen <- uuid:Dimmer-...`
2. `Added device Kitchen to dynamic endpoint ...`

## 9. Troubleshooting by Symptom

### Symptom A: Devices show offline
1. Confirm both daemons are running.
2. Confirm LAN reachability and multicast/UPnP availability.
3. Check startup log for discovery/bind lines.
4. Restart stack after confirming network interface is correct.

### Symptom B: Toggling one tile controls another device
1. Check bridge log to confirm both devices are discovered with correct names.
2. Recommission bridge in controller app.
3. If still wrong, controller may have stale endpoint identity cache.
4. Keep endpoint assignment deterministic (UDN sorting) and avoid changing
   identity model frequently between releases.

### Symptom C: Dimmers do not appear dimmable
1. Confirm `supports_level` is true for dimmer device in discovery.
2. Confirm Level Control cluster is present on dynamic endpoint.
3. Remove/re-add bridge in controller if cluster capability changed after
   previous commissioning.

### Symptom D: Off action changes dim level
1. Verify bridge logs for synthetic min-level suppression.
2. Ensure no stale binary is running after rebuild/deploy.

## 10. Upgrade Strategy (Safe)
For each upgrade:
1. Pin target CHIP SHA.
2. Build and deploy in maintenance window.
3. Validate with a smoke sequence:
   - discover
   - one on/off per room
   - one dimmer level change
   - restart stack and verify state behavior
4. Only then mark release as known-good.

## 11. Rollback Strategy
Keep previous known-good binaries and SHAs.

Rollback flow:
1. Stop stack.
2. Restore previous `wemo-bridge-app` binary.
3. Restore previous dependency pin if required.
4. Restart stack.
5. Confirm old behavior returns before further changes.

## 12. Support Bundle for Bug Reports
When filing issues, include:
1. `wemo-matter-bridge` commit SHA
2. `connectedhomeip` SHA/tag
3. Controller app/version
4. Device model(s)
5. Relevant excerpts from:
   - `var/log/wemo_bridge.log`
   - `var/log/wemo_ctrl.log`
6. Exact timestamp and action performed

This makes mapping/control bugs diagnosable quickly.
