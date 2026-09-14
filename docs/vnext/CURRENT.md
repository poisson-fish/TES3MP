# Current state and next action

## Handoff

- **Direction:** OpenMW-backed authoritative cooperative multiplayer; see
  [README.md](README.md) and [DECISIONS.md](DECISIONS.md).
- **Milestone:** M2 in [PLAN.md](PLAN.md) prepares detached MISC values,
  source-removal and destination stacking decisions, and deferred add effects
  between resolved base-store owners. This is not atomic cross-container transfer.
- **Next action:** prepare an owned deferred MWScript deregistration intent for
  full removal of initialized non-gold MISC, sharing stock `LocalScripts::remove`
  semantics and preserving script iteration without live removal or effect execution.
- **Checkpoint:** `8850e745c298c6de629ef9a5a26bbdddf6aa56e3` preserves the working
  gameplay implementation before the engine-backed pivot.

## Implemented M2 slice

[ContainerStore::prepareTransferRemove](../../apps/openmw/mwworld/containerstore.cpp)
now returns a move-only owned source decision for plain non-gold MISC. Read-only
accessors expose registered item identity, requested quantity and proposed remaining
signed count, including zero. Preparation and stock `removeWithContext` share the
full-removal choice and partial `subtractItems` calculation. Stock callers retain
clamping, zero-count script cleanup, selected-item repair and notification ordering.
Preparation rejects nonpositive/excess quantities, dead/foreign/unregistered items
and `INT_MIN` source counts before stock arithmetic or copying.

The private witness binds the source store, WorldModel, registered owner/cell and
item identity/address. A fresh unregistered reference owns the original signed
count, CellRef values and independent RefData; the proposed zero is never applied
to a reference. `validateTransferRemoval` reads only current MISC list members,
checks registry mappings and compares base/record identity, count, soul, ownership,
charge values, scale/position, locals, animation, deletion/enabled/physics state
and activation flags. RefData comparison neither consumes flags nor invokes script
services. Destroyed/replaced inventory references and iterators are never
dereferenced. Scene nodes are omitted; Lua/custom state and source scripts reject.

This is current-state validation, not mutation-history tracking. Content records
remain borrowed and immutable; store, WorldModel and owner/cell dependencies must
remain alive. Source and destination decisions are independent and do not bind
each other or prove that an incoming detached value still matches the source.

Existing preparation remains: `prepareTransferItem` shares source validation and
detached copying, accepting plain or matching initialized MWScript MISC values.
`prepareTransferAdd` owns the first compatible destination stack identity and
proposed signed count, or a new-stack decision, using stock selection/`addItems`.
Aggregate count guards include incompatible/dormant MISC nodes.
`validateTransferStacking` binds ordered destination membership/state and incoming
stacking values without retaining inventory iterators.

Stock add normalization and deferred `LocalScripts::prepareAdd`/OnPCAdd remain.
Explicit player destinations set OnPCAdd only on the copy with a null script cell;
nonplayers retain OnPCAdd and use the owner cell. Notification consumers are copied
into owned intents. Preparation, validation, rejection and discard never mutate
live inventories, trigger zero-count cleanup, register temporaries/scripts, advance
script iteration, execute scripts or emit success. No installation API exists.

## Fresh verification

Windows MSVC RelWithDebInfo, `build/vnext-product`, individually:

- `tes3mp_native_loadout_tests` build: exit 0 after fixing an initial exit-2
  owner-cell const-pointer mismatch and rerunning that target.
- `inventory-transfer-preparation`, `inventory-two-owners`, `inventory-scripted`:
  each exit 0.
- Documentation budget and local-link guards: each exit 0.

The preparation check covers both directions, partial/full removal, positive and
negative stock parity through exact integer limits, and invalid count rejection.
Changed source count/sign, values, locals, activation/animation, deletion, base,
script, owner/cell, identity and registry mappings reject without additional mutation.
Store replacement destroys source nodes while decisions survive; replacement nodes
with a reused identity still reject. Late notification-copy failures occur while
full source-removal and destination stacking state exist. Destructor counters and
OSG observers prove temporary cleanup. Destination stacking and scripted-item
preparation, including earlier OnPCAdd failures, remain covered.

Snapshots cover inventory values, locals/buffers, selection/weight, owner bindings,
registry mappings/revision/ID counter, script membership/cursor and notifications.
Fixtures use real engine base stores, two NPC identities and two cell objects;
they are synthetic, not live actor inventories. No Environment, World,
WindowManager, Lua runtime or actor custom data is initialized.

Logs: `build/logs/native-source-removal-*`. No complete suites, expensive gates or
upstream baseline tests ran.

## Remaining limits and inherited evidence

Scripted source-removal effects, live transfer/effect installation, unresolved
stores, equipment, gold/other item types, Lua/custom state, persistence, durability
and stable multiplayer instance mapping remain outside this slice. Copy-time
allocator faults and live installation failures are not covered.

Inherited M1 real-Morrowind/enchantment-charge evidence under
`build/native-loadout/real` and `build/native-loadout/parity` was not rerun; TR remains
unverified. Native targets remain outside independent networking and standalone
targets. Migration-world callers and client-authoritative positions are unchanged.
Broad openmw-lib rendering dependencies still require extraction before production
headless packaging. Whole-baseline provenance debt remains; only touched
patch-registry entries changed.
