# Current state and next action

## Handoff

- **Direction:** OpenMW-backed authoritative cooperative multiplayer; see
  [README.md](README.md) and [DECISIONS.md](DECISIONS.md).
- **Milestone:** M2 in [PLAN.md](PLAN.md). Paired non-gold MISC preparation now
  owns the proposed existing destination stack value as well as source removal,
  incoming values and deferred effects. This is not an atomic transfer.
- **Next action:** prepare an owned post-removal source MISC value, including
  zero-count full removal without script cleanup, and bind it into the pair.
- **Checkpoint:** `8850e745c298c6de629ef9a5a26bbdddf6aa56e3` preserves the working
  gameplay implementation before the engine-backed pivot.

## Implemented M2 slice

[ContainerStore::prepareTransfer](../../apps/openmw/mwworld/containerstore.cpp)
returns one move-only `PreparedContainerTransfer`. Private owned state binds
source removal, normalized incoming and destination values, stack/count selection,
script deregistration/registration and both notification consumers. Public access
exposes const item/removal views, scalar decisions and a copied registration value;
it cannot independently replace values or execute effects. Moved-from decisions
reject validation.

Stock stacking changes the selected destination reference. Its owned result now
comes from that exact destination witness, preserving applicable RefData even
when compatible source/destination values differ. Shared stock normalization
resets ownership and CellRef position; stock signed arithmetic adds the removal
quantity. Scene links and live identity are omitted. The incoming value remains
source-derived with that same quantity. `getDestinationItem()` returns the owned
stack result or the existing incoming value for a new stack.

Scripted items never stack. Their existing detached preparation, registration and
OnPCAdd ordering are preserved: explicit player destinations use a null script
cell; nonplayers retain the incoming local and use the owner cell. Partial source
removal preserves the signed remainder and registration; full removal proposes
zero and deregistration without cleanup. Inherited new-stack preparation retains
activation flags that stock copying clears; complete new-stack parity is not claimed.

`validateTransfer` checks both stores/owners, WorldModel mappings, content-store
identity, player/service/listener bindings, source selection relevant to full
removal, script registrations and owned item values. Both contexts require
LocalScripts bound to the content store, including plain-item registration
absence. Destination witnesses include raw/dormant MISC nodes, CellRef values,
independent RefData and registration identity. Validation derives the expected
destination result from the selected witness and removal quantity, checking
normalized values, signed count, RefData and detachment jointly. It uses current
members, without script service calls or saved inventory/script iterators.

Each base store has an owned storage identity. Copy/move replacement invalidates
paired witnesses even when list assignment reuses node addresses and item IDs.
Immutable LocalScripts registration identities detect erase/reinsert; address-only
lookup remains safe past dangling registry Ptrs. These checks do not track every
mutation: ordinary values may change and return to their captured state. Content
records are borrowed and immutable; stores, owners/cells, WorldModel and script
services must outlive the decision. Notification callables are owned snapshots,
not re-sourced or compared against callables in later validation contexts.

Preparation, validation, rejection and discard leave live inventories, selection,
scripts, locals, registry mappings and notifications unchanged. There is no live
removal, deregistration, destination installation or effect execution API in the
pair. Stock callers continue through the shared engine rules; networking boundaries
and production authority are unchanged.

## Fresh verification

Windows MSVC RelWithDebInfo, `build/vnext-product`, individually:

- `tes3mp_native_loadout_tests` focused build: exit 0.
- `inventory-transfer-preparation`: exit 0.
- `inventory-two-owners`, `inventory-scripted`: each exit 0.
- Documentation budget, local-link and patch-registry semantic-field guards:
  each exit 0.

The expanded synthetic preparation filter compares results with stock add/remove
in disposable stores with separate owners, WorldModel and script registry. It
covers both directions, positive/negative source and destination counts,
partial/full source removal, differing compatible RefData, new/scripted stacks,
player contexts, move/discard, stale/replaced destinations and corrupted owned
results. Existing-stack RefData, including activation flags, matches stock.

Snapshots compare live values/buffers, weight, selection, bindings, WorldModel
mappings/revision/ID counter, script membership/cursor and notifications. Late
consumer-copy exceptions unwind after the destination result exists; lifetime
counters check notification unwinding and item/OSG observers check discard of
both owned values. Destination custom-state and overflow rejection are covered.
No Environment, World, UI or Lua runtime is initialized; actors are synthetic
NPC identities with real engine base stores/cells.

Logs: `build/logs/native-destination-value-*`. No complete suites, expensive gates
or upstream baseline tests ran.

## Remaining limits and inherited evidence

Unresolved stores, equipment, gold/other item types, Lua/custom state, live
installation/effect failures, persistence, durability and stable multiplayer
instance mapping remain outside this slice. Copy-time allocator faults are not
injected. No coherent post-transfer inventories or atomic transfer are claimed.

Inherited M1 real-Morrowind/enchantment-charge evidence under
`build/native-loadout/real` and `build/native-loadout/parity` was not rerun; TR
remains unverified. Independent networking/standalone targets and the migration
base are unchanged. Broad openmw-lib rendering dependencies still need extraction
before production headless packaging. Whole-baseline provenance debt remains;
only touched patch-registry entries changed.
