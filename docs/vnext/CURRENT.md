# Current state and next action

## Handoff

- **Direction:** OpenMW-backed authoritative cooperative multiplayer; see
  [README.md](README.md) and [DECISIONS.md](DECISIONS.md).
- **Milestone:** M2 in [PLAN.md](PLAN.md). The protected MISC transfer pair now
  owns stock inventory lists and selection/script/registry relocation to their
  stable nodes. Preparation remains isolated, not an atomic transfer.
- **Next action:** prepare owned stock LocalScripts list storage and cursor
  relocation from the protected pair's stable MISC nodes, preserving unaffected
  registrations and shared/distinct services; bind it to the same pair and
  witnesses without live installation or effects.
- **Checkpoint:** `8850e745c298c6de629ef9a5a26bbdddf6aa56e3` preserves the working
  gameplay implementation before the engine-backed pivot.

## Implemented M2 slice

[ContainerStore::prepareTransfer](../../apps/openmw/mwworld/containerstore.cpp)
returns one move-only `PreparedContainerTransfer`. Its protected decisions bind
source removal, both inventories/selections, incoming/destination values, complete
script lists/cursors, registry results and deferred notification consumers to one
removal quantity. No stock mutation caller or production authority changed.

`getSourceStorage()` and `getDestinationStorage()` expose const stock
`CellRefList<ESM::Miscellaneous>::List` storage. Explicit detached value copies
are moved into list nodes, preserving source/existing-stack flags and the already
prepared new-stack activation semantics. Lists preserve raw stock order, signed
counts, partial/full removal and dormant nodes. Existing destinations are replaced
in place; new stacks append. Original/proposed value witnesses remain separate
from storage, including independently owned locals and animation buffers.

`getRelocation()` exposes raw membership and selected-node views, with empty
selection for stock end. Original inventory IDs remain separate from values;
new membership retains an unset ID, while `getDestinationIdentity()` and the
relocated registry carry the proposed ID. No node receives a RefNum, WorldModel,
scene, Lua/custom-state or live container link.

Relocated LocalScripts entries reference owned nodes while preserving script,
owner/container/cell associations and full membership/cursors. Shared services
keep one combined remove-then-append result; distinct services remain independent.
Unaffected registrations retain their immutable identity. Relocated registry
bindings reference the same owned nodes, including dormant source identities,
while preserving stock cell hints, revision and generated-counter results.
Unrelated registry/script entries remain compare-only keys, even after destruction.
The earlier inventory/script/registry views remain protected relocation witnesses.

Joint validation first checks live storage identity, current values, selections,
service/owner bindings, script membership/cursors, registry membership/bindings,
revision/counter and deferred consumers. It then checks owned list membership and
values and reconstructs relocation from current owned nodes. It never dereferences
relocation views or saved live inventory nodes/iterators. Replaced storage,
reordered/equal-valued nodes, corrupted associations and another pair's bindings
reject. This witnesses current state, not complete mutation history.

The pair's heap-owned state keeps lists, nodes and relocation stable across moves.
Moved-from access rejects. The final fallible consumer copy follows storage and
relocation preparation; failures and discard destroy the whole owned result.
Content records, stores, owners/cells, WorldModel and script services must outlive
the pair. Public views last while the pair owns its state.

## Fresh verification

Windows MSVC RelWithDebInfo, `build/vnext-product`, individually:

- `tes3mp_native_loadout_tests` focused builds: exit 0.
- `inventory-transfer-preparation`: exit 0.
- `inventory-two-owners`: exit 0.
- Documentation budget/links, patch-registry semantic fields, formatting and
  whitespace checks: exit 0.

Synthetic disposable-stock comparisons cover both directions, signed partial/full
removal, existing/new destinations, plain/scripted values, raw dormant nodes,
selection, independent script/registry cell hints and shared/distinct cursors.
Faults cover stale live/owned storage, equal-value replacement, node order,
corrupted relocation, cross-pair bindings, script/registry witnesses, OnPCAdd and
late consumer-copy failure, moves and discard. Snapshots preserve live inventories,
RefData flags/locals, script registrations/cursors, selection, WorldModel
mappings/revision/counter and notifications. Move assignment also proves disposal
of replaced owned stock nodes.

Logs: `build/logs/native-storage-*`. Initial diagnostic compile and rejection-label
failures were fixed; their individual checks passed on retry. No complete suites,
expensive gates or upstream baseline tests ran. No Environment, World, UI or Lua
runtime initialized.

## Remaining limits and inherited evidence

Stock LocalScripts/registry installation storage, live installation, durability
and effect execution remain unprepared. No atomic transfer or installation-failure
proof exists; allocator faults are not injected. Unresolved stores, equipment,
gold/other types, Lua/custom state, persistence and stable multiplayer instance
mapping remain outside this slice.

Inherited M1 real-Morrowind/enchantment evidence under `build/native-loadout/real`
and `build/native-loadout/parity` was not rerun; TR remains unverified. Independent
networking/standalone targets and the migration base are unchanged. Broad
openmw-lib rendering dependencies still need extraction before production
headless packaging. Whole-baseline provenance debt remains; only touched
patch-registry entries changed.
