# Current state and next action

## Handoff

- **Direction:** OpenMW-backed authoritative cooperative multiplayer; see
  [README.md](README.md) and [DECISIONS.md](DECISIONS.md).
- **Milestone:** M2 in [PLAN.md](PLAN.md) prepares detached MISC values,
  source-removal/destination stacking decisions and deferred script effects
  between resolved base-store owners. This is not atomic cross-container transfer.
- **Next action:** prepare one owned paired non-gold MISC transfer decision that
  binds source removal to the detached destination value and validates both
  owners and deferred intents together, without live installation.
- **Checkpoint:** `8850e745c298c6de629ef9a5a26bbdddf6aa56e3` preserves the working
  gameplay implementation before the engine-backed pivot.

## Implemented M2 slice

[ContainerStore::prepareTransferRemove](../../apps/openmw/mwworld/containerstore.cpp)
accepts non-gold MISC with matching initialized MWScript locals and an explicit
LocalScripts collection. Plain callers may still omit that service. Stock
`prepareRemoveCount` supplies signed partial/full counts without applying zero
or invoking cleanup. The move-only decision owns independent original item state;
full removal exposes a read-only deregistration intent only when stock cleanup
would find a registration. Partial removal preserves registration. Both bind its
presence or absence for validation.

[LocalScripts](../../apps/openmw/mwworld/localscripts.cpp) now owns immutable
registration identities containing script, CellRef address, cell and store.
Preparation shares ownership of that immutable identity, with no live item Ptr or
list iterator. Erase/reinsert of identical registration values invalidates the
witness. Stock CellRef removal and preparation share first-match address lookup;
lookup compares stored keys without dereferencing possibly destroyed references.
Both stock remove overloads preserve next-iterator repair. `clear` resets its
cursor to end, including before subsequent add/remove calls.

`validateTransferRemoval` first reacquires current source MISC members, then
checks source/owner/store/cell identity, WorldModel mappings, base/script identity,
signed count, CellRef values and independent RefData state, including all locals,
animation and activation flags. It checks the captured script collection and
registration identity only after establishing source membership/state. Stale
script, locals, registration, membership, registry mappings or source values
reject without invoking script services or reading destroyed inventory references
or erased script iterators.

Source state validation remains current-state comparison, not a mutation-history
tracker. Content records remain borrowed and immutable; stores, WorldModel,
LocalScripts and owner/cell dependencies must outlive the operation. Source and
destination decisions are still independent: they do not establish that an
incoming detached value matches the source or jointly validate deferred effects.

Existing `prepareTransferItem` detached copying and `prepareTransferAdd` first
compatible destination stack/count selection remain. Raw aggregate guards include
incompatible/dormant MISC nodes. Stock normalization and deferred registration/
OnPCAdd share the engine path: explicit player destinations change only copied
OnPCAdd and use a null script cell; nonplayers retain OnPCAdd and use the owner
cell. Notifications remain owned, unexecuted intents.

Preparation, validation, rejection and discard never mutate live inventories,
apply zero-count cleanup, register temporary references/scripts, advance script
iteration, execute scripts or emit success. No installation API exists.

## Fresh verification

Windows MSVC RelWithDebInfo, `build/vnext-product`, individually:

- `tes3mp_native_loadout_tests` build and fixture-fix rebuild: exit 0.
- `inventory-transfer-preparation`: initially exit 1 because the alternate-script
  fixture was absent from the two-owner ESMStore; fixed and rerun, exit 0.
- `inventory-two-owners`, `inventory-scripted`: each exit 0.
- Documentation budget and local-link guards: each exit 0.

The synthetic preparation check covers both scripted directions, partial/full
removal and positive/negative stock parity through exact integer limits. It
rejects stale locals buffers/identity, script/cell/store registration changes,
absence/reinsertion, source values/ownership/mappings and destroyed/replaced source
nodes, including reused item IDs. Script lookup passes a dangling registry Ptr
safely. Stock cursor checks cover both remove overloads, clearCell and clear at
unvisited, partially visited and exhausted positions.

Late notification-copy failure/discard runs with owned source state/deregistration
and destination item/registration intents. Destructor counters and OSG observers
verify temporary cleanup; snapshots verify inventories, locals/buffers, selection,
weight, owner bindings, registry mappings/revision/ID counter, script membership/
cursor and notifications. Plain removal, destination stacking and deferred OnPCAdd
failure coverage remain. No Environment, World, UI, Lua runtime or live actor
custom data is initialized. These are two NPC identities and real engine base
stores/cell objects, not live actor inventories.

Logs: `build/logs/native-script-removal-*`. No complete suites, expensive gates or
upstream baseline tests ran.

## Remaining limits and inherited evidence

Live removal/deregistration, destination installation, effect execution, unresolved
stores, equipment, gold/other item types, Lua/custom state, persistence, durability
and stable multiplayer instance mapping remain outside this slice. Copy-time
allocator faults and live installation failures are not covered.

Inherited M1 real-Morrowind/enchantment-charge evidence under
`build/native-loadout/real` and `build/native-loadout/parity` was not rerun; TR remains
unverified. Independent networking/standalone targets and the migration base are
unchanged. Broad openmw-lib rendering dependencies still need extraction before
production headless packaging. Whole-baseline provenance debt remains; only
touched patch-registry entries changed.
