# Compatibility

This matrix is intentionally conservative. Entries marked untested need real
validation before they should be described as supported.

## Runtime Targets

| Target | OpenWrt Target | Status | Notes |
|---|---|---|---|
| Raspberry Pi 4 | `bcm27xx/bcm2711`, `rpi-4-wemo-matter-bridge` | Primary | Recommended release image target. |
| Raspberry Pi 5 | `bcm27xx/bcm2712`, `rpi-5-wemo-matter-bridge` | Experimental | Build target exists; validate before broad release. |
| Raspberry Pi OS 64-bit | Debian package, `arm64` | Experimental | Native `.deb` package path; no OpenWrt/procd watchdog equivalent yet. |
| Belkin MT7628 bridge | `ramips/mt76x8`, `wemo-matter-bridge` | Project appliance | Existing custom hardware target. |

## WeMo Devices

| Device | Matter Exposure | Status | Notes |
|---|---|---|---|
| WeMo Switch | On/Off | Supported/needs more validation | Legacy LAN UPnP control path. |
| WeMo Mini / WeMo Plug | On/Off Plug-in Unit | Supported/needs more validation | Presented as a Matter on/off plug when detected as socket/plug, including newer FreeRTOS plug firmware. |
| WeMo Dimmer | Dimmable Light | Supported/needs more validation | Exposes On/Off and Level Control. |
| WeMo Insight / Insight V2 | On/Off Plug-in Unit | Experimental | On/off is exposed to Matter as a plug. Insight power telemetry is collected locally and available with `wemo-matter-bridge insight`; Matter electrical measurement exposure is not implemented yet. |

## Matter Controllers

| Controller | Status | Notes |
|---|---|---|
| Google Home | Tested in local deployments | Controller cache may require recommissioning after endpoint capability changes. |
| Apple Home | Untested/needs validation | Expected to work as a standard Matter bridge, but validate before release claims. |
| SmartThings | Future | Untested. |
| Alexa | Future | Untested. |

## Network Requirements

- Bridge and WeMo devices must share LAN multicast/SSDP visibility.
- Wired Ethernet is recommended for Raspberry Pi.
- Matter controller must be able to reach the bridge on the LAN.
- This project is local-first and does not require Belkin cloud access.
