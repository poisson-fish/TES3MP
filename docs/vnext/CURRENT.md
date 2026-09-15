# Current state and next action

## Handoff

- **Direction:** OpenMW-backed authoritative cooperative multiplayer; see
  [README.md](README.md) and [DECISIONS.md](DECISIONS.md).
- **Milestone:** M2 in [PLAN.md](PLAN.md). The protected MISC transfer pair now
  owns both inventory/selection results and full LocalScripts membership/cursor
  results. Preparation remains isolated; this is not an atomic transfer.
- **Next action:** prepare owned WorldModel registry membership/binding and
  generated-identity results from the protected MISC transfer pair, preserving
  dormant source identities and stock existing/new-stack registration behavior,
  without changing live mappings, revisions or identity counters.
- **Checkpoint:** `8850e745c298c6de629ef9a5a26bbdddf6aa56e3` preserves the working
  gameplay implementation before the engine-backed pivot.

## Implemented M2 slice

[ContainerStore::prepareTransfer](../../apps/openmw/mwworld/containerstore.cpp)
returns one move-only `PreparedContainerTransfer`. Its private state binds source
removal, both inventory results/selections, incoming/destination values, stack
selection, script-list results and deferred notification consumers to one removal
quantity. No stock mutation caller or production authority changed.

`getSourceScripts()` and `getDestinationScripts()` expose read-only results of
entire LocalScripts services, including unaffected world/other-owner entries.
Each result owns immutable registration witnesses in stock order and a numeric
cursor; size means end. Witnesses retain script, cell and container associations
and compare-only address keys, with no saved inventory Ptr or script iterator.
Snapshot and validation never dereference unrelated registered items, including
already-destroyed references retained by a stock list.

Full removal proposes stock first-match deregistration; partial removal retains
the source registration. Absent registrations stay absent. Scripted new stacks
append an owned registration bound to the same detached incoming value and
destination container/cell. Plain items leave registration membership intact.
Shared services expose one combined remove-then-append result; distinct services
own independent results and cursors. Removing at the cursor chooses its successor;
removing earlier shifts its numeric position. An end cursor stays at end through
append, including removal of the final registration.

The private add-preparation overload prepares the new list entry inside the
existing registration callback, before OnPCAdd. It allocates no live item identity,
registers nothing and runs no script instructions. Stock add/remove, duplicate
registration replacement, iteration and cursor repair remain the live behavior.

Joint validation compares complete original membership, immutable registration
identities and cursors, then checks proposed order, cursor and added-reference
bindings against the protected removal/addition. Unrelated entry removal,
addition, replacement or cursor movement invalidates the decision. Corrupted
results or additions from another pair reject. This checks current state; it
does not guarantee mutation history or provide an installation precondition.

Both inventory projections retain original identity associations, stock iteration
order, remaining values and selection rules. Dormant nodes remain owned outside
membership views. Existing destination stacks are replaced in place; new stacks
append the detached incoming value. Separate original witnesses protect source
remainder and destination arithmetic. Applicable RefData, locals and buffers are
owned; new-stack activation flags follow stock copying semantics.

Preparation, validation, failure, moves and discard perform no live removal,
deregistration, registration, installation or notification/effect execution.
Copies have no scene link, WorldModel registration, Lua/custom state or shared
mutable buffers. Immutable content records, stores, owners/cells, WorldModel and
script services must outlive the decision. Public views last while it owns its
state; moved-from accessors and validation reject.

## Fresh verification

Windows MSVC RelWithDebInfo, `build/vnext-product`, individually:

- `tes3mp_native_loadout_tests` focused builds: exit 0; no new warnings.
- `inventory-transfer-preparation`: exit 0, including final boundary additions.
- `inventory-two-owners`: exit 0.
- Documentation budget/links and patch-registry semantic fields: exit 0.
- Formatting and whitespace checks: exit 0.

Synthetic disposable-stock comparisons preserve full script order and bindings,
with shared/distinct services, both directions, plain/scripted items, absent
registrations, signed partial/full removal, existing/new destination stacks,
empty lists, final-entry removal and every cursor position. Unaffected entries
include other owners. Faults cover stale membership/cursor/registration,
corrupted result order/entries/cursor, another pair's addition, OnPCAdd preparation
failure, late notification-copy failure, moves and discard.

Snapshots verify inventories, dormant values, RefData flags/locals/buffers,
selection, WorldModel mappings/revision/ID counter, registrations/cursors and
notifications remain unchanged. Existing lifetime/scene observers cover owned
value cleanup. Deliberate stock registration failures remain expected diagnostics.
No Environment, World, UI or Lua runtime is initialized.

Logs: `build/logs/native-script-list-*`. No complete suites, expensive gates or
upstream baseline tests ran.

## Remaining limits and inherited evidence

WorldModel registry/identity results and live installation remain unprepared.
Unresolved stores, equipment, gold/other-type results, Lua/custom state,
installation/effect failures, persistence, durability and stable multiplayer
instance mapping remain outside this slice. Allocator faults are not injected.

Inherited M1 real-Morrowind/enchantment-charge evidence under
`build/native-loadout/real` and `build/native-loadout/parity` was not rerun; TR
remains unverified. Independent networking/standalone targets and the migration
base are unchanged. Broad openmw-lib rendering dependencies still need extraction
before production headless packaging. Whole-baseline provenance debt remains;
only touched patch-registry entries changed.
