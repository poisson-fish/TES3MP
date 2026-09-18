# Current state and next action

## Handoff

- **Direction:** OpenMW-backed authoritative cooperative multiplayer; see
  [README.md](README.md) and [DECISIONS.md](DECISIONS.md).
- **Milestone:** M3 in [PLAN.md](PLAN.md). Shared inventories, actor equipment,
  corpse loot, Take All and stationary world pickup/drop use one writer.
- **Placement status:** user verified live dropping works and accepted this slice.
- **Next action:** inspect stock non-teleport door activation, motion and saved
  state; identify the smallest native-authority integration for one unscripted
  door in the bound interior.

## Implemented behavior

The [native host](../../apps/tes3mp-server/native/inventory_host.hpp) accepts
**native-inventory-8**. Stock OpenMW cursor/floor rays and rendered bounds now have
shared query helpers used by single-player and a headless server scene. Clients
send bounded camera/cursor input before inventory mutation. The server resolves
the stock 200-unit camera query; misses or slopes >=30 degrees use the original
downward ground fallback. Bounds center the actual dropped model, including gold
piles. No additional reach, supporting-surface or overlap rules were added.

Resolved stationary position, partial-stack subtraction and fresh world identity
commit in one durable image. Both clients install the committed position without
snapping it again. Safe write rejection leaves inventory/world state unchanged;
uncertain writes close service. Recovery never reloads looted placements or loot.

V8 adds resolved model bytes to saved content identity. Older descriptors retain
their meanings; v7 still drops at the player position. Changing versions requires
matching builds and a new campaign or explicit migration, never an automatic reset.

Inherited: up to 64 active world references, two players and 32 shared inventories
in one interior; winning placement overrides/deletions, whole-reference pickup,
stock instance fields and gold conversion, starting equipment/all 19 slots,
container/corpse transfers and atomic Take All. Complete ground baselines suppress
the original item domain. Authentication, stale-state checks and coherent recovery
remain in the same canonical writer.

## Verification

Windows MSVC RelWithDebInfo, `build/vnext-product`; focused logs in `build/logs`.

| Filter/check | Evidence | Log |
|---|---|---|
| item-placement | stock distance/fallback, slope threshold, upright yaw, scaled bounds and particle exclusion | placement-query.log |
| world-item-placement-host | real Morrowind records plus synthetic meshes: floor/table/slopes, malformed cameras, partial stacks/gold, committed-item support, atomic rejection, two synthetic clients, reconnect/restart and resource mismatch | placement-host.log |
| inventory replication | camera payload round trip, truncation and invalid-field rejection | placement-protocol.log |
| world-items / world-items-canonical | inherited contention, atomic durability and recovery regressions | placement-world-items-regression.log / placement-world-items-canonical.log |
| builds | native tests, desktop and adjacent tes3mp_server-placement.exe | placement-final-build.log / placement-desktop-build.log / placement-server-build.log |
| guards | UI interception and patch coverage | placement-ui-contract.log / placement-registry.log |

Live v8: user confirmed dropping works and accepted stationary placement as done.
The focused automated logs above supply the case-by-case placement evidence.

Earlier live v7 evidence used Morrowind.esm plus generated ManualWorldItems.esp,
"vNext World Items Test", `build/manual-world-items-v7-20260917`. User confirmed
pickup/drop presentation; both clients rejoined at revision 33 before/after restart
with an identical 3,588-byte image. Logs: `build/logs/manual-world-items-v7-20260917`.
Container/corpse, equipment and Take All visuals are also inherited user-confirmed
evidence. No published-mod, TR or complete-suite proof is claimed.

## Limits

The server placement scene covers initial unscripted non-actor geometry and
committed world items in one interior. Animated geometry and changed doors are
not simulated. Matching client/server model resources remain a deployment
requirement; camera/movement authority is inherited. Optional placement preview
is not implemented. Ordinary drops remain stationary; tumbling/bumping is excluded.

Dynamic cells, script/Lua/custom records, ownership/theft, living/companion access,
AI/combat/death, item use, locks/traps, merchants/respawn, chargen/rewards and
persistent time/RNG remain unfinished. General campaign recovery and broad
mod compatibility remain unproven.
