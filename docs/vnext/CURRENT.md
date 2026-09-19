# Current state and next action

M3 in [PLAN.md](PLAN.md) remains open. Live verification is deferred by request.
**Noran Grotto** TR travel setup is ready: one interior, nine exteriors (x=37–39/y=-31–-29),
Nedothril/Padomaic Ocean regions. Level 1/seed 0 inspection found no scripts or
unsupported containers. Next: live travel/weather acceptance. Noran has return
teleports but no swinging doors; door checks need another location.
Warehouse's 26-area campaign rejects `contain_corpse10` in exterior 11,-33.
Scripts/Lua remain deferred to M5.

[Host](../../apps/tes3mp-server/native/inventory_host.hpp): **native-inventory-15**
requires fresh campaigns and leveled-actor-capable clients. OpenMW selects initial
unscripted living actors using campaign level/seed. Selected/none outcomes persist
with inventories/doors; recovery never rerolls. Clients suppress local spawning.
AI, combat, respawn, leveled authored corpses, scripts/Lua and locked/trapped
inventories remain unsupported.

V14 streaming remains: 1–256 cells, occupied interiors and both players' 3×3
exterior neighborhoods; unloaded doors freeze, weather continues, inventories persist.
Bounds: 1,024 stores/leveled markers, 128 actors per neighborhood, 8,192 ground
references, 192 items/cell, 128 ordinary doors/cell, 4,096 doors total.
[Bootstrap](../../apps/tes3mp-server/native/bootstrap.cpp) exports V15 atomically.
`--help` covers discovery/spawn/single-cell mode. Read-only
`--inspect-area CONFIG START [RADIUS]` reports connected cells, scripts, inventories,
actors, doors and regions; runtime startup remains required.

Build: `build/vnext-product`; **openmw.exe** rebuilt.
Inspector compiled/linked against existing libraries. Older Ninja reset the build
log during cost inspection; unrelated rebuilding was stopped. Future builds may
recompile dependencies.

Passing: `tes3mp_native_loadout_tests` filters `leveled-actors`,
`leveled-actor-persistence`, `player-area-discovery`;
`tes3mp_inventory_replication_tests neighborhoods`. Evidence in `build/logs`:
`m3-leveled-actors-04.log`, `m3-leveled-persistence-03.log`,
`m3-area-discovery-02.log`, `m3-neighborhoods-01.log`,
`m3-bootstrap-fixture.log`, `m3-bootstrap-failures.log`.

TR runtime: `build/m3-tr-noran`, radius 1, incoming-door spawn
`338.515:-184.765:-836`, mod Lua excluded. Alice/Bob credentials/configs prepared;
`launch.ps1 -Role server|Alice|Bob`, loopback port 25616. Startup/restart passed:
UDP listener, advancing canonical save, empty stderr. Servers stopped; save retained;
no clients launched. Evidence: `build/logs/m3-tr-noran/startup.json`.
Inspection: `build/logs/m3-inspect-noran-grotto.log`. CLI discovery/argument checks:
`build/logs/m3-inspect-area-check.log`. Ordered plugin hashes/manifest/logs:
`build/logs/m3-v15-acceptance.json`.

Inherited Warehouse acceptance: two-client visuals, loot/container transfers,
reconnect/restart; observed transfer/drop latency 164–166 ms.
Saves: `build/acceptance-v14-tr-warehouse`; evidence: matching `build/logs` directory.

Pending live: exterior crossings, teleports, separation, unload/return; matching
door motion, obstruction/reversal, latency; regional weather transition on both
clients. Doors/weather must also converge after late join and reconnect.
