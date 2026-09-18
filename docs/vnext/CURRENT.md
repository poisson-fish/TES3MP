# Current state and next action

## Handoff

- **Direction:** OpenMW-backed authoritative cooperative multiplayer; see
  [README.md](README.md) and [DECISIONS.md](DECISIONS.md).
- **Milestone:** M3 in [PLAN.md](PLAN.md). Shared interior inventories now include
  unscripted placed NPCs and creatures, ordinary starting equipment, and
  content-defined corpse looting through the coherent inventory writer. Living
  actors now replicate public equipment without private inventory contents.
- **Next action:** visually verify living NPC/armed-creature equipment on both
  updated desktop clients, including empty loadouts and reconnect/restart.

## Implemented behavior

The [native host](../../apps/tes3mp-server/native/inventory_host.hpp) accepts
**native-inventory-6**. It discovers winning NPC, creature and container placements,
including overrides/deletions and repeated bases, sorted by stable identity.
The domain is 1–32 shared inventories and may contain only actors. Scripted actors/items,
leveled actor spawning, locks/traps and excessive inventories reject startup.
V3/v4/v5 retain their meanings and saved bindings; switching descriptors requires
explicit migration or a new campaign.

NPCs/creatures use stock storage and equipment selection, including creature
skills and merchant exclusions. Owners bind before loot consumes IDs. Fresh
v6 campaigns consume one fixed/leveled stream in player-role order, then combined
placement order.
Selected enchanted equipment rejects until its effect services exist. This does
not activate AI, combat, scripts, death transitions or companion behavior.

Only content-defined corpses admit take/put. Living actor access is rejected at
both the service and runtime boundaries; their inventories are not broadcast as
containers. Their in-cell public equipment carries placed identity and all 19
slot prototypes, including empty NPC/unarmed-creature appearances. No private
stacks, counts or transfer revisions are sent. Desktop projection replaces local
equipment with ordinary presentation copies, validates records/slots before
replacement, and never registers them for transfers. Repeated snapshots reuse
unchanged presentation; local drift is reconciled on the next snapshot.
Equipped corpse items can be looted: complete removal clears the slot,
partial ammunition removal retains it, and putting items into a corpse does not
select replacement equipment. Player-equipped source items remain protected.
Authentication, ownership, cell/reach, revision and durability checks still apply.
All owners share one registry/counter and image; uncertain writes close service.

Recovery validates storage type, preserves saved slots/emptied inventories, and
never rerolls loot or equipment. Corpse baselines carry equipment bindings;
desktop installation preserves distinct stacks without splitting or scripts.
Quick deposits resolve proxies; player transfers request authoritative unequip;
inventory replacement cancels borrowed drags. Corpse disposal is disabled.
Take All preserves server slots but submits only one stack per click.
Committed notifications and ordinary empty nodes are retired without recycling
IDs, preventing exhaustion during repeated exchanges. Corpse looting, passive
refresh, shirt-disconnect recovery and two-client exchanges are user-confirmed.
Living equipment visuals and bulk Take All remain unconfirmed.
V6 requires the matching updated desktop build.

Chargen peers/upright rotation are user-confirmed; chargen uses the older runtime.

Inherited functionality includes all 12 TES3 inventory types, instance fields,
organic/capacity checks, player fixed/leveled starting inventory and ordinary
equipment, and private/public projection across all 19 slots. General scripts
and equipment effects remain unsupported.

## Verification

Windows MSVC `scripts/setup_msvc_env.ps1 -PreferLatest`, RelWithDebInfo,
`build/vnext-product`. Individual targets and filters only; logs in `build/logs`.

| Filter/check | Evidence | Log |
|---|---|---|
| corpse GUI | taking/passive refresh confirmed; equipped/spare shirt disconnect reproduced, saved case reconnects on both rebuilt clients | corpse-receive-replay.log / corpse-receive-build.log |
| world-actor-inventories | NPC/creature public slots, empty appearances, private loot exclusion, appearance validation, interest/reconnect/stale-session guards, saved slots; inherited corpse regressions pass | living-equipment-actors.log |
| placed-actors | winning overrides/deletion, repeated bases, ordering, script/spawn/budget rejection | placed-actors.log |
| world-actor-host | real Morrowind.esm plus generated actor cell, living public equipment, corpse loot, restart/late join, content mismatch rejection | living-equipment-host.log |
| inventory replication | public actors/all slots/empty appearances, round trips, truncation, ordering/size rejection and maximum packet budget; assertions enabled in release | living-equipment-protocol.log |
| builds | native tests, openmw, tes3mp_server | living-equipment-native-build.log / living-equipment-desktop-build.log / living-equipment-server-build.log |
| guards | patch registry, document budget/links | living-equipment-registry.log / docs-budget.log / docs-links.log |

Headless inventory clients/transport are synthetic. No published-mod, TR, complete
suite or baseline proof is claimed. Earlier chest evidence: `build/native-chest-final`.

## Limits

One interior, two players, 32 combined shared stores in v6, 64 nodes per store,
bounded loot expansion and a 1 MiB image. Living actor access,
companion sharing, theft/ownership rules, AI/combat/death and corpse disposal,
chargen/rewards, world items, item use, general scripts/Lua/custom records,
locks/traps, merchants/restocking/respawn, persistent time/RNG and cell scheduling
remain unfinished. Ownership and fractional wear persist but are not fully
projected. General campaign recovery and broad desktop/mod compatibility remain
unproven.
