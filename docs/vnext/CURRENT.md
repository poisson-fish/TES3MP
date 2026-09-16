# Current state and next action

## Handoff

- **Direction:** OpenMW-backed authoritative cooperative multiplayer; see
  [README.md](README.md) and [DECISIONS.md](DECISIONS.md).
- **Milestone:** M2 in [PLAN.md](PLAN.md). Plain equipment now has a
  test-target-only bound file adapter and atomic detached file restart. This is
  synthetic persistence staging, not live installation or production integration.
- **Next action:** add a test-target-only durable plain-equipment commit boundary
  around [PreparedPlainEquipment](../../apps/openmw/mwworld/plainequipment.hpp):
  stage actor-local installation and captured gameplay effects, revalidate trusted
  caller/actor/service witnesses, then persist through
  [EquipmentFileSink](../../apps/tes3mp-server/native/equipment_file.hpp) before
  nonthrowing installation and owned publication. Preserve stock effect semantics
  and fail closed on uncertainty; limit the slice to existing plain shirts.
- **Session scope:** complete 2–3 closely related bounded slices sequentially;
  one slice at a time limits concurrent scope, not slices per session. After
  review and commit, summarize results/commit and provide a ready-to-paste next
  session prompt with workspace, reading, concrete scope, verification and commit
  requirements. Carry these session-scope and final-response instructions forward.
- **Checkpoint:** 8850e745c298c6de629ef9a5a26bbdddf6aa56e3 preserves the working
  gameplay implementation before the engine-backed pivot.

## Implemented M2 slices

[EquipmentFileSink](../../apps/tes3mp-server/native/equipment_file.cpp) validates
and encodes equipment format 1 with trusted runtime/content/actor bindings before
I/O. Complete owned bytes publish only after acceptance by nonthrowing swap.
The adapter retains no borrowed bindings, pointers or iterators. Synthetic
bindings remain neither authentication nor a production loadout fingerprint.

[BoundedFileSink](../../apps/tes3mp-server/native/bounded_file.cpp) extracts the
existing transfer sink's byte-only durability discipline: exclusive sibling
staging, bounded short writes, checked flush/close, replacement, barrier and
allocation-free verification. Safe rejection preserves prior committed bytes and
output storage/value. Replacement uncertainty is sticky and blocks even encoding
on retry. Recovery requires a new composition after validated restart. Windows
requests write-through replacement; no separate directory-fsync or power-loss
proof is claimed. Transfer format 4 and its 8 MiB bound are unchanged.

Equipment reads check regular-file size on the opened handle before allocating
bytes; the equipment bound is 80 MiB, with existing 2 MiB/object, 65-object,
256-animation and 4096-byte text codec limits. Exact reads, EOF and close precede
publication. Restart stages read, strict codec decode and detached restoration,
then publishes one complete owned object. Every read/decode/restore failure
preserves prior output storage/value. Caller content must outlive restored stock
storage; no owner, registry, listener or script service is installed.

[Focused tests](../../apps/tes3mp-server/native/equipment_tests.cpp) cover both
actors, complete supported values, distinct clothing identities, signed counts,
dormant membership/selection, shirt slot, split/restack behavior, maximum nodes,
and exact counters including rollover/exhaustion. Re-export/re-encoding is byte
identical. A valid 9,574,212-byte file proves the equipment bound reaches every
I/O stage. Source destruction, file removal and empty inventory exercise owned
lifetimes. Faults preserve both live actors and captured effect intents; uncertain
files contain a complete prior/new state with no accepted result or effects.

## Fresh verification

Windows MSVC, scripts/setup_msvc_env.ps1 -PreferLatest, RelWithDebInfo,
build/vnext-product, 2026-09-16. Target tes3mp_native_loadout_tests and these
individual filters exited **0**. Logs are under build/logs.

| Filter | Evidence | Log |
|---|---|---|
| inventory-equipment-file-write | 2 bound writes | native-equipment-file-write.log |
| inventory-equipment-file-restart | 16 detached round trips | native-equipment-file-restart.log |
| inventory-equipment-file-guards | 12 safe, 16 uncertain, 62 read/input rejections | native-equipment-file-guards.log |
| inventory-equipment-file-allocations | 1,836 injected failures, leak-free cleanup | native-equipment-file-allocations.log |
| inventory-equipment-file-bounds | 3 bounds/lifetime cases | native-equipment-file-bounds.log |
| inventory-equipment-codec | 16 byte/detached round trips | native-equipment-file-codec.log |
| inventory-equipment-restore | 16 detached round trips | native-equipment-file-restore.log |
| inventory-transfer-file-sink | 48 cases; 384 safe, 384 uncertain, 31 input rejections; 1,024 allocation failures | native-equipment-transfer-file-sink.log |

Final build: native-equipment-file-final-build.log. Documentation budget and
local links ran individually, exit **0**: docs-budget.log and docs-links.log.
No build/test failures occurred in this session.

## Limits and inherited evidence

Live equipment installation, production integration, scripts, enchantments,
other slots/types and durable request/notification deduplication remain deferred.
File acceptance authorizes no gameplay success notification or effect execution.
Runtime scenes/caches are not saved values; postponed physics remains rejected.
Access requires one serialized writer and an existing private scratch directory.

Transfer trusted caller matching, fixed owner/service roles, version-4 selections,
exact counters, persistence-before-install and owned publication are unchanged.
Other transfer/preparation/codec guards retain prior evidence. Allocation tracking
excludes direct C allocation, other threads and private external allocators.
No full suites, expensive gates or upstream baseline tests ran. M1/TR evidence,
independent networking, migration base, broad headless dependencies and baseline
provenance debt are unchanged.
