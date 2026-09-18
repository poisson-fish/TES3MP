# Current state and next action

## Handoff

- **Direction:** OpenMW-backed authoritative cooperative multiplayer; see
  [README.md](README.md) and [DECISIONS.md](DECISIONS.md).
- **Milestone:** M3 in [PLAN.md](PLAN.md). Shared interior inventories now include
  unscripted placed NPCs and creatures, ordinary starting equipment, and
  content-defined corpse looting through the coherent inventory writer.
- **Next action:** repeat two-client corpse exchanges and verify visible gear removal
  on two desktop clients, then reconnect/restart. Extend living actors' public equipment
  without exposing their private inventories.

## Implemented behavior

The [native host](../../apps/tes3mp-server/native/inventory_host.hpp) accepts
**native-inventory-6**, with the same fields as v5. It discovers winning NPC,
creature and container placements together, including overrides/deletions and
repeated base records, sorted by stable reference identity. The combined domain
is 1–32 shared inventories and may contain only actors. Scripted actors/items,
leveled actor spawning, locks/traps and excessive inventories reject startup.
V3/v4/v5 retain their meanings and saved bindings; switching descriptors requires
explicit migration or a new campaign.

NPCs use stock InventoryStore; creatures use InventoryStore or ContainerStore
according to the stock weapon flag. Owners bind before loot consumes IDs. Fresh
v6 campaigns consume one fixed/leveled stream in player-role order, then combined
placement order. NPC/creature ordinary equipment reuses stock selection, including
creature combat/magic/stealth skill specialization and merchant weapon exclusions.
Selected enchanted equipment rejects until its effect services exist. This does
not activate AI, combat, scripts, death transitions or companion behavior.

Only content-defined corpses admit take/put. Living actor access is rejected at
both the service and runtime boundaries; their inventories are not broadcast as
containers. Equipped corpse items can be looted: complete removal clears the slot,
partial ammunition removal retains it, and putting items into a corpse does not
select replacement equipment. Player-equipped source items remain protected.
Authentication, ownership, cell/reach, revision and durability checks still apply.
All owners share one registry/counter and image; uncertain writes close service.

Actor equipment stays in the existing full-slot image. Recovery validates actual
storage type before installation, retains saved equipment and emptied inventories,
and never loads loot or reruns selection. Shared-container baselines now optionally
carry validated equipment bindings; ordinary empty-equipment encoding is retained.
New desktop projection finds placed corpses, preserves distinct remote stacks,
reuses retired presentation nodes, and installs slots without local splitting or
script execution. Script-registration cleanup receives its explicit service.
Desktop taking/passive refresh are user-confirmed; deposits recognize actor models.
Player baselines now preserve distinct native stacks and install slots directly.
Stock rebuilding merged an equipped shirt with a received copy and disconnected
the receiver. The saved case failed before and reconnects on both rebuilt clients.
Repeated exchanges and visible gear removal await manual confirmation.
V6 requires the matching updated desktop build.

Chargen peers are user-confirmed; remote roots share stock upright rotation.
Chargen still uses the older runtime.

Inherited functionality includes all 12 TES3 inventory record types, instance
fields, condition/light time, charge and souls; stock organic/capacity checks;
player fixed/leveled starting inventory and automatic ordinary equipment; and
commands/private-public projection across all 19 clothing/armor/weapon slots.
The bounded scripted/constant Fortify Luck shirt diagnostic remains available,
but does not establish general script/effect execution.

## Verification

Windows MSVC `scripts/setup_msvc_env.ps1 -PreferLatest`, RelWithDebInfo,
`build/vnext-product`. Individual targets and filters only; logs in `build/logs`.

| Filter/check | Evidence | Log |
|---|---|---|
| prior peer checks | chargen safe-point visibility and upright rotation pass; user confirmed peers | chargen-visibility.log / actor-upright.log |
| corpse GUI | taking/passive refresh confirmed; equipped/spare shirt disconnect reproduced, saved case reconnects on both rebuilt clients | corpse-receive-replay.log / corpse-receive-build.log |
| world-actor-inventories | NPC/creature loot/equipment, guards, partial ammo, failed/uncertain durability, allocation-free installation, restart, wire/client assembly and presentation helpers | corpse-receive-actor-check.log |
| placed-actors | winning overrides/deletion, repeated bases, ordering, script/spawn/budget rejection | placed-actors.log |
| world-actor-host | real Morrowind.esm plus generated actor cell, equipped corpse loot, restart/late join, content mismatch rejection | world-actor-host.log |
| equipment-slots | inherited player equipment, durability and recovery regression | actor-equipment-slots.log |
| starting-equipment | player selection and saved-choice regression after creature skill extraction | actor-starting-equipment.log |
| inventory replication | ordinary/actor codec round trips; malformed equipment and truncation | actor-protocol.log |
| builds/guards | native tests, openmw, tes3mp_server; patch registry, document budget/links | actor-inventory-build.log / actor-desktop-build.log / actor-server-build.log / actor-registry.log / docs-budget.log / docs-links.log |

Headless inventory clients/transport are synthetic. No published-mod, TR, complete
suite or baseline proof is claimed. Earlier desktop chest evidence remains under
`build/native-chest-final`; earlier starting-loadout evidence remains in its logs.

## Limits

One interior, two players, 32 combined shared stores in v6, 64 nodes per store,
bounded loot expansion and a 1 MiB image. Living actor public projection/access,
companion sharing, theft/ownership rules, AI/combat/death and corpse disposal,
chargen/rewards, world items, item use, general scripts/Lua/custom records,
locks/traps, merchants/restocking/respawn, persistent time/RNG and cell scheduling
remain unfinished. Ownership and fractional wear persist but are not fully
projected. General campaign recovery and broad desktop/mod compatibility remain
unproven.
