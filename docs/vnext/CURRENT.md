# Current state and next action

## Handoff

- **Direction:** OpenMW-backed authoritative cooperative multiplayer; see
  [README.md](README.md) and [DECISIONS.md](DECISIONS.md).
- **Milestone:** M2 in [PLAN.md](PLAN.md) has its first bounded owner-bound
  base-store add/remove slice. M1's inherited probe exit remains met. M2's actor,
  transfer, equipment, persistence, and production cutover criteria remain open.
- **Next action:** implement isolated item preparation for a resolved base-store
  transfer. Inspect `LiveCellRefBase`/`RefData` copy and registry/script lifetimes;
  prepare detached item state and deferred effects, then inject preparation failure
  and prove both live owners, script state, and registry remain unchanged with no
  success notifications. Keep equipment and unresolved stores excluded. Do not
  wire or claim atomic cross-container transfer until staging and failure handling
  are proven.
- **Checkpoint:** `8850e745c298c6de629ef9a5a26bbdddf6aa56e3` preserves the working
  gameplay implementation before the engine-backed pivot.

## Implemented M2 slice

[ContainerStore](../../apps/openmw/mwworld/containerstore.cpp) now shares removal
between the stock virtual entry and a registry/owner/LocalScripts/presentation
context. Registry-explicit owner binding and lookup avoid SafePtr's global lookup.
Explicit add/remove require a registered matching owner and a resolved base store;
derived stores, nonpositive counts, add-count overflow, and empty/dead/foreign
removal items reject before mutation or success. Removal checks actual membership,
including when a Ptr's public container hint is forged. Positive oversized removal
requests retain OpenMW's clamping semantics.

The shared removal body preserves negative restocking counts, cached-weight
invalidation, selected-enchantment cleanup, and listener ordering. Stock RefId
removal still dispatches virtually to InventoryStore; its equipment/replacement
logic remains intact. [CellRef](../../apps/openmw/mwworld/cellref.cpp) shares count
mutation with explicit LocalScripts cleanup when a stack reaches zero, retaining
the stock World wrapper. This removes a hidden global dependency without skipping
script unregistration.

The [native probe](../../apps/tes3mp-server/native/inventory.cpp) now binds its
registered owner and explicitly initializes an empty resolved store. The new
`inventory-two-owners` [filter](../../apps/tes3mp-server/native/loadout_tests.cpp)
uses OpenMW-written/loaded synthetic MISC/script records, two distinct registered
NPC references, and disposable base stores. It checks isolated add/stack/removal,
rejection without changes to inventories/registry/selection/notifications,
correct owner/item/count notifications, negative-stack arithmetic, full removal,
and removal of only the intended owner's script registration. No Environment,
World, WindowManager, or actor custom data is initialized; an empty InventoryStore
only exercises rejection.

## Fresh verification

Windows MSVC RelWithDebInfo in `build/vnext-product`, individually run:

- `tes3mp_native_loadout_tests` build: exit 0, including affected stock callers.
- `inventory-two-owners`, `inventory-plain`, `inventory-scripted`, and
  `inventory-rejection`: each exit 0.
- `tes3mp_native_loadout_probe` build: exit 0.

Initial build exit 2 reported an incomplete InventoryList type; the direct header
include fixed it. Initial two-owner exit `-1073741819` exposed CellRef's global
zero-count cleanup; explicit LocalScripts fixed it. Each failing check was fixed
and rerun before continuing. Final logs are
`build/logs/native-removal-build-script-cleanup.log`, `native-removal-build-probe.log`,
`native-removal-two-owners-retry.log`, and `native-removal-{plain,scripted,rejection}.log`
under the same directory. No complete suites or upstream baseline tests ran.

Reproduce one filter at a time with the existing dependency DLL directory on PATH
and output redirected to `build/logs`:

```text
build/vnext-product/tes3mp_native_loadout_tests.exe inventory-two-owners build/native-loadout/inventory-two-owners
```

## Remaining limits and inherited evidence

These synchronous engine operations are not transactional commands. Allocation,
script preparation, callbacks, durability failure, and cross-container transfer
still need isolated staging and installation. Script instructions, equipment and
enchanted effect execution, coherent inventory save/restore, stable multiplayer
instance mapping, and production actor integration remain unfinished. The two
NPC references provide owner identity only; this is not two live actor inventories.

Inherited M1 evidence was not rerun: normalized real Morrowind enumeration and
Bittercup/Dwemer puzzle-box initialization remain under `build/native-loadout/real`;
normal-client/native enchantment-charge captures remain under
`build/native-loadout/parity`. Tamriel Rebuilt remains unverified.

The opt-in [native targets](../../apps/tes3mp-server/native/CMakeLists.txt) remain
outside the independent server/standalone graph. No networking or migration-world
callers changed. Client-authoritative positions and migration worlds remain.
The probe still links broad openmw-lib/components OSG/OpenGL/MyGUI dependencies;
extract content/runtime libraries before production headless packaging. Linked
DLLs do not establish initialized services. Inherited whole-baseline provenance
debt remains; only touched entries were updated.
