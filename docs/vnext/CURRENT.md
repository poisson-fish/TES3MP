# Current state and next action

## Handoff

- **Direction:** OpenMW-backed authoritative cooperative multiplayer; see
  [README.md](README.md) and [DECISIONS.md](DECISIONS.md).
- **Milestone:** M2 in [PLAN.md](PLAN.md). The protected MISC transfer pair now
  owns stock inventory and LocalScripts list storage, with bound node/cursor
  relocation. Preparation remains isolated, not an atomic transfer.
- **Next action:** prepare owned stock PtrRegistry map storage from the protected
  pair's stable MISC nodes and relocated registry result, preserving unaffected
  mappings, revision and generated-identity counter; bind it to the same pair and
  witnesses without live installation or effects.
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
Inventory membership retains original identities or an unset new-node ID; the
proposed destination ID remains in registry results. Owned nodes receive no
RefNum, WorldModel, scene, Lua/custom state or live inventory reference.

`getSourceScriptStorage()` and `getDestinationScriptStorage()` now expose const
[LocalScripts::PreparedStorage](../../apps/openmw/mwworld/localscripts.hpp).
It owns the same `std::list<Entry>` type as the stock service, populated from the
pair's validated relocation. Entries preserve exact immutable registration
identities, script/cell/container associations and stock order. Shared services
return one combined remove-then-append storage; distinct services stay independent.
Only pair-owned stable inventory nodes receive item pointers, with registration
cell/container hints. Dormant associations remain present. Unaffected entries
retain immutable comparison keys/metadata and expose empty item views, including
registrations whose old live references have already been destroyed.

The prepared cursor identifies an owned script-list node, or null for stock end.
It preserves before/at/after-removal and append-at-end behavior without retaining
a live script iterator. Storage and cursor views remain stable across pair moves.
Read-only entry access returns `ConstPtr`; it cannot expose mutable item values.

Joint validation checks current live storage identity, values, selections,
owner/service bindings, script membership/cursors, registry mappings/revision/
counter and deferred consumers before inspecting prepared storage. It reconstructs
inventory relocation from current owned nodes, then verifies script storage node
addresses, exact registration identities, item/cell/container bindings and cursor.
It never follows saved script-list node/cursor keys or saved live inventory
references/iterators. Corrupted, replaced, reordered and foreign-pair storage
rejects. This witnesses current state, not complete mutation history.

Script storage preparation precedes the final fallible consumer copy. Failure,
discard and move assignment destroy script lists before their owned item nodes.
Moved-from access rejects. Content records, stores, owners/cells, WorldModel and
script services must outlive the pair; public views last while it owns its state.

## Fresh verification

Windows MSVC RelWithDebInfo, `build/vnext-product`, individually:

- `tes3mp_native_loadout_tests` focused builds: exit 0.
- `inventory-transfer-preparation`: exit 0, including final lifetime checks.
- `inventory-two-owners`: exit 0.
- Documentation budget/links, patch-registry semantic fields, formatting and
  whitespace checks: exit 0.

Synthetic disposable-stock comparisons cover both directions, signed partial/full
removal, existing/new destinations, plain/scripted values, dormant associations,
independent cell hints and shared/distinct services at every cursor position.
Owned list iteration tails match stock `getNext`. Faults cover stale storage and
bindings, replacement/destroyed script nodes, corrupt results/cursors, another
pair/quantity, expired unrelated references, OnPCAdd and late consumer-copy failure,
moves and discard. Snapshots preserve live inventories, RefData flags/locals,
script registrations/cursors, selection, WorldModel mappings/revision/counter and
notifications. Owned script-item values survive live inventory destruction.

Logs: `build/logs/native-script-storage-*`. No complete suites, expensive gates or
upstream baseline tests ran. No Environment, World, UI or Lua runtime initialized.

## Remaining limits and inherited evidence

Stock PtrRegistry map storage, resolution of unaffected live script pointers,
installed script iterators, live installation, durability and effect execution
remain unprepared. No atomic transfer or installation-failure proof exists;
allocator faults are not injected. Unresolved stores, equipment, gold/other types,
Lua/custom state, persistence and stable multiplayer instance mapping remain
outside this slice.

Inherited M1 real-Morrowind/enchantment evidence under `build/native-loadout/real`
and `build/native-loadout/parity` was not rerun; TR remains unverified. Independent
networking/standalone targets and the migration base are unchanged. Broad
openmw-lib rendering dependencies still need extraction before production
headless packaging. Whole-baseline provenance debt remains; only touched
patch-registry entries changed.
