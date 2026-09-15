# Current state and next action

## Handoff

- **Direction:** OpenMW-backed authoritative cooperative multiplayer; see
  [README.md](README.md) and [DECISIONS.md](DECISIONS.md).
- **Milestone:** M2 in [PLAN.md](PLAN.md). Fully resolved non-gold MISC pairs
  support allocation-tested preparation and reversible, effect-free installation
  rehearsal on disposable stock stores/services. Production installation remains
  unavailable; no live atomic transfer exists.
- **Next action:** introduce explicit-declaration overloads for
  `MWScript::Locals::write` and `RefData::write`, retaining stock wrappers, then
  serialize detached plain/initialized-MWScript MISC RefData from fully validated
  prepared pairs without `Environment`. Verify short/long/float names, types and
  values, flags, position and animation. Malformed declaration/value shapes and
  individual allocation failures must leave caller output and original fixtures
  unchanged. This is an owned serialization seam only; no file durability,
  production installation or effects.
- **Checkpoint:** `8850e745c298c6de629ef9a5a26bbdddf6aa56e3` preserves the working
  gameplay implementation before the engine-backed pivot.

## Implemented M2 slice

The new `inventory-transfer-preparation-allocations` filter in
[transfer tests](../../apps/tes3mp-server/native/transfer_rehearsal_tests.cpp)
measures `ContainerStore::prepareTransfer` itself through successful discard or
partial-preparation unwinding. A test-only source consumer forces allocating
`std::function` storage and an identifiable allocation inside its final copy.
The test checks that copy's entry/completion ordinal and subsequent validation;
no production instrumentation or installation API was added.

[Allocation instrumentation](../../apps/tes3mp-server/native/test_allocations.hpp)
remains linked only into `tes3mp_native_loadout_tests`. Thread-local hooks cover
C++ scalar/array, aligned and nothrow allocation. A fixed 8,192-slot address table
tracks observed blocks without allocating; overflow fails the check. Hook checks
cover all eight routes, zero size, alignment, throwing/nothrow semantics,
non-LIFO frees and deallocation of blocks created before observation.
Assertions, snapshots and fixture construction remain outside measured calls.

All 48 synthetic combinations cover plain/scripted MISC, existing/empty
destinations, full/partial removal, shared/distinct services and begin/middle/end
script cursors, with dormant nodes and selections. Every observed allocation is
failed individually. Each failure propagates `std::bad_alloc`, performs no further
allocation during cleanup, and releases every tracked preparation block.
Fresh preparation/rehearsal succeeds on the same fixture after every failure;
protected bindings and owned nodes survive rehearsal, then owned reference
lifetimes expire on discard. Repeated preparations retain allocation counts,
phase visits, peak storage and final consumer-copy order.

Exact snapshots check original inventory nodes, lifetimes, identities, values,
locals and selections; script nodes, registrations and cursors; registry nodes,
revision and generated counter; cache flags/values/storage/capacity, resolved
flags and listeners. Snapshots now also include animation values/capacity, local
vector capacities and additional CellRef fields. Notifications, script execution,
the supplied store and an independent WorldModel remain unchanged.

Full `validateTransfer` and complete registry/script resolution still precede
rehearsal storage access. Protected pair/context/collection bindings, separate
proposed identities, ownership/storage/lifetime witnesses, read-only views and
owned-node/iterator guards remain intact. Unresolved keys are never followed and
no additional objects are resolved. Shared services exchange once; distinct
services exchange source then destination. Original inventory/script/registry
nodes and metadata are retained and restored; rollback clears temporary
identities and WorldModel links from detached nodes. Disposable ownership remains
test-only, with no commit/release or effect API.

## Fresh verification

Windows MSVC 14.51 (`scripts/setup_msvc_env.ps1 -PreferLatest`), RelWithDebInfo,
`build/vnext-product`, individually, all exit **0**:

- Focused `tes3mp_native_loadout_tests` build.
- `inventory-transfer-preparation-allocations`: **13,970** allocations failed
  individually, including **48** final consumer-copy-body allocations; peak
  outstanding **237**, remaining after every cleanup **0**.
- `inventory-transfer-preparation`.
- `inventory-transfer-rehearsal`.
- `inventory-transfer-rehearsal-allocations`: **6,998** individual failures;
  validation **3,454**, setup **90**, revalidation **3,454**. Exchange and rollback
  each observe **zero allocations**.
- Documentation budget/links, patch-registry semantic fields, formatting and
  whitespace.

No check failed. Logs: `build/logs/native-preparation-allocations-` followed by
`build.log`, `test.log`, `preparation.log`, `rehearsal.log` or
`rehearsal-allocations.log`; documentation/review logs use the same prefix.
No complete suites, expensive gates or upstream baseline tests ran.

## Remaining limits and inherited evidence

Allocation coverage excludes direct C allocation, other threads and external
libraries' private allocators. Borrowed content/readers/script-manager/cell
services must outlive use. Checks assume serialized engine access and current
state/lifetime, not complete mutation history or live-world concurrency safety.

Production installation, durability and notification execution remain unavailable.
Unresolved stores, equipment, gold/other types, Lua/custom-state transfer,
persistence and stable multiplayer mapping remain outside this slice.
M1 real-Morrowind/enchantment evidence under `build/native-loadout/real` and
`build/native-loadout/parity` was not rerun; TR remains unverified. Independent
networking/standalone targets and the migration base are unchanged. Broad
openmw-lib rendering dependencies still need extraction for production headless
packaging. Whole-baseline provenance debt remains; only relevant patch-registry
test entries changed.
