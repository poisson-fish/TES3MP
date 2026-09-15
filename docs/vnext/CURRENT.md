# Current state and next action

## Handoff

- **Direction:** OpenMW-backed authoritative cooperative multiplayer; see
  [README.md](README.md) and [DECISIONS.md](DECISIONS.md).
- **Milestone:** M2 in [PLAN.md](PLAN.md). The protected MISC transfer pair owns
  stock inventory/script/registry storage and guarded stock iterator bindings.
  Preparation remains isolated; no atomic transfer or live installation exists.
- **Next action:** prepare lifetime-checked resolution of unaffected source-owner,
  destination-owner and initiator registry/script bindings from explicit contexts,
  binding them to the same protected pair. Leave all other unaffected entries
  unresolved and keep installation/effects deferred.
- **Checkpoint:** `8850e745c298c6de629ef9a5a26bbdddf6aa56e3` preserves the working
  gameplay implementation before the engine-backed pivot.

## Implemented M2 slice

[ContainerStore::prepareTransfer](../../apps/openmw/mwworld/containerstore.cpp)
returns one move-only `PreparedContainerTransfer`. Its private stock selection and
LocalScripts cursor iterators bind exact pair relocation, removal quantity,
inventory lists, script storage and registry storage. Joint validation also checks
original/proposed values, live inventories/selections, script registrations/results/
cursors, registry mappings/revision/generated counter, contexts and deferred consumers.
No stock mutation caller or production authority changed.

Each prepared inventory now has an isolated stock ContainerStore owner. Stock
selection iterators traverse its owned MISC nodes and use its distinct end; they
cannot advance into live inventory lists. Raw dormant selections are preserved,
full removal clears a selected source item, and stock traversal skips zero counts.
Existing destination selections remain at their original position. Raw order,
signed partial/full counts, new-stack append and in-place destination replacement
remain covered. Item values stay detached with independently owned RefData;
original/proposed identities remain separate and no live RefNum is assigned.
Iterator Ptr container hints name only the isolated stock owner.

[LocalScripts::PreparedStorage](../../apps/openmw/mwworld/localscripts.hpp) retains
stock list nodes, immutable registration identities and compare-only cursor keys.
Private iterators select those nodes. End is stored logically and resolved against
the current list sentinel; no saved std::list end iterator survives replacement.
Shared services use one combined remove-then-append list/cursor; distinct services
retain independent positions. Only pair-owned nodes receive item pointers.
Unaffected registrations remain immutable keys with empty item views.

[PtrRegistry::PreparedStorage](../../apps/openmw/mwworld/ptrregistry.hpp) still owns
the stock map type with relocated keys, revision, counter and cell/container hints.
Protected identity membership selects owned pointers, including dormant entries;
unaffected mappings remain exact compare-only bindings with empty map slots.
Lookup exposes ConstPtr, without generating identities or changing item values.

Before accessing or comparing private saved iterators, validation checks current
owned storage identities, nodes, values, relocation, script storage and registry
results. Opt-in [node lifetime witnesses](../../apps/openmw/mwworld/livecellref.hpp)
reject identical payload reconstruction at the same address. Copies/moves create
new node lifetimes; assignment to an existing node preserves its lifetime. Stock
references/script entries carry lifetime metadata, but live nodes allocate no token.
These checks witness current state and node lifetime, not complete mutation history
or stable multiplayer identity.

Public iterator accessors validate owned storage and return read-only copies; they
do not establish live installation preconditions. Tokens/iterators are prepared
before the final fallible consumer copy and destroyed before their lists. Pair moves
preserve nodes, owners and sentinels; moved-from access rejects. Borrowed content,
services, owners/cells and WorldModel must outlive the pair. Public views expire
with its owned state.

## Fresh verification

Windows MSVC RelWithDebInfo, `build/vnext-product`, individually:

- `tes3mp_native_loadout_tests` focused build: exit 0.
- `inventory-transfer-preparation`: exit 0.
- `inventory-two-owners`: exit 0.
- Documentation budget/links, patch-registry semantic fields, formatting and
  whitespace checks: exit 0.

Synthetic disposable-stock comparisons now walk actual prepared iterators. Cases
cover empty/end positions, dormant selections/nodes, signed partial/full removal,
shared/distinct services, foreign/corrupted bindings and quantities, replaced owned
stores, destroyed and same-address reconstructed nodes, replaced end sentinels,
preparation failure, moves and discard. Snapshots preserve live inventories,
RefData flags/locals, scripts/cursors, selection, WorldModel mappings/revision/
counter and notifications. Non-end owned selections and script items remain usable
after live inventory destruction. No Environment, World, UI or Lua runtime initialized.

Logs: `build/logs/native-iterator-bindings-*`. No complete suites, expensive gates
or upstream baseline tests ran.

## Remaining limits and inherited evidence

Unaffected live-pointer resolution, installation, durability and effect execution
remain unprepared. No atomic transfer, installation-failure or allocator-fault
proof exists. Unresolved stores, equipment, gold/other types, Lua/custom state,
persistence and stable multiplayer instance mapping remain outside this slice.

Inherited M1 real-Morrowind/enchantment evidence under `build/native-loadout/real`
and `build/native-loadout/parity` was not rerun; TR remains unverified. Independent
networking/standalone targets and the migration base are unchanged. Broad
openmw-lib rendering dependencies still need extraction before production
headless packaging. Whole-baseline provenance debt remains; only touched
patch-registry entries changed.
