# Current state and next action

## Handoff

- **Direction:** OpenMW-backed authoritative cooperative multiplayer; see
  [README.md](README.md) and [DECISIONS.md](DECISIONS.md).
- **Milestone:** M2 in [PLAN.md](PLAN.md) prepares detached MISC values,
  destination stacking decisions and deferred MWScript/notification intents
  between resolved base-store owners. This is not atomic cross-container transfer.
- **Next action:** prepare an owned source-removal decision for plain non-gold
  MISC using stock `subtractItems`, binding source identity/count/item state and
  rejecting stale source state without live removal or effect emission.
- **Checkpoint:** `8850e745c298c6de629ef9a5a26bbdddf6aa56e3` preserves the working
  gameplay implementation before the engine-backed pivot.

## Implemented M2 slice

[ContainerStore::prepareTransferItem](../../apps/openmw/mwworld/containerstore.cpp)
validates distinct registered owners, resolved base stores, source membership,
registration and bounded counts. It accepts non-gold MISC with plain or matching
initialized MWScript state. Its uniquely owned fresh reference has no registry
identity, WorldModel, cell or store links. RefData owns copied locals, position,
animation, flags and runtime values; scene nodes are omitted, Lua/custom state
rejects, and the immutable ESM base record remains borrowed.

`prepareTransferAdd` now owns a destination stacking decision: the first compatible
stack's identity and proposed signed count, or an unset identity indicating a new
detached stack. The incoming value remains owned separately when an existing stack
is selected. Stock `addImp` and preparation share first-stack selection, including
virtual `stacks` and stock equipment exclusion. Preparation calls stock `addItems`;
negative destination counts remain negative. A shared preparation guard bounds
same-ID aggregate counts at detachment and again before destination preparation,
including incompatible stacks. `INT_MIN` rejects before stock arithmetic; iterator
zero checks now use raw counts without changing which nodes are visited.

The decision binds the destination store, content/WorldModel, registered owner and
owner cell, incoming stacking state, and ordered MISC membership/state witnesses,
including dormant nodes. Destination MISC nodes must be registered, have matching
script locals and exclude Lua/custom state. `validateTransferStacking` checks
current members and registry mappings against owned witnesses for identity, base,
record ID, soul, script, signed count and deletion. It never dereferences saved
inventory references or iterators, including after store replacement destroys
nodes. This is current-state validation, not a mutation-history counter or a
source/script-effect/durability validator. Content records and operation dependencies
must remain alive and content immutable.

Existing deferred behavior remains: stock normalization resets CellRef
position/ownership fields on the temporary; other item values survive.
`LocalScripts::prepareAdd` prepares script/cell intent and preserves initialized
locals. Explicit player destinations set OnPCAdd only on the copy and use a null
script cell; nonplayers retain OnPCAdd and use the owner cell. Script instructions
never execute. Notification consumers are copied into owned intents, never invoked.
Stock immediate registration, exception handling, iterator repair and notification
ordering remain intact.

Preparation, validation, rejection and discard never change either live inventory,
register temporary references/scripts, advance script iteration or emit success.
There is no live installation API. These operation-local values are not a durable
or commit-ready transfer object.

## Fresh verification

Windows MSVC RelWithDebInfo, `build/vnext-product`, individually:

- `tes3mp_native_loadout_tests` build: exit 0, including affected stock callers.
- `inventory-transfer-preparation`, `inventory-scripted`, `inventory-two-owners`:
  each exit 0.
- Documentation budget and local-link guards: each exit 0.

The extended two-owner preparation check covers compatible/incompatible and empty
destinations, first-match selection, scripted separation, positive/negative count
parity with actual stock adds, exact count limits and aggregate overflow. Stale
count/sign, soul, script/base, deletion, owner/cell, membership, registry mapping
and incoming-value changes reject. Store replacement exercises destroyed-node
lifetimes. Late notification-copy failures occur after owned stacking state exists;
destructor counters and OSG observers prove cleanup. Earlier OnPCAdd failure and
script preparation coverage remains.

Snapshots cover inventory values, locals/buffers, selection/weight, owner bindings,
registry mappings/revision/ID counter, script membership/cursor and notifications.
Fixtures use real engine base stores, two NPC identities and two cell objects;
they are not live actor inventories. No Environment, World, WindowManager, Lua
runtime or actor custom data is initialized.

Logs are under `build/logs/native-stacking-*`. No complete suites, expensive gates
or upstream baseline tests ran.

## Remaining limits and inherited evidence

Live transfer/effect installation, unresolved stores, equipment, gold/other item
types, Lua/custom state, persistence, durability and stable multiplayer instance
mapping remain outside this slice. Copy-time allocator faults and live installation
failures are not covered.

Inherited M1 real-Morrowind and enchantment-charge parity evidence under
`build/native-loadout/real` and `build/native-loadout/parity` was not rerun; TR remains
unverified. The opt-in native targets remain outside independent networking and
standalone targets. Migration-world callers and client-authoritative positions are
unchanged. Broad openmw-lib rendering dependencies still require extraction before
production headless packaging. Inherited whole-baseline provenance debt remains;
only the touched patch-registry entry changed.
