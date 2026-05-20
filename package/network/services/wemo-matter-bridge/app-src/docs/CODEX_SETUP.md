# Codex Setup Guide

This guide is optimized for Codex/CLI automation on a clean Ubuntu/Debian host.

## 1. Create workspace
```bash
export WORKSPACE="${WORKSPACE:-$HOME/wemo-stack}"
mkdir -p "$WORKSPACE"
cd "$WORKSPACE"
```

## 2. Clone repositories
```bash
git clone https://github.com/jolie1204/wemo-matter-bridge.git
git clone https://github.com/jolie1204/openwemo-bridge-core.git
git clone https://github.com/jolie1204/connectedhomeip.git
git clone https://github.com/jolie1204/pupnp.git
```

## 3. Check out pinned baseline
```bash
git -C "$WORKSPACE/wemo-matter-bridge" checkout v0.1.0
git -C "$WORKSPACE/openwemo-bridge-core" checkout 4c173e6b15eb487dd75e4da62b2ec358b1677ce4
git -C "$WORKSPACE/connectedhomeip" checkout v1.5.0.1
git -C "$WORKSPACE/pupnp" checkout 1124f692772f673a0dc8d5371f50c0d334905b1c
```

## 4. Build dependencies and bridge
Build order:
1. `pupnp`
2. `openwemo-bridge-core`
3. `wemo-matter-bridge/matter-bridge-app`

```bash
cd "$WORKSPACE/pupnp"
./bootstrap || true
./configure
make -j

cd "$WORKSPACE/openwemo-bridge-core"
make -j UPNP_BASE="$WORKSPACE/pupnp"

cd "$WORKSPACE/wemo-matter-bridge/matter-bridge-app"
./build_wemo_bridge.sh
```

Expected output binary:
- `$WORKSPACE/wemo-matter-bridge/matter-bridge-app/out/ethernet/wemo-bridge-app`

## 5. Deploy and start
Preferred one-command deploy:
```bash
cd "$WORKSPACE/wemo-matter-bridge"
./scripts/install_bridge_stack.sh --workspace "$WORKSPACE"
```

If using an existing stack script:
```bash
cd "$WORKSPACE"
./bin/bridge_stack.sh stop
cp wemo-matter-bridge/matter-bridge-app/out/ethernet/wemo-bridge-app ./bin/wemo-bridge-app
./bin/bridge_stack.sh start
./bin/bridge_stack.sh status
```

## 6. Verify runtime
```bash
cd "$WORKSPACE/wemo-matter-bridge"
./scripts/wemo_bridge_health.sh --workspace "$WORKSPACE"
rg -n "WeMo bind|Added device|Device\\[" "$WORKSPACE/var/log/wemo_bridge.log" | tail -n 200
```

## 7. Commission
Use Google Home or Apple Home to add the bridge as a Matter accessory.

## 8. Known blockers and fast fixes

### A) `UpnpInit2` argument mismatch in `openwemo-bridge-core`
If build fails with `too many arguments to function 'UpnpInit2'`:
```bash
sed -i 's/UpnpInit2(ifname, port, DeviceUDN)/UpnpInit2(ifname, port)/' \
  "$WORKSPACE/openwemo-bridge-core/wemo_ctrl/wemo_ctrl.c"
make -C "$WORKSPACE/openwemo-bridge-core" -j UPNP_BASE="$WORKSPACE/pupnp"
```

### B) CHIP submodule files missing during `build_wemo_bridge.sh`
`build_wemo_bridge.sh` auto-bootstraps required submodules by default.

If you disabled bootstrap, re-enable by unsetting:
```bash
unset WEMO_SKIP_CHIP_SUBMODULE_BOOTSTRAP
```

## 9. Codex operational notes
- Keep all repos as siblings under one workspace.
- Avoid partial upgrades; move all pinned repos together.
- After capability/cluster changes, recommission bridge in controller apps.
