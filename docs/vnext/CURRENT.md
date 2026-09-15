# Current state and next action

## Handoff

- **Direction:** OpenMW-backed authoritative cooperative multiplayer; see
  [README.md](README.md) and [DECISIONS.md](DECISIONS.md).
- **Milestone:** M2 in [PLAN.md](PLAN.md). The protected transfer pair now owns
  a source non-gold MISC inventory result and selection decision. Preparation
  remains isolated; this is not an atomic transfer.
- **Next action:** prepare an owned destination MISC inventory result from the
  pair, including existing-stack replacement, new-stack membership and unchanged
  selection, without live installation.
- **Checkpoint:** `8850e745c298c6de629ef9a5a26bbdddf6aa56e3` preserves the working
  gameplay implementation before the engine-backed pivot.

## Implemented M2 slice

[ContainerStore::prepareTransfer](../../apps/openmw/mwworld/containerstore.cpp)
returns one move-only `PreparedContainerTransfer`. Its private state binds source
removal, source inventory and selection, incoming/destination item values, stack
selection, script intents and both notification consumers to one removal quantity.

`getSourceInventory()` returns read-only detached item views with copied original
identity associations, in stock MISC iteration order. Stock removal retains a
zero-count node in storage but skips it during iteration. The prepared result
likewise owns dormant values while excluding them from membership views.
`getSourceItem()` returns the same owned removal result, including zero after
full removal. Partial removal preserves the signed remainder. No identity is
installed or allocated in WorldModel.

`getSourceSelection()` returns the original selected identity, clearing it only
when that item is fully removed. Partial removal, another selected member and no
selection match stock behavior. Preparation rejects dormant, foreign and non-MISC
selections. No inventory or script iterator is retained in the decision.

All raw source MISC nodes have original membership/value/registration witnesses
and independently owned proposed values. Remaining items preserve signed counts,
ownership, CellRef position/change tracking and applicable RefData, including
activation flags, locals and animation buffers. Copies have no scene link,
WorldModel registration, Lua/custom state or shared mutable buffers. Source MISC
nodes require matching content, initialized script locals and registered identity;
gold and unrepresentable signed counts reject.

Joint validation reacquires current source members, checks membership/order,
WorldModel bindings, original values, registrations and exact original selection,
then checks proposed values and selection against the removal quantity. The
original removal witness remains separate; changing live source state to the
proposed remainder still rejects. Dormant revival, remaining-item changes,
registration replacement and storage replacement invalidate the pair.

Existing destination preparation is preserved: stock first-stack selection,
signed counts and normalization, independent destination-stack RefData, new-stack
activation-flag clearing, registration-before-OnPCAdd and explicit player/owner
cell behavior. Full source removal only proposes deregistration. Notification
callables remain owned snapshots. Preparation, validation, failure, moves and
discard perform no live removal, deregistration, installation or effect execution.

Content records remain borrowed and immutable. Stores, owners/cells, WorldModel
and script services must outlive the decision; public views last only while it
owns its state. Moved-from source accessors and validation reject. Networking,
stock mutation callers and production authority are unchanged.

## Fresh verification

Windows MSVC RelWithDebInfo, `build/vnext-product`, individually:

- `tes3mp_native_loadout_tests` focused build: final exit 0. Initial exit 2 was a
  test clone helper requiring mutable Ptr from const iteration; corrected to ConstPtr.
- `inventory-transfer-preparation`: final exit 0. Initial exit 1 requested an
  unreachable activation flag combination in a new corruption fixture; corrected
  to a reachable change and rerun successfully.
- `inventory-two-owners`: exit 0.

The expanded synthetic filter compares disposable stock source inventories and
selection with owned results for plain/scripted items, both directions, signed
counts, partial/full removal, selected/unselected/other-selected sources,
multiple remaining items, dormant nodes and empty results. It preserves existing
destination comparisons and checks remaining stock registration cells/containers.

Corruption, stale membership/selection/registrations, remaining custom-state
rejection, late consumer-copy failure, move construction/assignment and discard
are covered. Snapshots compare live and dormant values, RefData flags/locals and
buffer identities, selection, weight, bindings, WorldModel mappings/revision/ID
counter, script membership/cursor and notifications. Lifetime counters and OSG
observers cover replaced/discarded remaining values as well as transfer items.
No Environment, World, UI or Lua runtime is initialized.

Logs: `build/logs/native-source-inventory-*`. No complete suites, expensive gates
or upstream baseline tests ran.

## Remaining limits and inherited evidence

Only the source MISC inventory projection is prepared. Destination inventory
membership and selection preparation are next. Unresolved stores, equipment,
gold/other types, Lua/custom state, live installation/effect failures, persistence,
durability and stable multiplayer instance mapping remain outside this slice.
Copy-time allocator faults are not injected; no atomic transfer is claimed.

Inherited M1 real-Morrowind/enchantment-charge evidence under
`build/native-loadout/real` and `build/native-loadout/parity` was not rerun; TR
remains unverified. Independent networking/standalone targets and the migration
base are unchanged. Broad openmw-lib rendering dependencies still need extraction
before production headless packaging. Whole-baseline provenance debt remains;
only the touched patch-registry entry changed.
