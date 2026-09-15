# Current state and next action

## Handoff

- **Direction:** OpenMW-backed authoritative cooperative multiplayer; see
  [README.md](README.md) and [DECISIONS.md](DECISIONS.md).
- **Milestone:** M2 in [PLAN.md](PLAN.md). The protected MISC transfer pair now
  owns inventory/selection, LocalScripts membership/cursor and WorldModel
  registry/identity results. Preparation remains isolated, not an atomic transfer.
- **Next action:** prepare owned stock MISC list storage and selection relocation
  from the protected pair, preserving dormant nodes and keeping proposed identities
  separate; bind prepared script/registry associations to those stable owned nodes
  without live installation or effects.
- **Checkpoint:** `8850e745c298c6de629ef9a5a26bbdddf6aa56e3` preserves the working
  gameplay implementation before the engine-backed pivot.

## Implemented M2 slice

[ContainerStore::prepareTransfer](../../apps/openmw/mwworld/containerstore.cpp)
returns one move-only `PreparedContainerTransfer`. Its private state binds source
removal, both inventories/selections, incoming/destination values, complete script
lists/cursors, registry results and deferred notification consumers to one removal
quantity. No stock mutation caller or production authority changed.

`getRegistry()` exposes an owned WorldModel mapping result with proposed revision
and generated-ID counter. Bindings retain compare-only reference keys and cell/
container associations, including unaffected world/other-owner entries.
`getDestinationIdentity()` returns the existing or proposed new ID;
`getRegistryItem(id)` resolves either inventory's ID to its detached owned value,
including dormant nodes, and returns empty for unrelated entries. It never follows
an old inventory reference. Existing inventory views retain original IDs; new
membership still uses an unset ID, with its proposed ID available separately.

Stock removal leaves the source registered even at zero count. Stock addition
registers its iterator Ptr before script-specific cell hints: it retains an
existing stack's ID, resets that mapping's cell hint, and increments the registry
revision even when replacing an identical binding. A new stack proposes one ID
using stock `CellRef::getOrAssignRefNum` on a scratch CellRef and owned counter.
No proposed item receives a RefNum or WorldModel link. Negative-content-file
rollover is preserved. New-ID preparation rejects nonnegative counter files,
exhaustion and collisions, including with the dormant source; unaffected mappings
must survive. Existing-stack preparation needs no generated identity. Live stock
generation, insertion and removal behavior remains unchanged.

Joint validation compares complete original registry membership/bindings,
revision and counter, then reconstructs the expected result from the protected
destination choice. Registry corruption and another pair's new binding reject.
Restoring a mapping through stock registration still changes its revision and
invalidates an old decision. This witnesses current state, not complete history:
resetting the counter back without a revision change is not detectable.

Both inventories preserve stock order, signed arithmetic, dormant values and
selection rules. Applicable RefData, locals and buffers are owned; new-stack
activation flags follow stock copying semantics. Entire LocalScripts results
preserve unaffected registrations and cursor behavior; shared services combine
removal then append. Immutable script identities and registry address keys can be
copied/compared without dereferencing unrelated destroyed objects.

Preparation, validation, failure, moves and discard perform no live removal,
deregistration, registration, installation or notification/effect execution.
Content records, stores, owners/cells, WorldModel and script services must outlive
the pair. Public views last while it owns its state; moved-from access rejects.
Copies have no scene, Lua/custom-state or shared mutable-buffer links.

## Fresh verification

Windows MSVC RelWithDebInfo, `build/vnext-product`, individually:

- `tes3mp_native_loadout_tests` focused builds: exit 0.
- `inventory-transfer-preparation`: exit 0.
- `inventory-two-owners`: exit 0.
- Documentation budget/links, patch-registry semantic fields, formatting and
  whitespace checks: exit 0.

Synthetic disposable-stock comparisons now reproduce complete registry mappings,
owner bindings and identity counters. Coverage includes both directions, signed
partial/full removal, existing/new stacks, plain/scripted items, dormant nodes,
unrelated owners and independent script/registry cell hints. Faults cover stale
membership/bindings/revisions/counters, corrupted proposals, identity boundaries,
OnPCAdd and late consumer-copy failures, moves and discard. Stock registry tests
cover first generation, repeated insertion, replacement and mismatched/absent
removal. Snapshots preserve live inventories, RefData flags/locals, script
registrations/cursors, selection, mappings/revision/counter and notifications.

Logs: `build/logs/native-registry-*`. No complete suites, expensive gates or
upstream baseline tests ran. No Environment, World, UI or Lua runtime initialized.

## Remaining limits and inherited evidence

Installable stock storage, relocation and live installation remain unprepared.
Unresolved stores, equipment, gold/other types, Lua/custom state, persistence,
durability and stable multiplayer instance mapping remain outside this slice.
Allocator faults are not injected; no installation/effect-failure proof exists.

Inherited M1 real-Morrowind/enchantment evidence under `build/native-loadout/real`
and `build/native-loadout/parity` was not rerun; TR remains unverified. Independent
networking/standalone targets and the migration base are unchanged. Broad
openmw-lib rendering dependencies still need extraction before production
headless packaging. Whole-baseline provenance debt remains; only the touched
patch-registry entry changed.
