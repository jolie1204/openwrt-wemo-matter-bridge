# Roadmap

## Mission
Keep legacy WeMo LAN devices usable through Matter after Belkin support loss,
with reliability first and low operational burden for non-expert users.

## Guiding Principles
1. Local-first operation (no cloud dependency).
2. Stable behavior across restarts and recommissioning.
3. Small, auditable patches over large framework forks.
4. Reproducible builds pinned to known-good dependency SHAs.

## Near-Term Priorities
1. Baseline stability
- Pin `connectedhomeip` to a tested commit/tag and publish it.
- Keep WeMo device identity stable (UDN-based and deterministic endpoint order).
- Eliminate behavior regressions in on/off and dimmer level handling.

2. Commissioning robustness
- Reduce "generic Matter device" and stale identity mapping issues in controllers.
- Document clean recommission and cache reset procedures by controller app.

3. Operator docs and supportability
- Provide a single detailed HOWTO for install, commissioning, updates, and rollback.
- Provide symptom-based troubleshooting with exact log locations and commands.

## Mid-Term Priorities
1. Compatibility coverage
- Expand tested matrix for WeMo models, firmware variants, and controllers.
- Track known caveats per controller (Google Home, Apple Home, Alexa).

2. Packaging and release
- Publish versioned release notes with:
  - pinned `connectedhomeip` SHA
  - required local patches
  - known issues
  - upgrade and rollback instructions
- Add signed release artifacts where feasible.

3. Reliability tooling
- Add startup health checks and runtime readiness checks.
- Add repeatable smoke tests (discover, toggle, level, restart persistence).

## Long-Term Priorities
1. Zero-touch update path
- Documented upgrade path with canary verification and fast rollback.

2. Reduced integration risk
- Keep a minimal compatibility layer for CHIP version drift.
- Contribute generic fixes upstream when possible.

3. Community support model
- Use issue templates that collect logs and environment details.
- Maintain a "known issues + workarounds" page tied to release tags.

## Definition of Done for Each Release
1. Build reproducible from clean checkout using documented steps.
2. Commissioning verified on at least one Google Home setup.
3. On/off and dimmer behavior validated for representative devices.
4. Restart behavior verified (`wemo_ctrl` + bridge app survive managed restart flow).
5. Release notes include exact dependency SHAs and migration notes.
