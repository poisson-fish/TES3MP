# Current state and next action

## Handoff

- **Direction:** OpenMW-backed authoritative cooperative multiplayer; see
  [README.md](README.md) and [DECISIONS.md](DECISIONS.md).
- **Milestone:** M2 in [PLAN.md](PLAN.md) now prepares detached item values and
  destination MWScript/notification intents between two resolved base-store
  owners. This is not atomic cross-container transfer. Actor, equipment,
  persistence, and production cutover criteria remain open.
- **Next action:** prepare destination stacking for detached plain non-gold MISC
  using stock `stacks`/`addItems` semantics; verify destination overflow and
  changed-state rejection without live installation or success emission.
- **Checkpoint:** `8850e745c298c6de629ef9a5a26bbdddf6aa56e3` preserves the working
  gameplay implementation before the engine-backed pivot.

## Implemented M2 slice

[ContainerStore::prepareTransferItem](../../apps/openmw/mwworld/containerstore.cpp)
validates distinct registered owners, resolved base stores, source membership and
registration, positive bounded counts, and destination overflow. It accepts
non-gold MISC with plain or matching initialized MWScript state, returning a
uniquely owned fresh reference without registry identity, WorldModel, cell or store
links. [RefData::copyForContainerTransfer](../../apps/openmw/mwworld/refdata.cpp)
copies owned locals, position, animation, flags and runtime values, omits scene
nodes, and rejects Lua/custom state. Its immutable ESM base record remains borrowed.

`ContainerStore::prepareTransferAdd` consumes that temporary for a resolved
destination with explicit owner/player and script-service context. Stock add and
deferred preparation share item normalization, script-cell selection and OnPCAdd
ordering. Destination CellRef position/ownership fields receive stock resets;
soul, condition/charge, other locals and RefData state remain item values.
OnPCAdd is assigned only on the owned copy for the explicit player destination.
Nonplayer destinations retain the copied value. No script instructions execute.

[LocalScripts::prepareAdd](../../apps/openmw/mwworld/localscripts.cpp) shares
RefData local initialization with stock registration and returns a script/cell
intent with no item pointer or live-list link. Existing locals are not reset.
Player script intents have a null cell; nonplayer intents carry the owner's cell.
The prepared result owns its item, optional registration intent, destination/count
and listener-notification intent, and a copied inventory-notification consumer.
It never invokes that consumer. Owner, cell, content and callback dependencies
must outlive the operation; this is not a durable or commit-ready transfer object.

Preparation never stacks, changes either live inventory, allocates a live identity,
registers a temporary script, advances live script iteration or emits success.
Its exceptions propagate and destroy temporary state/intents. Stock callers retain
immediate registration before OnPCAdd, registration error logging/catching,
duplicate replacement, iterator repair, and subsequent notification ordering.
Stock RefData/LiveCellRef/ContainerStore copy behavior remains unchanged.

The extended `inventory-transfer-preparation`
[filter](../../apps/tes3mp-server/native/loadout_tests.cpp) exercises plain and
scripted MISC in both directions, with each destination selected as player,
another player's container, or nonplayer with no selected player. Two real cell
objects distinguish owner-cell registration from player lifetime. Snapshots cover
inventory values/counts, owned buffers, selection/weight, owner bindings, registry
mappings/revision/ID counter, script membership/cursor and notifications.

Injected failures occur during OnPCAdd declaration lookup and notification-intent
copying, after temporary state exists. Item destructor markers, an OSG observer,
and notification-intent construction/destruction counters verify cleanup during
failure and ordinary discard. Existing detached-value/rejection checks remain;
stock registration exception, missing-script, duplicate and cell-removal cases
also verify preserved locals and iteration. No Environment, World, WindowManager,
Lua runtime or actor custom data is initialized. These are synthetic base stores
bound to NPC identities, not two live actor inventories.

## Fresh verification

Windows MSVC RelWithDebInfo, `build/vnext-product`, individually:

- `tes3mp_native_loadout_tests` build: exit 0, including affected stock callers.
- `inventory-transfer-preparation`, `inventory-scripted`, `inventory-two-owners`:
  each exit 0.
- Documentation budget and local-link guards: each exit 0.

Initial build exit 2 reported an incomplete `MWWorld::Class` in the new test;
adding its direct include and removing a shadowed name fixed the same build
before testing continued. Logs are under `build/logs`: `native-deferred-build.log`,
`native-deferred-build-final.log`, `native-deferred-transfer.log`,
`native-deferred-scripted.log`, `native-deferred-two-owners.log`,
`native-deferred-docs-budget.log`, and `native-deferred-docs-links.log`.
No complete suites, expensive gates or upstream baseline tests ran.

## Remaining limits and inherited evidence

Destination stacking, live transfer/effect installation, unresolved stores,
equipment, gold/other item types, Lua/custom state, durability/save-restore and
stable multiplayer instance mapping remain outside this slice. Copy-time
allocator faults and live installation failures are not covered.

Inherited M1 real-Morrowind enumeration/initialization and enchantment-charge
parity remain under `build/native-loadout/real` and `build/native-loadout/parity`;
they were not rerun. TR remains unverified. The opt-in
[native targets](../../apps/tes3mp-server/native/CMakeLists.txt) remain outside
independent networking/standalone targets. Migration-world callers and
client-authoritative positions remain unchanged. Broad openmw-lib rendering
dependencies still need extraction before production headless packaging.
Inherited whole-baseline provenance debt remains; only touched entries changed.
