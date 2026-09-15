# Current state and next action

## Handoff

- **Direction:** OpenMW-backed authoritative cooperative multiplayer; see
  [README.md](README.md) and [DECISIONS.md](DECISIONS.md).
- **Milestone:** M2 in [PLAN.md](PLAN.md). Paired non-gold MISC preparation now
  matches stock new-stack activation flags while retaining source and existing
  destination-stack RefData. This is not an atomic transfer.
- **Next action:** prepare an owned source MISC inventory result from the pair,
  including full-removal membership and selected-item handling, without live installation.
- **Checkpoint:** `8850e745c298c6de629ef9a5a26bbdddf6aa56e3` preserves the working
  gameplay implementation before the engine-backed pivot.

## Implemented M2 slice

[ContainerStore::prepareTransfer](../../apps/openmw/mwworld/containerstore.cpp)
returns one move-only `PreparedContainerTransfer`. Private owned state binds
source removal, source/incoming/destination results, stack selection, script
intents and both notification consumers to one removal quantity. Public access
exposes const item/removal views, scalar decisions and a copied registration value.
Moved-from decisions reject validation and source-result access.

`getSourceItem()` exposes an owned post-removal value derived from the original
source witness. It preserves applicable RefData, ownership and CellRef position,
with independent locals/animation buffers and no scene link or live identity.
[CellRef::copyWithCount](../../apps/openmw/mwworld/cellref.cpp) shares stock
count/change tracking without cleanup, including zero. Stock `setCount` callers
retain their existing cleanup behavior. Partial removal preserves the signed
remainder and registration; full removal proposes zero and deferred deregistration.

The original source witness remains separate. Joint validation first checks live
source state, then derives the expected source result from that witness and the
removal quantity, checking count, CellRef values/change tracking, RefData and
detachment. Changing live state to the proposed remainder still rejects.

Stock `addNewStack` copy-constructs RefData before registration and OnPCAdd.
[RefData](../../apps/openmw/mwworld/refdata.cpp) now shares that activation-flag
clearing with detached preparation, only when a new stack is selected. Suppression,
OnActivate and buffered activation are cleared; unrelated bits and other applicable
RefData survive. The original source witness and post-removal source retain their
flags and locals. Existing destination stacks retain their own RefData and flags.

Shared normalization resets ownership and CellRef position; signed arithmetic
uses the same removal quantity. `getDestinationItem()` returns the owned stack
result or the normalized incoming value for a new stack. Scripted items never
stack. Registration preparation precedes OnPCAdd; explicit player destinations
use a null script cell and set OnPCAdd, while nonplayers retain the incoming local
and use the owner cell. No public mutable view or activation mutator was added.

Validation also checks stores/owners, WorldModel mappings, content-store identity,
player/service/listener bindings, full-removal selection and registrations.
Both contexts require LocalScripts bound to the content store, including plain
registration absence. Destination witnesses include dormant MISC nodes. Only
current inventory members are read; no saved inventory/script iterators or script
service calls are used. Storage identities reject copy/move replacement even
when nodes/IDs are reused; immutable registration identities detect erase/reinsert.
Ordinary values may change and return to their captured state.

Content records remain borrowed and immutable. Stores, owners/cells, WorldModel
and script services must outlive the decision. Notification callables are owned
snapshots, not re-sourced from later validation contexts. Preparation, validation,
rejection, moves and discard leave live inventories, selection, scripts, locals,
registry mappings and notifications unchanged. No live removal, deregistration,
installation or effect execution API is introduced. Stock callers, networking
boundaries and production authority remain unchanged.

## Fresh verification

Windows MSVC RelWithDebInfo, `build/vnext-product`, individually:

- `tes3mp_native_loadout_tests` focused build: exit 0.
- `inventory-transfer-preparation`: exit 0 after fixing a test observation that
  called global-World `search`; initial exit was -1073741819. The replacement
  iterates current disposable-store members.
- `inventory-two-owners`, `inventory-scripted`: each exit 0.

The expanded synthetic filter compares source and destination RefData with stock
add/remove in disposable stores with separate owners, WorldModel and scripts.
Coverage includes every runtime/save-load reachable activation combination and
unrelated flag bits, both directions, signed source/destination counts, partial/full
removal, plain/scripted items, existing/new stacks, and player/nonplayer contexts.
It checks stock registration before OnPCAdd and detached clearing before that
lookup; corrupted new-stack flags, corrupted source results and normalized live
source flags reject. Move construction/assignment, discard and late consumer-copy
failure exercise the flag matrix.

Snapshots compare live values/buffers and supported RefData flags, weight,
selection, bindings, WorldModel mappings/revision/ID counter, script
membership/cursor and notifications. Late
consumer-copy failures unwind after all results exist; lifetime counters and OSG
observers check source/incoming/destination discard and replaced source-result
ownership. No Environment, World, UI or Lua runtime is initialized; actors are
synthetic NPC identities with real engine base stores/cells.

Logs: `build/logs/native-activation-*`. No complete suites, expensive gates or
upstream baseline tests ran.

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
only the touched patch-registry entries changed.
