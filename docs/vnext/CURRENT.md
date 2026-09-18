# Current state and next action

## Handoff

M3 in [PLAN.md](PLAN.md); constraints: [README.md](README.md), [DECISIONS.md](DECISIONS.md).
**Deferred until tonight:** Alice/Bob teleport round trip, latency/reconnect/restart and
ordinary-door contact/reversal. Broader reference/time/weather integration and
Tamriel Rebuilt two-client acceptance remain.

## Native cells

[Host](../../apps/tes3mp-server/native/inventory_host.hpp): **native-inventory-11**,
v10 fields, two interiors/wire IDs; requires fresh campaign and teleport-capable
clients. V8/v9/v10 unchanged. Bounds: two players,
32 shared inventories, 64 world items, one ordinary door and 32 teleport doors.

OpenMW resolves unscripted, unlocked, untrapped intercell doors. Activation validates
session/actor, placement, cell, revision and reach. Native-image commit precedes
destination publication; only requester position/epoch change. Direct transitions
and old-epoch movement reject.

Sessions pin occupied interiors. Empty cells release engine resources and freeze
doors; canonical state remains resident.

Format 6 preserves inventories, door state and ground-reference cell membership.
Reentry/recovery never initializes loot. Cell baselines suppress removed placements
and replace old container/teleport bindings. Clients suppress local door fallback,
ignore correction echoes and wait for matching destination state.

## Existing native behavior

Equipment/corpse loot/Take All/pickup/drop share one image. Ordinary doors use stock
motion at 30 Hz; one native mutation per tick, uncertain writes close service.
Contacts expire after ten ticks; latency may clip. No scripts/Lua,
locked/trapped/exterior teleports, followers, server physics/NPC contacts, AI/combat,
respawn, general streaming/recovery or persistent time/RNG. Movement/camera remain inherited.

## Verification

Windows RelWithDebInfo: `build/vnext-product`, `build/logs`.

| Filter | Evidence | Log |
|---|---|---|
| teleport-traversal | round trip/Bob stays, rejection/durability, stale motion, wire baselines, loot/restart | teleport-traversal.log |
| teleport-host | Morrowind/generated interiors, OpenMW destinations, unsupported-door exclusion, reentry/recovery | teleport-host.log |
| teleport-presentation (adapter) | reordered baselines, old-cell suppression, correction echo | teleport-presentation.log |

Builds: `teleport-build.log`, `teleport-adapter-build.log`, `teleport-client-build.log`
(openmw), `teleport-server-build.log` (tes3mp_server).
V10 host regression: `teleport-v10-regression.log`. Guards: `teleport-boundary.log`,
`docs-budget.log`, `docs-links.log`.

Graphical: clients connected; Alice reached B. Desktop locked. Resume
`build/teleport-graphical-v11-20260918/{launch,control}.py`; logs
`teleport-graphical-v11/`. Synthetic loadout; loot unchanged. Driver builds; revised
scene check unexercised. Requested acceptance and published mods remain unverified.
