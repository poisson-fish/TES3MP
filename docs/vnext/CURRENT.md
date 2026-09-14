# Current state and next action

## Handoff

- **Direction:** OpenMW-backed authoritative cooperative multiplayer; see
  [README.md](README.md) and [DECISIONS.md](DECISIONS.md).
- **Milestone:** M2 in [PLAN.md](PLAN.md) now has isolated item preparation between
  two resolved base-store owners, following owner-bound add/remove. This is not
  atomic cross-container transfer. M2's actor, equipment, persistence, and
  production cutover criteria remain open; M1's inherited probe exit remains met.
- **Next action:** extract deferred MWScript registration/OnPCAdd preparation for
  this detached non-gold MISC path from stock add semantics. Inject failure after
  effect preparation and verify both owners' locals, script/registry state, and
  notifications remain unchanged; keep live transfer installation excluded.
- **Checkpoint:** `8850e745c298c6de629ef9a5a26bbdddf6aa56e3` preserves the working
  gameplay implementation before the engine-backed pivot.

## Implemented M2 slice

[ContainerStore::prepareTransferItem](../../apps/openmw/mwworld/containerstore.cpp)
validates distinct registered owners, resolved base stores, actual source
membership/registration, positive bounded requests, and destination count overflow.
It accepts non-gold MISC with plain or already initialized MWScript state. It
returns a uniquely owned fresh LiveCellRef with copied CellRef values, the requested
positive count, no RefNum, and no WorldModel, store, or cell link. Its immutable
base record remains borrowed from the loaded ESMStore, which must outlive it.

[RefData::copyForContainerTransfer](../../apps/openmw/mwworld/refdata.cpp) explicitly
copies owned locals, position, animation values, flags, and change/runtime state.
It omits scene nodes and rejects Lua/custom state instead of sharing mutable
objects or trusting arbitrary clone implementations. Stock RefData copies and
LiveCellRef/ContainerStore copies remain unchanged. Those copies retain registry,
scene/Lua, listener, or owner links and are not staging mechanisms.

Preparation neither stacks nor changes either inventory. It does not register
scripts, assign OnPCAdd, allocate a live registry identity, call listeners, or
publish presentation success. Source soul, condition/charge fields, ownership,
script identity and short/long/float locals are preserved as item data. This does
not execute condition, enchantment, or script gameplay.

The new `inventory-transfer-preparation`
[filter](../../apps/tes3mp-server/native/loadout_tests.cpp) reuses the two-owner
synthetic OpenMW fixture. In both directions it prepares plain and scripted MISC,
mutates detached values/locals/animation/activation state, and injects an exception
in subsequent preparation while the temporary remains owned. Destructor counters
and an OSG observer prove temporary cleanup during failure and ordinary discard.
Snapshots verify both inventories' values, signed counts, mutable buffer addresses,
selection, weights, owner bindings, registry mappings/revision/ID counter, and
notification counts. Live script membership, owner hints, and iteration cursor
remain unchanged. Checks also cover forged membership, bad counts, overflow,
unregistered items, uninitialized locals, custom-state rejection, and both source
and destination unresolved/equipment exclusions.

The preceding shared explicit-context add/remove path retains stock stacking,
negative-count arithmetic, clamping, selected-enchantment cleanup, and LocalScripts
zero-count unregistration. Stock removal still dispatches through InventoryStore.
Two registered NPC references supply identity for disposable base stores; these
are not two live actor inventories. No Environment, World, WindowManager, Lua
runtime, or actor custom data is initialized.

## Fresh verification

Windows MSVC RelWithDebInfo, `build/vnext-product`, one check at a time:

- `tes3mp_native_loadout_tests` build: exit 0, including affected stock callers.
- `inventory-transfer-preparation`, `inventory-two-owners`, `inventory-scripted`:
  each exit 0.
- Documentation budget and local-link guards: each exit 0.

Initial build exit 2 reported undefined `osg::observer_ptr`; its direct include
fixed it, and the same build passed before testing continued. Final logs under
`build/logs` are `native-preparation-build-final.log`,
`native-preparation-transfer-final.log`, `native-preparation-two-owners.log`,
`native-preparation-scripted.log`, `native-preparation-docs-budget.log`, and
`native-preparation-docs-links.log`. No complete suites, expensive gates, or
upstream baseline tests ran. Run the new filter with dependency DLLs on PATH:

```text
build/vnext-product/tes3mp_native_loadout_tests.exe inventory-transfer-preparation build/native-loadout/inventory-transfer-preparation
```

## Remaining limits and inherited evidence

Gold normalization, other item types, Lua/custom state, deferred script/effect
installation, destination stacking, allocator-fault injection inside value copying,
equipment, durability, coherent save/restore, stable multiplayer instance mapping,
and production transfer installation remain unfinished. The injected failure is
after owned item preparation, not a durability or live-installation failure.

Inherited M1 real-Morrowind enumeration and initialization remain under
`build/native-loadout/real`; normal-client/native enchantment-charge captures remain
under `build/native-loadout/parity`. They were not rerun; TR remains unverified.

The opt-in [native targets](../../apps/tes3mp-server/native/CMakeLists.txt) stay
outside independent networking/standalone targets. No networking or migration-world
callers changed; client-authoritative positions remain. Broad openmw-lib rendering
dependencies still require extraction before production headless packaging.
Inherited whole-baseline provenance debt remains; only touched entries changed.
