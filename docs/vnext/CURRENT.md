# Current state and next action

## Handoff

- **Direction:** OpenMW-backed authoritative cooperative multiplayer; see
  [README.md](README.md) and [DECISIONS.md](DECISIONS.md).
- **Milestone:** M3 in [PLAN.md](PLAN.md). M2's bounded headless exit is met:
  two actor contexts, shared item lifecycle, scripted/enchanted equipment,
  isolated failure and coherent save/restore. Conditional retirement follows
  production caller cutover. Equipped-shirt drop is not an integration blocker.
- **Next action:** extend `CanonicalCommandReducer::commitPrepared` and
  `CanonicalDurabilityPort`/`CanonicalPersistenceFile` to commit the prepared
  native session image and command disposition together, then install before
  publication. Route the existing put/take caller through that boundary.
- **Session scope:** 2–3 related bounded slices sequentially; inspect, implement,
  verify narrowly, review and commit. Replace this handoff; no planning files.
- **Base:** this change continues d59c62816f. The migration checkpoint remains
  8850e745c298c6de629ef9a5a26bbdddf6aa56e3.

## Production boundary and exact gap

Authentication establishes a connection session. The
[coordinator](../../apps/tes3mp-server/connection_session_coordinator.cpp) now uses
[InventoryCommandBinding](../../apps/tes3mp-server/inventory_command_binding.hpp)
for inventory intake: connection session/generation must match the decoded
request and active canonical session; player/entity identity comes from the
server. Deferred native preparation/commit rechecks that binding.

**Production mutations still use CanonicalInventoryWorld.** ServerApplication
pumps intake into CanonicalCommandReducer, stages inventory interest messages,
commits canonical durability, then pumps outbound queues. Join/resume/resync also
project the canonical inventory. `main.cpp` does not construct the native service.
No authenticated socket command has reached EquipmentRuntime, and no desktop
clients have been demonstrated on this path.

The exact blocking application service is canonical tick persistence: its
inventory field accepts only the old canonical inventory, while native inventory
previously committed its separate file. Calling native execution directly from
dispatch would split gameplay installation from durable command acknowledgment.
The new preparation boundary removes the native-side obstacle; joint server
persistence, reduction, startup and join/resync composition remain unwired.

## Implemented native boundary

[EquipmentRuntime](../../apps/tes3mp-server/native/equipment_runtime.hpp) now
separates transfer preparation from commit. A move-only preparation owns detached
engine candidates, result and the existing coherent session encoding. Commit
checks runtime lifetime, revision and live bindings before invoking a trusted
[session committer](../../apps/tes3mp-server/native/session_commit.hpp). Accepted
permits nonthrowing installation/publication; rejection permits retry; uncertainty
closes the runtime. Consumed and stale preparations cannot reach persistence.
Existing transfer/drop/take callers use this same implementation and existing
file durability; no new save format or snapshot layer was added.

[InventoryService](../../apps/tes3mp-server/native/inventory_service.hpp) owns
WorldModel, LocalScripts and EquipmentRuntime for its lifetime. Loaded content
and readers outlive it. Two fixed trusted PlayerIds bind actors; those roles and
wire item/container mappings bind recovery. It accepts the selected plain-shirt
put/take intent, checks shape/count/revision/ownership/cell/reach, and projects
installed engine state directly into existing owned inventory baseline types.
ESM instance IDs map reversibly to wire stack IDs; dormant nodes and signed
counts remain engine-owned. It contains no canonical inventory mirror.

The adapter is a separate engine-dependent application target. Independent
networking remains unchanged. Its delivery uses the existing interest encoder,
OutboundQueueSet and client receive implementation. This is exercised only with
synthetic authentication/transport; it is not production command integration.

## Verification

Windows MSVC `scripts/setup_msvc_env.ps1 -PreferLatest`, RelWithDebInfo,
`build/vnext-product`. Builds exit **0**: `tes3mp_native_loadout_tests` and affected
production `tes3mp_server`; logs `native-service-build.log` and
`native-service-server-build.log` in `build/logs`.

Individual filters, all exit **0**; logs in `build/logs`:

| Filter | Evidence | Log |
|---|---|---|
| inventory-service | caller/actor/shape guards; 40 frames, two synthetic client state machines; coherent restart, swapped-player rejection and both actors' continuation | native-service-test.log |
| inventory-service-durability | flush retry, uncertain closure across sinks, owned output preservation and fresh recovery | native-service-durability.log |
| inventory-equipment-container-prepared | detached preparation, retry, consumed/stale rejection, continuation | native-prepared-test.log |
| inventory-equipment-container-state | PCSkipEquip, constant effects, abilities, initialized spells, stats/locals and truncated-image rejection | native-service-state.log |
| inventory-equipment-container-allocations | 1,072 atomic failures; zero retained/post-acceptance allocations | native-service-allocations.log |
| inventory-equipment-container-recovery | eight durability/recovery cases | native-service-recovery.log |
| inventory-equipment-connected | direct transfer/equip/recovery preserved | native-service-transfer.log |

Three compile failures (exit **2**) were fixed and rerun: void test return,
WorldModel's mutable store requirement, missing ReadersCache include. Documentation
budget and local links pass individually, exit **0** (`docs-budget.log`,
`docs-links.log`). No full suites or gates.

**Inherited real-loadout evidence:** Morrowind.esm, common_shirt_01, barrel_01,
two `player` instances and diagnostic starting inventories;
`container-real-plain-final.log`, exit **0**, predates this adapter. No new
real-loadout service run, placed-container access, TR or actual two-client evidence.

## Limits

Startup mappings/inventories remain trusted bounded inputs, not placed-world
bootstrap. Equipment is supported by the underlying runtime, not this put/take
adapter's wire intake. Broader scripts, other categories, combat and full world
saves remain outside scope. Inherited limits include bounded nodes/effects,
actor-scoped spell IDs, no native durable request deduplication, private save
directories, rejected postponed physics and broad headless link/provenance debt.
