# Current state and next action

## Handoff

- **Direction:** OpenMW-backed authoritative cooperative multiplayer; see
  [README.md](README.md) and [DECISIONS.md](DECISIONS.md).
- **Milestone:** M2 in [PLAN.md](PLAN.md). Fully resolved non-gold MISC pairs
  support allocation-tested preparation, reversible effect-free rehearsal, and
  owned CellRef plus RefData serialization without Environment. Production
  installation remains unavailable; no live atomic transfer exists.
- **Next action:** add a failure-atomic explicit-declaration named-locals read
  overload for already configured `MWScript::Locals`, consuming this seam's
  modern named `ESM::Locals` output. Validate declaration/value names, types,
  counts and numeric ranges before publication; reject malformed input without
  changing locals or caller-owned serialized data. Preserve existing stock
  tolerant/legacy read semantics. Individually inject allocations, verify safe
  cleanup and fresh read/write round trips after every failure. Keep this bounded
  to locals restoration; complete ObjectState restoration remains deferred.
- **Checkpoint:** `8850e745c298c6de629ef9a5a26bbdddf6aa56e3` preserves the working
  gameplay implementation before the engine-backed pivot.

## Implemented M2 slice

TES3 [CellRef::writeState](../../apps/openmw/mwworld/cellref.cpp) copies allocating
fields into a temporary before nonthrowing move assignment to the caller's
`mRef`. Unrelated ObjectState fields and TES4 no-op behavior remain intact.
Direct checks cover every CellRef field, signed/zero/positive counts, unchanged
input/change tracking and unrelated caller values/storage.

The new `inventory-transfer-object-state` filter in
[transfer tests](../../apps/tes3mp-server/native/transfer_rehearsal_tests.cpp)
extends the test-only complete-pair composition to separate source/destination
`ESM::ObjectState` collections with separate owned proposed-identity vectors.
Full current `validateTransfer` and complete registry/script resolution precede
all owned-node reads. CellRef and RefData use their engine serializers with
explicit script declarations. Both collections publish only after all staging
succeeds. Proposed identities are never assigned to serialized or detached
CellRefs; outputs remain valid after pair destruction.

All 48 synthetic combinations cover plain/scripted MISC, existing/empty
destinations, full/partial removal, shared/distinct services and begin/middle/end
script cursors, including dormant nodes/selections. Checks cover counts, soul,
charge/remainder, enchantment charge, ownership, global/faction data, scale,
teleport/destination, locks, key/trap, reference-blocked state and original
position; two short/long/float names, types and values; flags, enabled state,
current position and multi-entry animation with a wide loop count.

Every observed successful serialization allocation is failed individually.
Failures preserve caller output values/storage and original fixtures, propagate
`std::bad_alloc`, allocate nothing further during cleanup and release every
tracked block. Fresh serialization succeeds after each failure. Each filter
also covers 19 malformed shapes/38 direct rejections, 13 malformed pair
declarations, 48 incomplete pairs and 48 corrupted completeness reports.
Malformed aggregate staging releases all tracked allocations.

Complete-resolution validation, protected pair/context/collection bindings,
separate identities, ownership/storage/lifetime witnesses, owned-node/iterator
guards and read-only views remain intact. No unresolved keys are followed or
additional objects resolved. Shared services coalesce; distinct services retain
source-then-destination order. Original inventories, registry metadata, script
nodes/cursors, selections, cache storage, listeners, supplied store and an
independent WorldModel remain unchanged. Notifications and script execution
remain unchanged; rehearsal still permits fresh serialization.

## Fresh verification

Windows MSVC 14.51 (`scripts/setup_msvc_env.ps1 -PreferLatest`), RelWithDebInfo,
`build/vnext-product`, individually, final exit **0**:

- Focused `tes3mp_native_loadout_tests` build and two incremental rebuilds.
- `inventory-transfer-object-state`: **6,219** individual allocation failures,
  **48** cases, remaining tracked allocations after cleanup **0**.
- `inventory-transfer-serialization`: **5,273** individual failures, **48** cases.
- `inventory-transfer-preparation` and `inventory-transfer-rehearsal`.
- `inventory-transfer-preparation-allocations`: **13,970** individual failures,
  including **48** final consumer-copy allocations; peak outstanding **237**.
- `inventory-transfer-rehearsal-allocations`: **6,998** individual failures;
  validation **3,454**, setup **90**, revalidation **3,454**, exchange/rollback **0**.
- Documentation budget/links, patch-registry semantic fields, formatting and whitespace.

The first ObjectState run exited **1** because the new test incorrectly assumed
`setCharge()` marks CellRef changed. The fixture now uses `setEnchantmentCharge()`;
its rebuild and same-filter rerun passed before continuing. Formatting initially
exited **1** for include-line endings; normalization and the same check passed.
Logs are under
`build/logs/native-object-state-`, including `test.log`, `test-rerun.log`,
`fixture-fix-build.log` and individually named regression/review logs.
No complete suites, expensive gates or upstream baseline tests ran.

## Remaining limits and inherited evidence

This remains an owned serialization seam. Complete restoration, coherent
inventory persistence, durable files, production installation and notification
execution remain unavailable. Unresolved stores, equipment, gold/other types,
Lua/custom-state transfer and stable multiplayer mapping remain outside scope.
Disposable ownership and allocation instrumentation remain test-only.

Allocation coverage excludes direct C allocation, other threads and external
libraries' private allocators. Borrowed services must outlive use. Validation
assumes serialized engine access and current state/lifetime, not complete mutation
history or live-world concurrency safety.

M1 real-Morrowind/enchantment evidence was not rerun; TR remains unverified.
Independent networking/standalone targets and the migration base are unchanged.
Broad openmw-lib dependencies still need extraction for production headless
packaging. Whole-baseline provenance debt remains; only relevant patch-registry
entries changed.
