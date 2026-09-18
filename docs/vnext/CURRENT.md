# Current state and next action

## Handoff

- **Direction:** OpenMW-backed authoritative cooperative multiplayer; see
  [README.md](README.md) and [DECISIONS.md](DECISIONS.md).
- **Milestone:** M3 in [PLAN.md](PLAN.md). Stationary pickup/drop is user-accepted.
- **Next action:** add one bound ordinary door's native state to the existing
  coherent session image, with bounded byte preflight, content binding and recovery.
  Preserve v8 campaigns; changing the domain requires an explicit descriptor version.

## Door preparation

[Stock callers](../../apps/openmw/mwworld/worldimp.cpp) now share
[door motion rules](../../apps/openmw/mwworld/doormotion.hpp): activation reversal,
partial-motion sound offsets, 90-degree/second rotation, endpoint clamping and
actor-contact direction. World retains collision rollback, AI and presentation.
Idle means stopped, including fully or partly open; explicit stock Idle activation
retains its close/snap behavior.

[OrdinaryDoor](../../apps/tes3mp-server/native/ordinary_door.hpp) prepares detached
ESM::DoorState values and sound intents. Moving steps require an explicit actor
collision query; blocked steps preserve rotation/direction. Invalid state, transforms
and duration reject before querying. Stock persistence stores current position plus
ANIM direction, and native validation rejects invalid ANIM values retained by ESM.
Loadout discovery resolves one winning placement and stable identity, rejecting
scripted, teleporting, locked/keyed or trapped targets and configured Lua services.

This is preparation, not a network authority cutover. No production door descriptor,
canonical transaction, bounded byte decoder, collision provider or native door
replication is wired. No new save file or competing live writer was introduced.

## Running inventory authority

The [native host](../../apps/tes3mp-server/native/inventory_host.hpp) remains
**native-inventory-8**: two players, 32 shared inventories and 64 active world items
in one interior. Equipment/all 19 slots, corpse loot, Take All and pickup/drop
share one durable image. Recovery never reloads looted content.

V8 uses shared stock camera/floor rays and model bounds for stationary placement;
misses or slopes >=30 degrees use the downward fallback. Clients install committed
positions without repositioning. Model bytes join saved content identity; v7 retains
player-position drops. Safe write rejection is atomic; uncertain writes close service.

## Verification

Windows MSVC RelWithDebInfo, `build/vnext-product`; logs in `build/logs`.

| Filter/check | Evidence | Log |
|---|---|---|
| ordinary-door | reversal, offsets, contact rollback, endpoints, stock field/save round trips, resumed motion, invalid-state rejection | door-state.log |
| placed-door | synthetic overrides/deletions, origin identity, unsupported targets, Lua rejection | door-placement.log |
| ordinary-door-loadout | Morrowind.esm:397586, in_c_door_arched, Seyda Neen Census and Excise Office; saved partial motion resumes/closes | door-real-loadout.log |
| builds | native tests and desktop link | door-final-build.log / door-desktop-build.log |
| guards | patch coverage and active documentation | door-registry.log / docs-budget.log / docs-links.log |

Door collision queries in tests are synthetic; no live door-client proof is claimed.
Inherited v8 evidence: `placement-query.log`, `placement-host.log`,
`placement-protocol.log`, `placement-world-items-regression.log` and
`placement-world-items-canonical.log`; user confirmed live dropping. Earlier v7
two-client reconnect/restart reached revision 33 with identical 3,588-byte images
in `build/manual-world-items-v7-20260917`. Container/corpse/equipment/Take All
presentation is also inherited user-confirmed evidence.

## Limits

Placement geometry is initial unscripted non-actor content plus committed items;
changed doors and animation remain unsimulated. Camera/movement authority is
inherited. Dynamic cells, scripts/Lua, AI/combat, locks/traps, merchants/respawn,
chargen/rewards and persistent time/RNG remain unfinished. General campaign
recovery, published mods and Tamriel Rebuilt remain unproven.
