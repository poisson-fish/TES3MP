# Current state and next action

## Handoff

M3 in [PLAN.md](PLAN.md); rules: [DEVELOPMENT.md](DEVELOPMENT.md).
**Fresh-session visuals deferred by request:** time/weather, teleport round trips,
ordinary-door contacts, latency, late join, reconnect and restart.
Next integration slice: replace the fixed two-cell domain with bounded player-area
streaming and retire fixture-only bootstrap. Named Tamriel Rebuilt two-client
acceptance remains missing; M3 is open.

## Native runtime

[Host](../../apps/tes3mp-server/native/inventory_host.hpp): **native-inventory-13**,
V12 fields; either selector accepts `exterior X Y` with matching
`cell exterior:WORLDSPACE_ID:X:Y`. Requires a fresh campaign and native-environment
clients. Older domains retain their meanings.

Two cells retain two players, 32 inventories, 64 world items, one selected ordinary
door and 32 teleport doors. OpenMW resolves overrides/deletions/moved references,
terrain vertices/triangles and teleport destinations. Drops crossing exterior
edges reject. Empty cells release scenes/terrain and freeze doors; reentry/recovery
never reload loot. Teleports durably commit destination/epoch before publication.

[Environment](../../apps/tes3mp-server/native/environment.cpp) shares OpenMW calendar,
REGN weather and dedicated RNG, persisted coherently at 30 Hz. Up to 4,096 regions
advance regardless of occupancy/menus. Legacy environment/script writers reject.

No scripts/Lua, locked/trapped/unbound teleports, followers, server physics/NPC
contacts, AI/combat, respawn, adjacent-area streaming or native wait/rest UI.
Cell/ordinary-door startup selection remains; movement is inherited and intercell
traversal requires doors.

## Verification

Windows RelWithDebInfo, `build/vnext-product`; logs in `build/logs`.
Passed `tes3mp_native_loadout_tests` filters:

| Filter | Evidence/log |
|---|---|
| exterior-references | overrides/deletion/moved refs, identities, malformed rejection; exterior-references.log |
| exterior-host | terrain drop, durable teleport, split baselines, unload/reentry, recovery, descriptors/environment; exterior-host.log |
| teleport-host | interior regression; exterior-interior-regression.log |

Builds: `exterior-{build,server-build}.log`; docs: `docs-{budget,links}.log`.
Boundary: `test_inventory_and_object_migration_sources_remain_engine_independent`
(`exterior-boundary.log`) passed.
Model fixtures need `OSG_LIBRARY_PATH=deps/installed/x64-windows/bin`.
Host tests take a fresh scratch directory and config file; verified config:
`build/environment-loadout-config/openmw.cfg`. Evidence uses Morrowind/generated
plugins, not TR or graphical clients.

Inherited logs: `environment-{clock,weather,durability,application,loadout,host,time-wire,boundary,patch-registry}.log`;
`teleport-{traversal,host,presentation}.log`. Graphical V11 fixture:
`build/teleport-graphical-v11-20260918/{launch,control}.py`; V13 needs a fresh campaign.
