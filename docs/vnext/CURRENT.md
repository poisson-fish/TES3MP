# Current state and next action

## Handoff

- **Direction:** OpenMW-backed authoritative cooperative multiplayer; see
  [README.md](README.md) and [DECISIONS.md](DECISIONS.md).
- **Milestone:** M2 in [PLAN.md](PLAN.md). The protected transfer pair now owns
  both non-gold MISC inventory results and selection decisions. Preparation
  remains isolated; this is not an atomic transfer.
- **Next action:** prepare owned LocalScripts membership and cursor results from
  the protected MISC transfer pair, preserving full-removal deregistration and
  new-stack registration order, without live list changes.
- **Checkpoint:** `8850e745c298c6de629ef9a5a26bbdddf6aa56e3` preserves the working
  gameplay implementation before the engine-backed pivot.

## Implemented M2 slice

[ContainerStore::prepareTransfer](../../apps/openmw/mwworld/containerstore.cpp)
returns one move-only `PreparedContainerTransfer`. Its private state binds source
removal, both inventory results/selections, incoming/destination item values,
stack selection, script intents and both notification consumers to one removal
quantity. No stock mutation caller or production authority changed.

`getSourceInventory()` and `getDestinationInventory()` return read-only detached
MISC views in stock iteration order with original identity associations. Each raw
node has independently owned original and proposed values. Dormant zero-count
nodes remain owned but are absent from membership views. Full source removal
likewise retains a dormant result; partial removal preserves the signed remainder.
The original removal witness remains separate from the proposed source value.

Destination addition replaces the first compatible stack's proposed value in
place, preserving its applicable RefData. Otherwise, membership appends the same
owned incoming value returned by `getDestinationItem()`, with an unset identity.
No live identity is allocated or installed. The previous separate destination
stack result is replaced by the inventory-owned value. Remaining destination
items preserve counts, ownership, CellRef position/change tracking, locals,
activation flags and animation buffers.

`getSourceSelection()` clears only a fully removed selected item.
`getDestinationSelection()` retains the original selection, including when its
stack is replaced. Neither decision retains an inventory iterator. Dormant,
foreign and non-MISC selections reject. All MISC nodes require matching content,
initialized script locals and registered identities; gold and unrepresentable
signed counts reject.

Joint validation reacquires current source/destination nodes, checks raw
membership/order, original values, WorldModel bindings, script registrations and
original selections, then validates both proposed inventories against the removal
quantity. Remaining-item changes, dormant revival, registration replacement and
storage replacement invalidate the pair. Corrupted proposed counts, membership,
item values, RefData or live bindings reject. This is a current-state check, not
a mutation-history guarantee.

Stock signed arithmetic, normalization, new-stack activation-flag clearing and
registration-before-OnPCAdd ordering remain shared. Player/owner cell behavior
and source inventory preparation are preserved. Full removal only proposes script
deregistration. Notification callables remain owned snapshots. Preparation,
validation, failure, moves and discard perform no live removal, deregistration,
registration, installation or effect execution.

Copies have no scene link, WorldModel registration, Lua/custom state or shared
mutable buffers. Content records remain borrowed and immutable. Stores,
owners/cells, WorldModel and script services must outlive the decision. Public
views last only while it owns its state; moved-from accessors/validation reject.

## Fresh verification

Windows MSVC RelWithDebInfo, `build/vnext-product`, individually:

- `tes3mp_native_loadout_tests` focused build: exit 0; final build has no new warnings.
- `inventory-transfer-preparation`: final exit 0. A review-time exit 1 found the
  new non-MISC selection fixture's book missing from the isolated ESMStore;
  fixed fixture provisioning, rebuilt and reran the same filter successfully.
- `inventory-two-owners`: exit 0.
- Documentation budget/links and patch-registry semantic fields: exit 0.
- Formatting: final exit 0; post-fixture whitespace failure corrected and rechecked.

Synthetic disposable-stock comparisons cover plain/scripted items, both directions,
signed counts, partial/full removal, existing/new destination stacks, first-match
order, selected/other-selected/unselected destinations, remaining items, empty
membership and dormant nodes in both stores. Remaining registration cells and
containers, stock scene cleanup and registration-before-OnPCAdd are compared.

Fault cases cover stale destination membership/selection/registrations, corrupted
proposed results, unsupported state, late consumer-copy failure, moves and discard.
Snapshots check live/dormant values, RefData flags/locals and buffer identities,
selection, weight, WorldModel mappings/revision/ID counter, script membership/cursor
and notifications. Lifetime counters and OSG observers include remaining destination
values. No Environment, World, UI or Lua runtime is initialized.

Logs: `build/logs/native-destination-inventory-*`. No complete suites, expensive
gates or upstream baseline tests ran.

## Remaining limits and inherited evidence

Script-list results and WorldModel installation state remain unprepared. Unresolved
stores, equipment, gold/other-type results, Lua/custom state, live installation/effect
failures, persistence, durability and stable multiplayer instance mapping remain
outside this slice. Copy-time allocator faults are not injected; no atomic transfer
is claimed.

Inherited M1 real-Morrowind/enchantment-charge evidence under
`build/native-loadout/real` and `build/native-loadout/parity` was not rerun; TR
remains unverified. Independent networking/standalone targets and the migration
base are unchanged. Broad openmw-lib rendering dependencies still need extraction
before production headless packaging. Whole-baseline provenance debt remains;
only the touched patch-registry entry changed.
