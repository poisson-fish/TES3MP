# Current state and next action

## Handoff

- **Direction:** OpenMW-backed authoritative cooperative multiplayer; see
  [README.md](README.md) and [DECISIONS.md](DECISIONS.md).
- **Milestone:** M2 in [PLAN.md](PLAN.md). Plain shirt preparation now exports
  complete supported owned values and restores them into fresh detached clothing
  inventory storage. This is synthetic staging, not live atomic equipment mutation,
  equipment file persistence or production integration.
- **Next action:** add a bounded in-memory plain-equipment byte codec around
  [PlainEquipmentValues](../../apps/openmw/mwworld/plainequipment.hpp), with explicit
  equipment-format, runtime/content and trusted actor bindings. Preserve every
  supported field through stock ESM serialization plus necessary lossless fields,
  exact saved counters and dormant selections; follow with strict decode/restore
  round trips and malformed/allocation rejection coverage. Keep transfer version 4,
  file persistence, installation and effect execution deferred.
- **Session scope:** complete 2–3 closely related bounded slices sequentially;
  one slice at a time limits concurrent scope, not slices per session. After
  review and commit, summarize results/commit and provide a ready-to-paste next
  session prompt with workspace, reading, concrete scope, verification and commit
  requirements. Carry these session-scope and final-response instructions forward.
- **Checkpoint:** 8850e745c298c6de629ef9a5a26bbdddf6aa56e3 preserves the working
  gameplay implementation before the engine-backed pivot.

## Implemented M2 slices

[PreparedPlainEquipment](../../apps/openmw/mwworld/plainequipment.cpp) still
reconstructs a protected candidate from a witnessed, resolved, actor-local inventory
of at most 64 plain shirts and runs shared stock equip/unequip/split/restack
mechanics. Actor/player, reference/store/service lifetimes, registry, source values,
listeners and raw slot/selection membership are revalidated. Gameplay effect
intents remain captured; equipment listeners are not silently disabled or executed.

Export stages owned ObjectState values using shared CellRef/RefData writers,
validates them and rechecks the source before nonthrowing publication. It retains
all supported CellRef fields, RefData position/enabled/activation/animation state,
clothing identities, signed counts, dormant nodes, shirt and selection identities,
and the exact proposed generation counter. It never publishes live pointers or
iterators; the legacy ObjectState converter must be null. Values survive candidate
and fixture destruction. Source runtime aliases do not enter the candidate.

RestoredPlainEquipment validates the complete bounded input and supplied actor
identity/content before staging fresh protected InventoryStore nodes. It relocates
shirt and selection onto those nodes, preserving dormant selection and counters
above surviving identities, including index rollover and representable exhaustion.
Re-export resolves raw members without constructing Ptr or allocating lazy node
lifetime tokens. No owner, registry, listener or script service is installed, and
no effect is executed. Caller content must outlive restored storage.

[Two-actor tests](../../apps/tes3mp-server/native/equipment_tests.cpp) independently
check complete populated fields and export/restore/export consistency across
positive/negative counts, split/no-split, restack/incompatible targets, dormant
selection, 65-node output, moves and input mutation. Malformed values, foreign
owners/content, unsupported state, stale witnesses and moved objects preserve prior
output storage/value, live state and captured effect intents. Individual C++
allocation failures cover export validation/serialization, detached restoration
and re-export, with leak-free rejection and deterministic recovery.

## Fresh verification

Windows MSVC, scripts/setup_msvc_env.ps1 -PreferLatest, RelWithDebInfo,
build/vnext-product, 2026-09-16. Target tes3mp_native_loadout_tests and each
individual filter below exited **0**. Logs use build/logs/native-equipment-.

| Filter suffix | Evidence / log suffix |
|---|---|
| export | 8 full-value cases; export.log |
| restore | 16 detached round trips; restore.log |
| value-guards | 96 rejected cases; value-guards.log |
| value-allocations | 264 injected failures; value-allocations.log |
| guards | 82 existing cases plus 24 stale exports; guards.log |
| preparation | shared preparation scenarios; preparation.log |
| allocations | 186 preparation failures; allocations.log |

Full filters start with inventory-equipment-. Final build: values-build.log.
Initial checks exited 1 for snapshot-copy flag loss, a duplicate fixture identity,
a valid-version rejection fixture and lazy restored-slot lifetime allocation.
Each was fixed and its check rerun successfully before continuing. Documentation
budget and local links ran individually, exit **0**: build/logs/docs-budget.log
and docs-links.log.

## Limits and inherited evidence

Equipment remains serialized-access, actor-local synthetic staging; matching the
supplied actor is not network authentication. Runtime change tracking, scenes and
caches are not saved values; postponed physics is explicitly rejected on export.
Scripts, enchantments, Lua/custom state and other slots/types remain unsupported.
Equipment byte/file persistence, live installation, production integration, script
execution and durable request/notification deduplication remain deferred. No
equipment success is durably acknowledged or published.

Transfer trusted caller matching, fixed owner/service roles, version-4 selections,
exact saved counters, persistence-before-install and owned publication are unchanged.
Prior transfer matrices and stock equipment seam evidence are inherited, not rerun.
Allocation tracking excludes direct C allocation, other threads and private external
allocators. No full suites, expensive gates or upstream baseline tests ran.
M1/TR evidence, independent networking, migration base, broad headless dependencies
and baseline provenance debt are unchanged.
