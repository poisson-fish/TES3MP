# Current state and next action

## Handoff

- **Direction:** OpenMW-backed authoritative cooperative multiplayer; see
  [README.md](README.md) and [DECISIONS.md](DECISIONS.md).
- **Milestone:** M2 in [PLAN.md](PLAN.md). A non-test app-local runtime now owns
  bounded equipment commands, durable installation and fresh recovery.
  Production server/network integration and M2 remain incomplete.
- **Next action:** extend this runtime's equipment persistence to one coherent
  two-actor image, then restore both actors atomically into one fresh runtime
  before allowing either actor to continue. Reuse the bounded file adapter,
  field serializers, allocation observation and recovery checks.
- **Session scope:** complete 2–3 closely related bounded slices sequentially.
  Inspect owning code and git status; preserve one writer. Review and commit,
  summarize capability, limitations, verification and commit, and provide the
  next ready-to-paste 2–3-slice prompt. No additional planning documents.
- **Checkpoint:** 8850e745c298c6de629ef9a5a26bbdddf6aa56e3 preserves the migration
  base before the engine-backed pivot.

## Implemented behavior

[EquipmentRuntime](../../apps/tes3mp-server/native/equipment_runtime.hpp), built
in `tes3mp_native_equipment_runtime` outside components/tes3mp, owns two actor
references, stock inventories, NPC stat contexts, pending effects and failure
state. Trusted startup binds content, WorldModel, LocalScripts, actor/item bases,
runtime/content identity and optional script declaration services. Content and
services must outlive the noncopyable runtime and remain stable during calls.

Command validation, protected preparation, relocation, canonical installation
and restart moved out of `PlainEquipmentFixture`. Fixtures provide synthetic
content, seed state, inspect results and inject faults; they call the same
compiled runtime. Stock mechanics and field serializers remain shared. Shared
phase markers preserve test-only allocation tracking without linking allocator
overrides into the probe.

Owned commands match a separately trusted caller and current actor/item/revision.
Plain, constant Fortify Luck and scripted shirts preserve signed counts, stable
identities, initialized spells, passive ability ownership, unrelated stats and
isolated locals. Synthetic Luck remains **40 → 49 → 40 → 49** and
**65 + 7 ability = 72 → 81 → 72 → 81**. PCSkipEquip persists locals and publishes
an owned skipped result without an equipment-change event. No instructions run.

Durability precedes nonallocating installation and owned publication. Safe failure
allows retry; uncertainty blocks both actors, including with a new sink. Fresh
recovery replays no success/events. Equipment formats 1/5/6 and transfer format 4
are unchanged; the bounded file adapter now belongs to the non-test native target.

[Native probe composition](../../apps/tes3mp-server/native/equipment_probe.cpp)
accepts `--equipment SHIRT --equipment-actors NPC_A NPC_B --equipment-save-dir NEW_DIR`.
For each actor it commits equip, destroys the owner, restores into a fresh runtime,
then commits unequip, retaining per-actor files. OpenMW file fingerprints bind
ordered content bytes, encoding and startup roles; these local diagnostic
identities are not authenticated multiplayer pack manifests.

## Verification

Windows MSVC, `scripts/setup_msvc_env.ps1 -PreferLatest`, RelWithDebInfo,
`build/vnext-product`, 2026-09-16. Both requested targets build with exit **0**:
`tes3mp_native_loadout_tests` and `tes3mp_native_loadout_probe`. Logs below are in
`build/logs`; final builds: `native-runtime-reviewed-tests-build.log` and
`native-runtime-reviewed-probe-build.log`.

Individually run synthetic filters, all exit **0**:

| Filter suffix (inventory-) | Evidence | Log (native-runtime-) |
|---|---|---|
| equipment-runtime | 14 commits, 6 destroyed-owner recoveries | bootstrap.log |
| equipment-scripted | 6 commits, 2 restarts, equipped/unequipped skips | scripted.log |
| equipment-scripted-durability | 12 failures and fresh continuations | durability.log |
| equipment-scripted-allocations | 877 failures, 8 successes, zero leftovers | allocations.log |
| equipment-command-guards | 96 atomic rejections | command-guards.log |
| equipment-restart-guards | 94 binding/lifetime rejections | restart-guards.log |
| equipment-scripted-guards | 17 local/codec rejections | scripted-guards.log |
| equipment-enchanted | 6 preserved commits | enchanted.log |
| equipment-command | 8 plain commits | command.log |
| transfer-command | 48 cases and existing failure checks | transfer.log |

**Real-loadout evidence:** local Morrowind.esm, `common_shirt_01`, two distinct
`player` instances: both equip/save/fresh-restart/unequip paths exit **0**;
`native-runtime-real-plain-reviewed.log`. No real-loadout scripted operation,
Tamriel Rebuilt or live clients ran.

Initial build exit **2** (stale transfer FileFaults forward declaration) was fixed
and rerun successfully. Individual documentation budget and local-link checks
exit **0**: `docs-budget.log`, `docs-links.log`. Valid verification was reused;
no complete suites, expensive gates or upstream baseline tests ran.

## Limits

Each save/recovery covers one actor beside the other's startup state, not a
coherent pair/world save. Transfer remains its existing rehearsal implementation.
Production networking, executing script registrations, broader script execution,
other equipment categories and complete world saves remain outside this runtime.
Do not resume fixture expansion merely to support another scripted detail.

Inherited limits: 64-node preparation/65-node saves, bounded pending effects,
actor-scoped active-spell IDs, no durable request/notification deduplication,
serialized writers, private save directories, rejected postponed physics, broad
headless linking/provenance debt and calling-thread C++ allocation observation.
