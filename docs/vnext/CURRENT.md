# Current state and next action

## Handoff

- **Direction:** OpenMW-backed authoritative cooperative multiplayer; see
  [README.md](README.md) and [DECISIONS.md](DECISIONS.md).
- **Milestone:** M2 in [PLAN.md](PLAN.md). The protected MISC transfer pair now
  owns stock inventory lists, LocalScripts lists and PtrRegistry map storage.
  Preparation remains isolated, not an atomic transfer.
- **Next action:** prepare stock selected-item and LocalScripts cursor iterator
  bindings over the protected pair's owned lists; bind them to the same pair and
  witnesses, validate current nodes before using saved iterators, and preserve
  stock end/dormant/shared-service behavior without live installation or effects.
- **Checkpoint:** `8850e745c298c6de629ef9a5a26bbdddf6aa56e3` preserves the working
  gameplay implementation before the engine-backed pivot.

## Implemented M2 slice

[ContainerStore::prepareTransfer](../../apps/openmw/mwworld/containerstore.cpp)
returns one move-only `PreparedContainerTransfer`. Protected decisions bind source
removal, both inventories/selections, detached incoming/destination values, full
script lists/cursors, registry results and deferred notification consumers to one
removal quantity. No stock mutation caller or production authority changed.

Const stock MISC lists preserve raw order, signed counts, partial/full removal,
dormant nodes and independently owned RefData. Existing destinations are replaced
in place; new stacks append. Original/proposed value witnesses remain separate.
Inventory membership retains original identities or an unset new-node ID; proposed
identities remain separate from detached values. Owned nodes receive no RefNum,
WorldModel, scene, Lua/custom state or live inventory reference.

Const [LocalScripts::PreparedStorage](../../apps/openmw/mwworld/localscripts.hpp)
owns stock list nodes with immutable registration identities, script/cell/container
associations, order and a compare-only owned-node cursor (null for end). Shared
services expose one combined remove-then-append list; distinct services remain
independent. Only pair-owned stable nodes receive item pointers. Unaffected script
entries retain immutable keys/metadata and expose empty items.

`getRegistryStorage()` exposes const
[PtrRegistry::PreparedStorage](../../apps/openmw/mwworld/ptrregistry.hpp).
It owns the same `unordered_map<RefNum, Ptr>` type as stock, with every relocated
key and the prepared revision/generated-identity counter. Pair-owned stable nodes
receive stock cell/container hints, including dormant identities. Unaffected
mappings retain exact compare-only bindings and occupy empty map slots; resolving
their live Ptrs remains separate work. Lookup exposes `ConstPtr`, never the mutable
Ptr map. It neither generates another identity nor changes detached item values.

Joint validation checks live storage identity, values, selections, owner/service
bindings, script membership/cursors, registry mappings/revision/counter and deferred
consumers. It reconstructs relocation from current owned nodes and checks script
storage identities/bindings/cursor before the registry map. Registry storage binds
the exact pair-owned relocated-result object and verifies its metadata plus every
stock map key and Ptr field. Protected identity membership decides which entries
receive owned pointers; address equality or owner hints alone cannot promote an
unaffected alias or a reused stale address. Saved keys/Ptrs/iterators are never
followed, including unrelated references destroyed before or after preparation.
These checks witness current state, not complete mutation history.

Registry allocation follows script storage and precedes the final fallible consumer
copy. Failure, discard and move assignment destroy pointer storage before owned
items. Pair moves preserve storage and item addresses; moved-from access rejects.
Content records, stores, owners/cells, WorldModel and script services must outlive
the pair; public views last while it owns its state.

## Fresh verification

Windows MSVC RelWithDebInfo, `build/vnext-product`, individually:

- `tes3mp_native_loadout_tests` focused builds: exit 0.
- `inventory-transfer-preparation`: exit 0.
- `inventory-two-owners`: exit 0.
- Documentation budget/links, patch-registry semantic fields, formatting and
  whitespace checks: exit 0.

Synthetic disposable-stock comparisons cover both directions, signed partial/full
removal, existing/new destinations, plain/scripted values, dormant associations,
independent cell hints, identity boundaries and shared/distinct services/cursors.
Registry storage comparisons check actual map lookups and detached values against
stock, including unchanged bindings/counters and shared script/inventory nodes.
Faults cover corrupt storage binding metadata/revision/counters, foreign pairs and
quantities, alias keys, stale live/owned storage, expired unrelated references,
OnPCAdd and late consumer-copy failure, moves and discard. Snapshots preserve live
inventories, RefData flags/locals, script registrations/cursors, selection, WorldModel
mappings/revision/counter and notifications. Owned registry/script item values
survive live inventory destruction.

Logs: `build/logs/native-registry-storage-*`. No complete suites, expensive gates or
upstream baseline tests ran. No Environment, World, UI or Lua runtime initialized.

## Remaining limits and inherited evidence

Stock selection/script iterator bindings, resolution of unaffected live pointers,
live installation, durability and effect execution remain unprepared. No atomic
transfer or installation-failure proof exists; allocator faults are not injected.
Unresolved stores, equipment, gold/other types, Lua/custom state, persistence and
stable multiplayer instance mapping remain outside this slice.

Inherited M1 real-Morrowind/enchantment evidence under `build/native-loadout/real`
and `build/native-loadout/parity` was not rerun; TR remains unverified. Independent
networking/standalone targets and the migration base are unchanged. Broad
openmw-lib rendering dependencies still need extraction before production
headless packaging. Whole-baseline provenance debt remains; only the touched
patch-registry entry changed.
