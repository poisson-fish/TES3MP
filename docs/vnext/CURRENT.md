# Current state and next action

## Handoff

- **Direction:** OpenMW-backed authoritative cooperative multiplayer; see
  [README.md](README.md) and [DECISIONS.md](DECISIONS.md).
- **Milestone:** M2 in [PLAN.md](PLAN.md). One connected runtime now supports
  plain-shirt drop into a shared container, the other actor's take/equip, coherent
  fresh recovery of all three owners, and both actors' continuation. M2 and
  production server/network integration remain incomplete.
- **Next action:** allow a currently equipped plain shirt to drop into this
  shared container in one atomic command, using stock unequip/removal preparation;
  have B take/equip it, recover all owners together and continue both actors.
- **Session scope:** complete 2–3 related bounded slices sequentially. Inspect
  code/status, preserve one writer, verify narrowly, review and commit. Replace
  this handoff and provide the next ready-to-paste prompt; no planning documents.
- **Checkpoint:** 8850e745c298c6de629ef9a5a26bbdddf6aa56e3 preserves the migration
  base before the engine-backed pivot. This slice continues dc30d976d7.

## Implemented behavior

[EquipmentRuntime](../../apps/tes3mp-server/native/equipment_runtime.hpp) owns two
stock InventoryStores and an optional shared base ContainerStore, their stable
identities, actor stats and owned effects. Trusted content, exclusive WorldModel,
LocalScripts and optional declaration services outlive the serialized runtime.
Networking remains independent; the runtime target is outside components/tes3mp.

[Protected clothing preparation](../../apps/openmw/mwworld/plainequipment.cpp)
shares detached node capture, stock add/stack selection, signed removal counts,
addition normalization and field serialization across both store types. Slots,
selection and NPC stats remain InventoryStore concerns. The
[connected implementation](../../apps/tes3mp-server/native/connected_runtime.cpp)
uses the same installation owner for transfer, drop/take, equipment and recovery.
No disposable rehearsal, fixture command implementation or second writer is used.

Drop/take accepts unequipped plain shirts. The trusted initiator must match the
participating actor, current source ownership and registry revision. Partial and
full moves preserve signed counts, dormant source identities and equipped-stack
exclusion. An emptied donor can receive and equip again after recovery. Adjacent
scripted equipment preserves PCSkipEquip, constant effects, passive abilities,
initialized spells, unrelated stats and isolated locals. No executing registration
or script instruction support was added.

Every connected command persists one complete session file before installation
or success publication. Shared-session framing includes both actors and the
required container, common content/runtime bindings, generation counter and
registry revision. It reuses equipment field codecs 1/5/6 and existing file
durability. Existing pair framing remains unchanged. Bounds, owner relationships,
duplicate identities and forbidden container equipment/stats are validated.
Fresh recovery stages all owners before nonthrowing installation without replaying
effects. Per-actor sinks are rejected; safe failures allow retry and uncertainty
blocks the entire runtime even with another sink.

The [native caller](../../apps/tes3mp-server/native/equipment_probe.cpp) accepts
`--equipment-container CONT` with the existing equipment options. It creates an
empty diagnostic container from that content base, drops two shirts from A,
lets B take one, equips both, destroys the runtime, recovers the session, unequips
both, lets A take the remaining shirt and equips both again.

## Verification

Windows MSVC, `scripts/setup_msvc_env.ps1 -PreferLatest`, RelWithDebInfo,
`build/vnext-product`, 2026-09-16. Both requested targets build with exit **0**:
`tes3mp_native_loadout_tests`, `tes3mp_native_loadout_probe`; logs
`container-tests-final-build.log`, `container-probe-final-build.log` in `build/logs`.

Individual synthetic filters, all exit **0**. Prefix is `inventory-equipment-`;
all logs are in `build/logs`:

| Suffix | Evidence | Log |
|---|---|---|
| container | complete shared sequence, total 8; 9 command/2 recovery guards | container-runtime-final.log |
| container-recovery | 8 durability cases, retries/closure, empty donor continuation | container-recovery.log |
| container-state | scripted/ability/stat/local preservation; truncated session rejection | container-state.log |
| container-allocations | 1,014 atomic failures; zero retained/post-acceptance allocations | container-allocations.log |
| connected | direct transfer/equip/recovery continuation preserved | container-direct-transfer.log |
| transfer-preparation | stock signed stacking; live actors unchanged | container-preparation.log |
| command-guards / restart-guards | 96 / 94 atomic rejections | container-command-guards.log / container-restart-guards.log |
| scripted-allocations | 878 failures, 8 successes, zero retained allocations | container-scripted-allocations.log |

**Real-loadout evidence:** local Morrowind.esm, common_shirt_01, barrel_01 and two
distinct `player` instances, with diagnostic starting inventories. The complete
shared sequence above exits **0**; `container-real-plain-final.log`. One
`session.equipment` file. No placed-world container, real scripted transfer,
Tamriel Rebuilt or live clients were tested.

Earlier failures were fixed and rerun: build exit **2** from omitted shell MSVC
initialization; filter exit **1** from the temporary container envelope binding
and an isolation assertion that incorrectly required the shared counter unchanged.
Individual documentation budget/local-link checks exit **0**: `docs-budget.log`,
`docs-links.log`. Valid verification was reused; no full suites or expensive gates.

## Limits

Equipped-shirt drop, scripted/enchanted transfer, other item categories,
placed-container access/capacity/crime services, executing registrations, broader
scripts, production networking and complete world saves remain unfinished.
Container base inventory lists are not loaded. Inherited limits include 64-node
preparation/65-node saves, bounded pending effects, actor-scoped active-spell IDs,
no durable request deduplication, private save directories, rejected postponed
physics, broad headless linking/provenance debt and calling-thread allocation evidence.
