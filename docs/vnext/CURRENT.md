# Current state and next action

## Handoff

- **Direction:** OpenMW-backed authoritative cooperative multiplayer; see
  [README.md](README.md) and [DECISIONS.md](DECISIONS.md).
- **Milestone:** M2 in [PLAN.md](PLAN.md). Fully resolved non-gold MISC pairs
  support allocation-tested preparation, reversible effect-free rehearsal, and
  owned RefData serialization without Environment. Production installation
  remains unavailable; no live atomic transfer exists.
- **Next action:** make TES3 `CellRef::writeState` failure-atomic, then extend the
  test-only complete-pair serialization composition to stage detached non-gold
  MISC CellRef plus RefData into separate source/destination `ESM::ObjectState`
  collections. Keep proposed identities as separate owned metadata; never assign
  them to detached nodes. Verify counts, soul, charge/remainder, enchantment
  charge, ownership and remaining CellRef fields alongside locals, flags,
  position and animation. Inject every observed allocation failure individually;
  preserve caller output and original fixtures and prove fresh serialization
  after each failure. No installation, file durability or effects.
- **Checkpoint:** `8850e745c298c6de629ef9a5a26bbdddf6aa56e3` preserves the working
  gameplay implementation before the engine-backed pivot.

## Implemented M2 slice

[MWScript locals](../../apps/openmw/mwscript/locals.cpp) and
[RefData](../../apps/openmw/mwworld/refdata.cpp) now have explicit-declaration
`write` overloads. Stock wrappers share their serialization behavior. Initialized
short/long/float declaration counts must match values exactly; empty names and
same-type/cross-type duplicates reject before staging. Locals retain stock named
append order and integer/float variant types. RefData stages locals and animation
before publishing flags, enabled state, position and animation through nonthrowing
operations. Unrelated caller ObjectState fields remain intact. Uninitialized
locals write nothing; initialized scripts with no locals still set `mHasLocals`.

The new `inventory-transfer-serialization` filter in
[transfer tests](../../apps/tes3mp-server/native/transfer_rehearsal_tests.cpp)
serializes only const detached RefData after full `validateTransfer` and complete
registry/script resolution. Its composition and
[allocation instrumentation](../../apps/tes3mp-server/native/test_allocations.hpp)
remain test-only. No production installation or persistence API was added.

All 48 synthetic combinations cover plain/scripted MISC, existing/empty
destinations, full/partial removal, shared/distinct services and begin/middle/end
script cursors, including dormant nodes and selections. Checks exercise two
locals of each type, allocating names, signed values, activation flags, enabled
state, position and multi-entry animation with a wide loop count. Direct locals
and RefData calls also preserve pre-existing caller data. Nineteen malformed
shapes cause 38 direct rejections; malformed pair declarations, 48 incomplete
pairs and 48 corrupted completeness reports preserve caller output.

Every observed serialization allocation is failed individually. Each failure
propagates `std::bad_alloc`, performs no further allocation during cleanup and
releases every tracked block. Fresh serialization succeeds after every failure.
Exact snapshots preserve output values/storage, detached nodes/lifetimes,
original inventories/identities/locals/selections, script nodes/cursors, registry
metadata, cache storage and listeners. Notifications and script execution remain
unchanged, as do the supplied store and independent WorldModel. Rehearsal permits
fresh serialization; discarding the pair expires owned reference lifetimes.

Complete-resolution validation, protected pair/context/collection bindings,
separate proposed identities, ownership/storage/lifetime witnesses, read-only
views and owned-node/iterator guards remain intact. Unresolved keys are never
followed and no additional objects are resolved. Shared services coalesce;
distinct services retain source-then-destination order.

## Fresh verification

Windows MSVC 14.51 (`scripts/setup_msvc_env.ps1 -PreferLatest`), RelWithDebInfo,
`build/vnext-product`, individually, all exit **0**:

- Focused `tes3mp_native_loadout_tests` build and warning-cleanup rebuild.
- `inventory-transfer-serialization`: **5,217** individual allocation failures,
  **48** cases, remaining tracked allocations after cleanup **0**.
- `inventory-transfer-preparation`.
- `inventory-transfer-rehearsal`.
- `inventory-transfer-preparation-allocations`: **13,970** individual failures,
  including **48** final consumer-copy allocations; peak outstanding **237**.
- `inventory-transfer-rehearsal-allocations`: **6,998** individual failures;
  validation **3,454**, setup **90**, revalidation **3,454**; exchange/rollback **0**.
- Documentation budget/links, patch-registry semantic fields, formatting and
  whitespace.

No check failed. Logs use `build/logs/native-serialization-` with `build.log`,
`rebuild.log`, `test.log`, `preparation.log`, `rehearsal.log`,
`preparation-allocations.log`, `rehearsal-allocations.log` and named review logs.
No complete suites, expensive gates or upstream baseline tests ran.

## Remaining limits and inherited evidence

This is an owned RefData serialization seam, not coherent inventory persistence.
CellRef serialization, durable files, production installation and notification
execution remain unavailable in this composition. Unresolved stores, equipment,
gold/other types, Lua/custom-state transfer, persistence and stable multiplayer
mapping remain outside scope.

Allocation coverage excludes direct C allocation, other threads and external
libraries' private allocators. Borrowed services must outlive use. Validation
assumes serialized engine access and current state/lifetime, not complete mutation
history or live-world concurrency safety.

M1 real-Morrowind/enchantment evidence was not rerun; TR remains unverified.
Independent networking/standalone targets and the migration base are unchanged.
Broad openmw-lib dependencies still need extraction for production headless
packaging. Whole-baseline provenance debt remains; only relevant patch-registry
entries changed.
