# Current state and next action

## Handoff

- **Direction:** OpenMW-backed authoritative cooperative multiplayer; see
  [README.md](README.md) and [DECISIONS.md](DECISIONS.md).
- **Milestone:** M2 in [PLAN.md](PLAN.md). One non-test runtime now supports
  plain-shirt transfer, recipient equip, both actors' continuation and coherent
  fresh recovery. Production server/network integration and M2 remain incomplete.
- **Next action:** extend this same owner with one shared plain-shirt container:
  actor drop into it, the other actor take/equip, then recover the affected
  inventories together and continue. Start by inspecting the stock ContainerStore
  versus InventoryStore preparation boundary; reuse existing mechanics and identity.
- **Session scope:** complete 2–3 related bounded slices sequentially. Inspect code
  and git status, preserve one writer, verify narrowly, review and commit. Replace
  this handoff and provide the next ready-to-paste prompt; no planning documents.
- **Checkpoint:** 8850e745c298c6de629ef9a5a26bbdddf6aa56e3 preserves the migration
  base before the engine-backed pivot.

## Implemented behavior

[EquipmentRuntime](../../apps/tes3mp-server/native/equipment_runtime.hpp) owns both
stock actor inventories, identities, stat contexts and effects. Connected commands,
installation and recovery are compiled into `tes3mp_native_equipment_runtime`,
outside components/tes3mp. Trusted content, exclusive WorldModel/LocalScripts and
optional declaration services outlive the runtime; serialized access remains required.

The [connected implementation](../../apps/tes3mp-server/native/connected_runtime.cpp)
uses protected equipment capture for two candidate inventories. Stock removal
counts, add/stack selection, equipped-stack exclusion and addition normalization
supply transfer mechanics. Equipment, transfer and recovery share one canonical
inventory installer. Neither a disposable rehearsal nor another authoritative
inventory owner participates in the connected sequence.

Owned transfer intent checks, results and notification delivery moved into the
non-test target; retained MISC migration tests use those functions. The MISC-only
ContainerStore preparation/rehearsal and transfer-v4 recovery remain separate
legacy tests, not the connected owner. Scripted/enchanted transfer remains unsupported.

Trusted callers must match current actor roles, item ownership and revision.
Partial/full transfers preserve signed counts and stable stack identities, including
dormant source nodes. An empty donor can receive and equip after recovery.
Plain transfers alongside equipped scripted shirts preserve isolated locals,
PCSkipEquip, constant effects, passive ability ownership, initialized spells and
unrelated stats. No script instructions or executing registrations were added.

Every connected transfer/equipment commit writes **one pair file** before either
candidate installs or owned success publishes. Pair framing embeds the existing
stock equipment codecs (1/5/6), common content/runtime bindings, generation counter
and registry revision. Bounds, duplicate identities and actor relationships are
validated. Fresh recovery stages both inventories, registry and stats before
nonthrowing installation; it replays no effects. Connected owners reject per-actor
sinks. Safe failures permit retry; uncertainty blocks both actors even with a new
sink. LocalScripts lifetime identity is bound at startup, avoiding lazy mutation
on a rejected restart. Legacy single-actor diagnostic APIs remain isolated by mode.

## Verification

Windows MSVC, `scripts/setup_msvc_env.ps1 -PreferLatest`, RelWithDebInfo,
`build/vnext-product`, 2026-09-16. Requested targets both build with exit **0**:
`tes3mp_native_loadout_tests`, `tes3mp_native_loadout_probe`; logs
`connected-reviewed-tests-build.log`, `connected-reviewed-probe-build.log` in
`build/logs`.

Individually run synthetic filters, all exit **0**. Prefix below is
`inventory-equipment-`; log names are in `build/logs`:

| Suffix | Evidence | Log |
|---|---|---|
| transfer-preparation | stock signed stacking, untouched live pair | connected-preparation.log |
| connected | transfer/equip/both continue/recover/return, 6 guards | connected-runtime-reviewed.log |
| connected-recovery | 8 durable/fault cases; empty donor continuation | connected-recovery-reviewed.log |
| connected-state | scripted/ability/stat/local preservation, truncated pair rejection | connected-state.log |
| connected-allocations | 536 atomic failures, zero retained/post-acceptance allocations | connected-allocations.log |
| scripted | 6 commits, 2 restarts, equipped/unequipped skips | connected-scripted.log |
| scripted-durability | 12 failures and fresh continuations | connected-scripted-durability.log |
| scripted-allocations | 874 failures, 8 successes, zero retained allocations | connected-scripted-allocations.log |
| enchanted | preserved Luck 40/49 and ability 72/81 | connected-enchanted.log |
| command-guards / restart-guards | 96 / 94 atomic rejections | connected-command-guards.log / connected-restart-guards.log |

`inventory-transfer-command`: exit **0**, 48 existing MISC cases plus failure,
allocation and delivery checks; `connected-legacy-transfer.log`.

**Real-loadout evidence:** local Morrowind.esm, common_shirt_01, two distinct
`player` instances with diagnostic starting inventories: transfer 2, recipient
and donor equip, destroyed-owner pair recovery, both unequip, return transfer 1;
exit **0**, `connected-real-plain-reviewed.log`. One `session.equipment` file.
No real-loadout scripted operation, Tamriel Rebuilt or live clients ran.

Build failures (exit 2: const slot lookup, test header imports) and allocation
failure (exit 1: lazy script lifetime) were fixed and rerun successfully.
Individual documentation budget/local-link checks exit **0**: `docs-budget.log`,
`docs-links.log`. Valid verification was reused; no complete suites, expensive
gates or upstream baseline tests ran.

## Limits

Connected transfer accepts unequipped plain shirts only. Other equipment categories,
shared containers/take/drop, executing script registrations, production networking,
broader scripts and complete world saves remain unfinished. Equipment still supports
its existing bounded constant/scripted shirts. Inherited limits: 64-node preparation,
65-node actor saves, bounded pending effects, actor-scoped active-spell IDs, no durable
request/notification deduplication, private save directories, rejected postponed
physics, broad headless linking/provenance debt and calling-thread allocation evidence.
