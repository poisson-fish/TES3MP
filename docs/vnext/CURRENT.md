# Current state and next action

## Handoff

- **Direction:** OpenMW-backed authoritative cooperative multiplayer; see
  [README.md](README.md) and [DECISIONS.md](DECISIONS.md).
- **Milestone:** M2 in [PLAN.md](PLAN.md). Fully resolved non-gold MISC pairs now
  support preparation, reversible rehearsal and complete detached owned
  save/restore/save. Production installation remains unavailable; no live atomic
  transfer exists.
- **Next action:** implement a test-only persistence-gated commit in
  `DisposableTransferRehearsal` for this same supported path. Stage the complete
  owned save and all installation/retirement bookkeeping; finish current-witness
  validation before calling an explicit synchronous test sink. Sink failure must
  preserve the entire fixture. Acceptance permits only nonallocating, nonthrowing
  installation of both inventories and their script/registry/selection/cursor
  results, followed by safe old-node retirement. Prove the intended result,
  consumed-pair reuse rejection, allocation/failure boundaries and shared/distinct
  services. The sink is a test double: actual durable files, production callers,
  notifications and script execution remain deferred.
- **Checkpoint:** `8850e745c298c6de629ef9a5a26bbdddf6aa56e3` preserves the working
  gameplay implementation before the engine-backed pivot.

## Implemented M2 slice

[RefData](../../apps/openmw/mwworld/refdata.cpp) has explicit strict validation
and fresh owned restoration of modern ObjectState fields: named locals, all
activation flags, enabled state, position and animation. Unlike stock loading,
this path preserves suppression flags. [Locals](../../apps/openmw/mwscript/locals.cpp)
shares strict named-value validation and staged vector reads between configured
reads and fresh restoration from explicit script identity/declarations. Stock
save-load construction, tolerant/legacy locals reads and writers are unchanged.

The test-only composition in
[transfer tests](../../apps/tes3mp-server/native/transfer_rehearsal_tests.cpp)
validates both serialized inventories completely before allocating staged state.
It bounds membership, supplied bases, declarations, names, locals and animations;
checks types, numeric values, locals presence, unique proposed identities and
association shapes; and rejects unsupported state. Successful validation allocates
nothing. Actual CellRef/LiveCellRef construction and RefData restoration populate
fresh stock MISC lists, including dormant nodes and signed restocking counts.
A single nonthrowing ownership publication exposes both lists read-only.

Source/destination collections and proposed identities remain separate. No ID is
assigned to restored CellRefs and no restored node enters a WorldModel. Only
explicitly supplied MISC bases and script declarations are used; referenced
owner/soul/faction/key/trap fields are preserved without resolving their targets.
The current synthetic composition supplies one script/declaration binding.
Scene state, content-deletion state and postponed physics are not serialized.
Restored lists do not reconstruct live script services, registry or selections.

All **48** plain/scripted, existing/empty-destination, full/partial-removal,
shared/distinct-service and cursor combinations serialize the prepared result,
destroy its pair, restore into fresh storage, and save/restore again. Independent
copies of prepared engine values check restored values directly. Coverage includes
activation flags, enabled/disabled state, integer endpoints, finite float extrema,
signed zero, subnormals, reordered named locals and configured zero-variable
scripts. Destroyed prepared-reference witnesses remain expired after restoration.

**218** malformed inputs reject without changing caller values/storage, supplied
content/declarations, prior output or original fixtures. Every observed successful
restoration and reserialization allocation is failed individually: **2,588**
failures propagate `std::bad_alloc`, allocate nothing during cleanup and leave
**0** tracked blocks. Successful output destruction is tracked too. Each failure
allows retries on preserved outputs and fresh instances.

Complete-resolution checks, protected pair/context/collection bindings,
ownership/storage/lifetime witnesses, node/iterator guards, read-only views,
shared-service coalescing and distinct service order remain intact. Original
inventories, independent WorldModel, script metadata, cursors, selections,
caches and listeners remain unchanged; no notifications or instructions execute.

## Fresh verification

Windows MSVC 14.51 (`scripts/setup_msvc_env.ps1 -PreferLatest`), RelWithDebInfo,
`build/vnext-product`, individually, exit **0**:

- `tes3mp_native_loadout_tests` build and focused rebuilds.
- `inventory-transfer-restore`: **48** cases, **218** rejections, **2,588** failures.
- `inventory-transfer-locals-restore`: **837** allocation failures.
- `inventory-transfer-object-state`: **6,219** allocation failures.
- `inventory-transfer-serialization`: **5,273** allocation failures.
- `inventory-transfer-preparation` and `inventory-transfer-rehearsal`.
- `inventory-transfer-preparation-allocations`: **13,970** failures; peak **237**.
- `inventory-transfer-rehearsal-allocations`: **6,998** failures; exchange/rollback **0**.
- Documentation budgets/links, registry semantic fields and final formatting.

Logs use `build/logs/native-inventory-restore-`. Formatting initially exited **1**
on the final test-fixture edit; the formatter fixed it and that check reran **0**.
All builds and native checks passed. No complete suites, expensive gates or
upstream baseline tests ran.

## Remaining limits and inherited evidence

Durable persistence, production installation, notifications and script execution
remain unavailable. Unresolved stores, equipment, gold/other types, Lua/custom
state and stable multiplayer mapping remain outside scope. Disposable ownership
and allocation instrumentation stay test-only. Tracking excludes direct C
allocation, other threads and private external allocators. Borrowed content and
services must outlive use; witness validation assumes serialized engine access,
not live-world concurrency safety. M1 real-content/enchantment evidence was not
rerun; TR remains unverified. Independent networking and the migration base are
unchanged. Broad openmw-lib dependencies still require extraction for headless
packaging. Whole-baseline provenance debt remains; only relevant registry entries
changed.
