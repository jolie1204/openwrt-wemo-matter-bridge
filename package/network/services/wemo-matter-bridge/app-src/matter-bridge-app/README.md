# matter-bridge-app (out-of-tree)

This is an out-of-tree Matter bridge app workspace that builds a commissionable
`wemo-bridge-app` binary without modifying CHIP source.

It uses a shared sibling checkout at `../../connectedhomeip`.

## Ethernet-only profile
The GN args are set to disable BLE/Thread/Wi-Fi support and run as an
Ethernet-based bridge host.

## Build
```bash
cd matter-bridge-app
./build_wemo_bridge.sh
```

Output binary:
- `matter-bridge-app/out/ethernet/wemo-bridge-app`

## Notes
- Uses `HOME=/tmp` during build to avoid sandboxed writes under `~/.zap`.
- This step intentionally reuses CHIP bridge-app source files while keeping
  all build orchestration outside the CHIP tree.
