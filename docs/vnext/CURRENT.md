# Current state and next action

## Handoff

- **Direction:** OpenMW-backed authoritative cooperative multiplayer; see
  [README.md](README.md) and [DECISIONS.md](DECISIONS.md).
- **Milestone:** M2 in [PLAN.md](PLAN.md). Paired non-gold MISC preparation now
  binds source removal, detached incoming values, destination stacking and deferred
  effects between resolved base-store owners. This is not an atomic transfer.
- **Next action:** prepare an owned post-transfer value for an existing destination
  MISC stack, preserving its RefData and applying stock normalization/count rules,
  and bind it into the pair without live installation.
- **Checkpoint:** `8850e745c298c6de629ef9a5a26bbdddf6aa56e3` preserves the working
  gameplay implementation before the engine-backed pivot.

## Implemented M2 slice

[ContainerStore::prepareTransfer](../../apps/openmw/mwworld/containerstore.cpp)
returns one move-only `PreparedContainerTransfer`. Private owned state contains
source removal, the detached normalized incoming item, destination stack/count
selection, script deregistration/registration and both notification consumers.
The item is derived from the owned source witness and its removal quantity.
Public access exposes const item/removal views, scalar decisions and a copied
registration value; it cannot independently replace the item or execute effects.
Moved-from decisions reject validation.

Preparation composes existing engine source/count checks, detached copying,
first-compatible-stack selection, normalization and deferred OnPCAdd. Partial
removal preserves the signed remainder and registration; full removal proposes
zero and deregistration without invoking cleanup. Explicit player destinations
change only copied OnPCAdd and use a null script cell; nonplayers retain the
incoming local and use the owner cell. Existing-stack decisions still hold the
normalized incoming value plus a count proposal, not an installed target value.

`validateTransfer` checks both stores/owners, WorldModel mappings, content-store
identity, player/service/listener bindings, source selection relevant to full
removal, script registrations and owned item values. Both contexts require
LocalScripts bound to the content store, including plain-item registration
absence. Destination witnesses include raw/dormant MISC nodes, CellRef values,
independent RefData and registration identity. Validation uses current members;
it does not call script services or read saved inventory/script iterators.

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

- `tes3mp_native_loadout_tests` initial/reviewed focused builds: each exit 0.
- `inventory-transfer-preparation` initial/expanded checks: each exit 0.
- `inventory-two-owners`, `inventory-scripted`: each exit 0.
- Documentation budget, local-link and patch-registry semantic-field guards:
  each exit 0.

The synthetic filter covers both directions, plain/scripted partial/full signed
removal, player/nonplayer/no-player destinations, stacking/new-stack choices,
move/discard and stale source/destination/context rejection, including owner/content
replacement, full-removal selection and source/destination storage replacement.
Snapshots compare
inventory values and backing buffers, weight, selection, owner bindings,
WorldModel mappings/revision/ID counter, script membership/cursor and notifications.
Late notification-copy exceptions occur after both owned decisions and destination
intents exist; notification lifetime counters verify unwinding. Detached-item
destructor/OSG observers cover discard and inherited independent preparation
failure paths. No Environment, World, UI, Lua runtime or actor custom data is
initialized: these are synthetic NPC identities with real engine base stores/cells.

Logs: `build/logs/native-paired-transfer-*`. No complete suites, expensive gates
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
