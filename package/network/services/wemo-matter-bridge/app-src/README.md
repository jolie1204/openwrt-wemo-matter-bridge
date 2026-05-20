# wemo-matter-bridge

Open-source WeMo to Matter bridge for users affected by Belkin WeMo app/cloud
shutdowns. It exposes legacy WeMo switches and dimmers as Matter endpoints for
Google Home, Apple Home, and other Matter controllers.

If you are searching for terms like "WeMo discontinued support", "WeMo Google
Home fix", "Belkin WeMo replacement", or "WeMo Matter bridge", this repository
is the bridge implementation.

For multi-repo setup and dependency pinning, start at:
`https://github.com/jolie1204/wemo-bridge-stack`

## Scope
- Bridge-only architecture (no firmware flashing or device bootloader changes).
- Uses WeMo UDN as canonical identity.
- Persists `UDN -> endpointId` for endpoint stability.

## Who this helps
- Existing Belkin WeMo device owners who want local control after service loss.
- Users migrating WeMo devices into Matter ecosystems.
- Home setups where WeMo devices appear offline in cloud-only app flows.

## Raspberry Pi note
This stack runs well on Raspberry Pi (for example Pi 4/5, and Pi Zero-class
devices with lower performance expectations). For best stability, use wired
Ethernet when possible, keep a fixed IP for the bridge host, and run services
with `systemd` (see below) so they recover automatically after reboot.

## Repository layout
- `src/`: app sources and adapters
- `include/`: public headers
- `config/`: runtime config examples
- `docs/`: end-user and operator docs
- `.github/workflows/`: CI skeleton

## Documentation
- `docs/HOWTO.md`: detailed setup/commissioning/troubleshooting guide
- `docs/CODEX_SETUP.md`: codex-friendly deterministic setup runbook
- `COMPATIBILITY.md`: tested hardware/controller matrix
- `ROADMAP.md`: project priorities and release strategy
- `docs/RELEASES.md`: versioned release notes and pinned dependency SHAs

## Search keywords
We include common migration keywords so people can find this project:
`WeMo end of support`, `Belkin WeMo migration`, `WeMo offline fix`,
`WeMo dimmer Matter`, `Google Home WeMo replacement`, `open source WeMo bridge`.

## Dependency model
- Use sibling checkout: `../connectedhomeip` (single shared CHIP source tree)
- `openwemo-bridge-core` consumed from `../openwemo-bridge-core`

## Quick start
```bash
# 1) configure
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug

# 2) build
cmake --build build -j
```

For clean-machine, multi-repo setup (clone, pinned SHAs/tags, build order,
deploy/start), use `docs/HOWTO.md` -> `Clean-machine quick start`.

## Automated install/deploy
Use the installer script to build and deploy `wemo-bridge-app` (and `wemo_ctrl`
by default) into your workspace `bin/` directory.

```bash
./scripts/install_bridge_stack.sh
```

Useful options:
```bash
# install without restarting bridge_stack.sh
./scripts/install_bridge_stack.sh --no-restart

# only rebuild/deploy wemo-bridge-app
./scripts/install_bridge_stack.sh --skip-openwemo-build
```

## Auto-restart with systemd
Service templates are included:
- `scripts/systemd/wemo-ctrl.service`
- `scripts/systemd/wemo-bridge-app.service`
- env template: `config/wemo-bridge.env.example`

Typical setup (root):
```bash
sudo mkdir -p /etc/wemo-bridge
sudo cp config/wemo-bridge.env.example /etc/wemo-bridge/wemo-bridge.env
# edit paths in /etc/wemo-bridge/wemo-bridge.env

sudo cp scripts/systemd/wemo-ctrl.service /etc/systemd/system/
sudo cp scripts/systemd/wemo-bridge-app.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now wemo-ctrl.service wemo-bridge-app.service
sudo systemctl status wemo-ctrl.service wemo-bridge-app.service
```

Health check:
```bash
./scripts/wemo_bridge_health.sh
./scripts/wemo_bridge_health.sh --systemd
```

## Use with Google Home (Android/iOS)
1. Start both services and confirm they are running:
   - `wemo_ctrl`
   - `wemo-bridge-app`
2. Open Google Home app and tap `+` -> `Set up device` -> `New device`.
3. Choose `Matter-enabled device`, then scan bridge QR code or enter setup code.
4. Assign bridged devices to rooms and test:
   - Switches: on/off
   - Dimmers: on/off + brightness slider
5. If a device type or name looks stale, remove the bridge from Google Home and
   commission again after restarting the bridge stack.

## Use with Apple Home (iPhone Home app)
1. Ensure your Apple Home environment is ready for Matter (iPhone Home app and
   a Home hub such as HomePod/Apple TV on the same home).
2. Start `wemo_ctrl` and `wemo-bridge-app`.
3. In Home app, tap `+` -> `Add Accessory` -> scan Matter QR code or enter code.
4. Complete room assignment, then verify control:
   - On/off for switches
   - Brightness control for dimmers
5. If tiles do not reflect latest capabilities, remove bridge accessory and
   add it again after restarting the bridge services.

## Local openwemo integration
By default, the project builds without linking `openwemo-bridge-core`.

Enable real engine control APIs:
```bash
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Debug \
  -DWEMO_BRIDGE_USE_OPENWEMO_CORE=ON \
  -DOPENWEMO_BRIDGE_CORE_ROOT=<workspace>/openwemo-bridge-core
cmake --build build -j
```

Runtime DB paths can be overridden with:
- `WEMO_DEVICE_DB_PATH`
- `WEMO_STATE_DB_PATH`

## Smoke CLI
```bash
# list discovered devices and endpoint assignments
./build-openwemo/wemo-bridge-app list

# control by UDN
./build-openwemo/wemo-bridge-app set-on <udn>
./build-openwemo/wemo-bridge-app set-off <udn>
./build-openwemo/wemo-bridge-app set-level <udn> <0-100>
```

## Notes
- Keep CHIP-core patches minimal and upstreamable.
- Keep bridge-specific logic in this repo.
