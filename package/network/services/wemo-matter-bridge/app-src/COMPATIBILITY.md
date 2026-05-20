# Compatibility Matrix

This file tracks real-world compatibility for users migrating away from
unsupported Belkin cloud flows.

## Tested Dependency Baseline
- `wemo-matter-bridge` branch: `master`
- `connectedhomeip` baseline: `v1.5.0.1` (recommended pinned baseline)
- Local patching strategy: bridge-managed patch files under
  `matter-bridge-app/patches/connectedhomeip/`

## WeMo Device Matrix
| Device Model | Example UDN Prefix | WeMo FW | On/Off | Dimming | Notes |
|---|---|---|---|---|---|
| WeMo Dimmer | `uuid:Dimmer-1_0-*` | TBD | Pass | Pass | Level preserved across on/off expected. |
| WeMo Light Switch | `uuid:Lightswitch-1_0-*` | TBD | Pass | N/A | Presented as on/off light. |

## Controller Matrix
| Controller | Commissioning | Device Naming | On/Off Control | Dimming Control | Known Caveats |
|---|---|---|---|---|---|
| Google Home | Pass | Mixed | Pass | Pass | May show stale generic tile after endpoint identity changes; recommission usually required. |
| Apple Home | TBD | TBD | TBD | TBD | Not yet validated on this baseline. |
| Alexa | TBD | TBD | TBD | TBD | Not yet validated on this baseline. |

## Runtime Environment Matrix
| Host OS | Network Mode | Result | Notes |
|---|---|---|---|
| Ubuntu 24.04 (x86_64) | Ethernet (`eth0`) | Pass | Current reference environment. |

## Known Issues
1. Controller-side stale cache may remap renamed/generic tiles after endpoint
   identity model changes.
2. Large initial descriptor reads can produce "No memory" logs; prioritize
   behavior validation over one-time commissioning log noise.

## Validation Checklist for New Reports
1. Confirm bridge and controller are on same LAN/VLAN.
2. Confirm both services are running:
   - `wemo_ctrl`
   - `wemo-bridge-app`
3. Capture startup bind lines from bridge log:
   - `WeMo bind (...)`
   - `Added device ... to dynamic endpoint ...`
4. Capture one failing action and correlate:
   - Google Home action time
   - bridge log entry
   - resulting WeMo state

## How to Contribute Compatibility Data
Open an issue with:
1. WeMo model and firmware version.
2. Controller app and platform version.
3. Exact `connectedhomeip` SHA used.
4. Relevant excerpts from `var/log/wemo_bridge.log`.
