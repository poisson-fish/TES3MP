# Current state and next action

## Handoff

- **Direction:** OpenMW-backed authoritative cooperative multiplayer; see
  [README.md](README.md) and [DECISIONS.md](DECISIONS.md).
- **Milestone:** M2 in [PLAN.md](PLAN.md). Fully resolved non-gold MISC pairs
  support allocation-tested preparation, reversible effect-free rehearsal,
  owned CellRef/RefData serialization and strict named-locals restoration without
  Environment. Production installation remains unavailable; no live atomic
  transfer exists.
- **Next action:** add failure-atomic explicit-declaration restoration of the
  serialized RefData fields into an already configured detached `MWWorld::RefData`:
  locals, activation flags, enabled state, position and animation. Consume the
  owned seam's modern `ESM::ObjectState` output using the strict Locals reader;
  validate presence/shape, supported flags and numeric values before publication.
  Preserve caller input and target values/storage on malformed input and every
  individually injected allocation failure; verify cleanup and exact fresh
  read/write recovery. Keep stock save-load constructor/read behavior unchanged.
  Keep CellRef and complete ObjectState restoration deferred; no installation.
- **Checkpoint:** `8850e745c298c6de629ef9a5a26bbdddf6aa56e3` preserves the working
  gameplay implementation before the engine-backed pivot.

## Implemented M2 slice

[MWScript::Locals::read](../../apps/openmw/mwscript/locals.cpp) now has an
explicit-declaration overload requiring configured locals. It accepts exactly
one modern named value per declaration in any order. Before staging, it checks
configured vector shapes, count/index bounds, unique nonempty declaration names
without embedded NUL, exact value names, types and numeric ranges. Short/long
values require `VT_Int`; shorts must fit int16, and longs already fit int32.
Floats require finite `VT_Float`. There is no coercion or legacy fallback in this
new overload. Three owned value vectors publish through nonthrowing swaps only
after complete validation and staging. Existing stock tolerant/legacy reads and
both writers are unchanged.

The `inventory-transfer-locals-restore` filter in
[transfer tests](../../apps/tes3mp-server/native/transfer_rehearsal_tests.cpp)
consumes owned source/destination ObjectState locals after prepared-pair
destruction. All **48** synthetic plain/scripted, existing/empty-destination,
full/partial-removal, shared/distinct-service and cursor-position combinations
retain original fixtures and serialized pair storage. **60** scripted locals
sets restore into separately configured targets. Tests cover two short/long/float
values, arbitrary named order, integer endpoints, float extrema, subnormals,
signed zero, empty configured scripts and individual-vector shapes.

**109** malformed-input rejections cover unconfigured targets, declaration and
target shapes, empty/duplicate/cross-type/NUL names, missing/extra/unknown/unnamed
values, unsupported variant types, short overflow and nonfinite floats.
Witnesses compare caller and target values, identities, vector/string storage and
float bits. Each rejection releases tracked allocations and permits recovery.
Every observed successful read and subsequent write allocation is failed
individually: **837** failures propagate `std::bad_alloc`, preserve callers,
allocate nothing further during cleanup and leave **0** tracked blocks. Successful
output destruction is tracked too. Recovery retries the preserved target and a
fresh instance; round trips verify names, types and values. The shared allocation
helper also now retries preserved serialization callers after failure.

Complete-resolution validation, protected pair/context/collection bindings,
separate proposed identities, ownership/storage/lifetime witnesses, owned-node
and iterator guards, read-only views, shared-service coalescing and distinct
source-then-destination order remain intact. No unresolved key is followed and
no extra object is resolved. Original inventories, registry/script metadata,
cursors, selections, caches, listeners, supplied store and independent WorldModel
remain unchanged. No notifications or script instructions execute.

## Fresh verification

Windows MSVC 14.51 (`scripts/setup_msvc_env.ps1 -PreferLatest`), RelWithDebInfo,
`build/vnext-product`, individually, exit **0**:

- Focused `tes3mp_native_loadout_tests` build and formatting rebuild.
- `inventory-transfer-locals-restore`, before/after formatting: **837** failures.
- `inventory-transfer-object-state`: **6,219** failures, **48** cases.
- `inventory-transfer-serialization`: **5,273** failures, **48** cases.
- `inventory-transfer-preparation` and `inventory-transfer-rehearsal`.
- `inventory-transfer-preparation-allocations`: **13,970** failures, including
  **48** final consumer copies; peak outstanding **237**.
- `inventory-transfer-rehearsal-allocations`: **6,998** failures; validation
  **3,454**, setup **90**, revalidation **3,454**, exchange/rollback **0**.
- Documentation budgets/links, patch-registry semantic fields, formatting,
  unchanged stock reader/writers and whitespace review.

Logs use the `build/logs/native-locals-restore-` prefix. No check failed.
No complete suites, expensive gates or upstream baseline tests ran.

## Remaining limits and inherited evidence

Only locals restoration is implemented. Complete RefData/ObjectState restoration,
coherent inventory persistence, durable files, production installation,
notifications and script execution remain unavailable. Unresolved stores,
equipment, gold/other types, Lua/custom-state transfer and stable multiplayer
mapping remain outside scope. Disposable ownership/instrumentation stay test-only.

Allocation coverage excludes direct C allocation, other threads and private
external allocators. Borrowed services must outlive use; validation assumes
serialized engine access and current state/lifetime, not complete mutation history
or live-world concurrency safety. M1 real-Morrowind/enchantment evidence was not
rerun; TR remains unverified. Independent networking and the migration base are
unchanged. Broad openmw-lib dependencies still need extraction for headless
packaging. Whole-baseline provenance debt remains; only the relevant registry
entry changed.
