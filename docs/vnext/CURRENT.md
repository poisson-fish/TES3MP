# Current state and next action

## Handoff

- **Direction:** OpenMW-backed authoritative cooperative multiplayer; see
  [README.md](README.md) and [DECISIONS.md](DECISIONS.md).
- **Milestone:** M3 in [PLAN.md](PLAN.md). Shared inventories cover containers,
  placed actors, corpse loot and living public equipment. Bulk Take All uses
  the coherent writer.
- **Next action (recommended):** authoritative pickup of ordinary unscripted
  placed items in the current interior, with atomic inventory transfer and
  persistent removal from the world.

## Implemented behavior

The [native host](../../apps/tes3mp-server/native/inventory_host.hpp) accepts
**native-inventory-6**. It discovers winning NPC, creature and container placements,
including overrides/deletions and repeated bases, sorted by stable identity.
The domain is 1–32 shared inventories and may contain only actors. Scripted actors/items,
leveled actor spawning, locks/traps and excessive inventories reject startup.
V3/v4/v5 retain their meanings and saved bindings; switching descriptors requires
explicit migration or a new campaign.

NPCs/creatures use stock storage and equipment selection. Owners bind before
loot consumes IDs; fresh campaigns use one fixed/leveled stream in player-role
order, then placement order.
Selected enchanted equipment remains unsupported.

Among actors, only content-defined corpses admit take/put. Living access rejects
at both service and runtime boundaries. Living actors expose placed identity and
all 19 public equipment prototypes, including empty appearances. Clients validate
presentation copies without registering transfer identities; identical snapshots
reuse them and later snapshots reconcile local drift. Private stacks, counts and
transfer revisions stay server-side.
Equipped corpse items can be looted: complete removal clears the slot,
partial ammunition removal retains it, and putting items into a corpse does not
select replacement equipment. Player-equipped source items remain protected.
Authentication, ownership, cell/reach, revision and durability checks still apply.
All owners share one registry/counter and image; uncertain writes close service.

Recovery validates storage type and preserves saved contents and slots without
rerolling. Desktop baselines retain distinct stacks; quick deposits resolve
proxies, player transfers request authoritative unequip, and inventory replacement
cancels borrowed drags. Corpse disposal stays disabled.
Take All now submits one revision-bound intent covering the entire source. Detached
stock transfers preserve instance fields, merge eligible stacks and clear emptied
corpse slots. One durable image precedes installation and publication; capacity,
stale input or save failure cannot transfer a prefix. The window stays open and
refreshes from committed baselines. Empty sources submit no command.
Committed notifications and ordinary empty nodes are retired without recycling
IDs, preventing exhaustion during repeated exchanges.
Living equipment and bulk Take All desktop visuals are user-confirmed.
Bulk commands require matching updated server and desktop builds;
existing v6 campaign images remain readable.

Chargen demonstrations still use the older runtime.

## Verification

Windows MSVC `scripts/setup_msvc_env.ps1 -PreferLatest`, RelWithDebInfo,
`build/vnext-product`. Individual targets and filters only; logs in `build/logs`.

| Filter/check | Evidence | Log |
|---|---|---|
| desktop GUI | corpse exchanges, living equipment and bulk Take All user-confirmed | user evidence; manual-bulk-test-20260917 |
| bulk-take-all | containers/NPCs/armed creatures, stock merging and slots, 64 stacks, capacity rejection without partial transfer, contention, failed/uncertain writes, recovery | bulk-take-all.log |
| bulk-canonical | same-tick contention, joint command/image durability, replay/stale rejection, file recovery and continuation | bulk-canonical.log |
| world-actor-inventories | NPC/creature public slots, empty appearances, private loot exclusion, appearance validation, interest/reconnect/stale-session guards, saved slots; inherited corpse regressions pass | bulk-actors.log |
| inventory replication | bulk command round trip, truncation and malformed-shape rejection; inherited equipment/baseline checks | bulk-protocol.log |
| builds | native tests, openmw, tes3mp_server | bulk-native-build.log / bulk-desktop-build.log / bulk-server-build.log |
| guards | GUI interception, patch registry, document budget/links | bulk-ui-contract.log / bulk-registry.log / docs-budget.log / docs-links.log |

Headless clients/transport are synthetic. No published-mod, TR or full-suite proof
is claimed.

## Limits

One interior, two players, 32 combined shared stores in v6, 64 nodes per store,
bounded loot expansion and a 1 MiB image. Living actor access,
companion sharing, theft/ownership rules, AI/combat/death and corpse disposal,
chargen/rewards, world items, item use, general scripts/Lua/custom records,
locks/traps, merchants/restocking/respawn, persistent time/RNG and cell scheduling
remain unfinished. Ownership and fractional wear persist but are not fully
projected. General campaign recovery and broad desktop/mod compatibility remain
unproven.
