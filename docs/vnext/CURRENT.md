# Current state and next action

## Handoff

- **Direction:** OpenMW-backed authoritative cooperative multiplayer; see
  [README.md](README.md) and [DECISIONS.md](DECISIONS.md).
- **Milestone:** M2 in [PLAN.md](PLAN.md). Plain shirt equip/unequip now has
  shared explicit-context mechanics and isolated preparation exercised against
  two registered actors with separate stock InventoryStores. This is not live
  atomic equipment mutation, equipment persistence or production integration.
- **Next action:** add failure-atomic owned full-value export of the protected
  [plain-equipment candidate](../../apps/openmw/mwworld/plainequipment.cpp),
  using stock CellRef/RefData serializers with clothing identity, shirt slot,
  dormant membership, selection and proposed-counter metadata. Follow with strict
  detached restore/round-trip and allocation/rejection coverage. Keep installation,
  effect execution and file persistence deferred; preserve transfer version 4.
- **Session scope:** complete 2–3 closely related bounded slices sequentially;
  one slice at a time limits concurrent scope, not slices per session. After
  review and commit, summarize results/commit and provide a ready-to-paste next
  session prompt with workspace, reading, concrete scope, verification and commit
  requirements. Carry these session-scope and final-response instructions forward.
- **Checkpoint:** `8850e745c298c6de629ef9a5a26bbdddf6aa56e3` preserves the working
  gameplay implementation before the engine-backed pivot.

## Implemented M2 slices

[InventoryStore](../../apps/openmw/mwworld/inventorystore.cpp) equip/unequip and
[ContainerStore](../../apps/openmw/mwworld/containerstore.cpp) unstack/restack now
share explicit content, actor/player and service/effect callbacks with stock
callers. Stock registration, virtual removal, zero-count script cleanup,
OnPCEquip clearing and equipment listener dispatch remain connected. The
synchronous seam itself does not promise atomic live failure.

`PreparedPlainEquipment` accepts a witnessed resolved inventory, current item,
expected identity/registry revision and matching explicit actor/player. It bounds
input to 64 plain-shirt nodes, rejects unsupported state and reconstructs fresh
detached storage instead of copying an InventoryStore. CellRef values and owned
RefData survive; scene/Lua/custom aliases and live WorldModel links do not enter
the candidate. Stock slot mechanics, signed count arithmetic, stacking predicates,
new-stack activation handling and identity generation run on that storage.

Preparation retains owned IDs/counts, shirt/selection results, proposed counter,
and registration, cleanup, inventory and equipment effect intents. Equipment
listeners can run gameplay and are never invoked during preparation. There is
no installation or delivery API. Source store/reference/service lifetimes,
storage identity, registry membership/revision/counter, item values, script list,
listener bindings and slot/selection positions are rechecked. Current raw members
resolve positions without following saved iterators. Rejection preserves live
state and prior caller output storage/value; successful preparation does too.

[Synthetic tests](../../apps/tes3mp-server/native/equipment_tests.cpp) exercise
both actors, positive/negative counts, split/no-split, restack/incompatible targets,
shirt replacement, selection retention/clearing, move/discard and owned output
after fixture destruction. Guards cover foreign/stale bindings, same-address
reference/store/service replacement, destroyed owners, storage replacement,
foreign iterators, unsupported scripts/enchantments/custom state, count bounds,
counter exhaustion/collision and disabled updates. Live custom cloning is rejected.
Snapshots retain exact registry counters, source node addresses/values, inventory
metadata, script state and untouched live effect consumers. Individual C++
allocation failures cover preparation, output copying and revalidation; fresh
recovery gives the same proposal. Diagnostic rejection also preserves output
with allocation failure armed.

## Fresh verification

Windows MSVC, `scripts/setup_msvc_env.ps1 -PreferLatest`, RelWithDebInfo,
`build/vnext-product`, 2026-09-16. `tes3mp_native_loadout_tests` build and each
individual filter below exited **0**. Logs: `build/logs/native-equipment-` prefix.

| Filter suffix | Evidence / log suffix |
|---|---|
| `seam` | two-actor shared stock operations; `seam.log` |
| `preparation` | isolated candidates, effects and success variants; `preparation.log` |
| `guards` | 82 guarded rejections; `guards.log` |
| `allocations` | 186 injected allocation failures; `allocations.log` |

Full filters start with `inventory-equipment-`. Build: `reviewed-build.log`.
Fixture include/override build errors and guard-fixture/diagnostic expectations
were fixed and affected checks rerun. Documentation budget and local links ran
individually, exit **0**: `build/logs/docs-budget.log`, `docs-links.log`.

## Remaining limits and inherited evidence

Equipment preparation is serialized, actor-local and synthetic. It validates
current state, not mutation history or a trusted network command. Content and
WorldModel lifetimes remain caller obligations. Full owned equipment export,
restore, persistence, effect execution and production installation are unfinished.
Scripted/enchanted equipment and other slots/types remain unsupported explicitly.
No equipment success is durably acknowledged or published.

Transfer trusted caller matching, fixed owner/service roles, version-4 selections,
exact saved counters, persistence-before-install and owned publication remain
unchanged. Prior opposing-transfer evidence (640 winners/reissued losers) and
codec/restore/installation matrices are inherited, not rerun. Production
durability, live atomic transfer, script execution and durable request/notification
deduplication remain deferred. Allocation tracking excludes direct C allocation,
other threads and private external allocators. No complete suites, expensive gates
or upstream baseline tests ran. M1/TR evidence, independent networking, migration
base, broad headless dependencies and baseline provenance debt are unchanged.
