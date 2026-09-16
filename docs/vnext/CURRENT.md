# Current state and next action

## Handoff

- **Direction:** OpenMW-backed authoritative cooperative multiplayer; see
  [README.md](README.md) and [DECISIONS.md](DECISIONS.md).
- **Milestone:** M2 in [PLAN.md](PLAN.md). Plain-shirt equipment has actor-local
  durable commits, fresh-fixture restart, post-restart continuation and subsequent
  uncertain-commit recovery in test-owned stores. Production integration is deferred.
- **Next action:** inject allocation failures through post-restart equip/unequip
  preparation and `PlainEquipmentFixture::commitEquipment` with a fresh
  `EquipmentFileSink`, proving unchanged state/output/file, cleanup and safe retry
  for the restored actor and the other actor separately. Keep equipment format 1.
- **Session scope:** complete 2–3 closely related bounded slices sequentially;
  one slice at a time limits concurrent scope, not slices per session. After
  review and commit, summarize results/commit and provide a ready-to-paste next
  session prompt with workspace, reading, concrete scope, verification and commit
  requirements. Carry these session-scope and final-response instructions forward.
- **Checkpoint:** 8850e745c298c6de629ef9a5a26bbdddf6aa56e3 preserves the working
  gameplay implementation before the engine-backed pivot.

## Implemented M2 slices

[PlainEquipmentFixture](../../apps/tes3mp-server/native/equipment_tests.cpp)
now proves equip/unequip continuation through its existing durable commit path
with fresh sinks after restart. Tests resolve current active members for each
new operation: a restacked shirt remains a registered dormant node and cannot
stand in for an active equip target. An independent expected transition/effect
ledger verifies signed counts, raw membership, shirt, active/dormant/unset
selection, exact generation and registry counters, including unsigned rollover.
Exported semantic fields match decoded durable bytes and installed values.

The restored actor and the other actor commit separately in the same fixture.
Each operation preserves the other actor's nodes, values, lifetimes, selections,
listeners, effects and registry mappings. Actor files remain separate. Only new
committed operations add removal, inventory-update and equipment-change effects;
restart adds none. Publication remains owned after fixtures are destroyed.

Post-restart guards cover mismatched/expired trusted callers, runtime/content/
actor bindings, stale preparation after same-actor and other-actor commits,
reconstructed script services, foreign/expired store lifetimes, storage identity,
listeners, counts and selection. Each rejection preserves prior state, output
storage/value and file, reaches no persistence/publication, and permits a newly
prepared retry after intentional test mutations are repaired.

Post-restart persistence faults cover creation, write, flush, close, replacement,
barrier and verification reads. Safe failures preserve prior state/output/file
and allow retry. Uncertainty installs/publishes nothing and blocks the entire
fixture through both actors, the used sink and fresh sinks with valid actor
bindings, before allocation or I/O. Uncertain fixtures also reject restart.

After a successful post-restart operation, another uncertain equip/unequip commit
is recovered into a second explicitly fresh fixture after the old fixture and
sink are destroyed. Every tested boundary restores complete prior/new actor-local
values and exact bytes/counters, preserves that fresh fixture's unrelated actor,
and replays no old or uncertain effects. Both actors then continue independently
through fresh sinks. This does not implement a complete two-player world save.

## Fresh verification

Windows MSVC, scripts/setup_msvc_env.ps1 -PreferLatest, RelWithDebInfo,
build/vnext-product, 2026-09-16. Final target tes3mp_native_loadout_tests build
exited **0**; log: build/logs/native-equipment-continuation-build.log.
Each filter below ran individually and exited **0**, including reruns after the
shared assertion changed. Logs are `build/logs/native-equipment-<suffix>.log`.

| Filter suffix after inventory-equipment- | Evidence |
|---|---|
| restart-continuation | 40 isolated commits, selections and counter rollover |
| restart-continuation-guards | 64 rejection-and-retry cases |
| restart-continuation-persistence | 40 safe retries, 64 uncertain outcomes, 256 blocked actor/sink retries |
| restart-continuation-recovery | 32 subsequent uncertain recoveries, 128 continuation commits |

Installation/publication/retirement allocated nothing in observed successful
commits. Failed persistence staging had no tracked leaks. Two test-helper build
errors (exit 2) and one fault-boundary assertion failure (exit 1) were corrected;
each failed check was rerun successfully before proceeding. Documentation budget
and local links ran individually, exit **0**: docs-budget.log and docs-links.log.

## Limits and inherited evidence

Only test coverage and this handoff changed. Existing restart validation,
allocation, codec, file and commit evidence is inherited; post-restart allocation
injection and continuation at capacity/exhaustion boundaries remain unproved.
Scripts, enchantments, other slots/types, production integration and durable
request/notification deduplication remain deferred. Unsupported behavior rejects
visibly. Equipment format 1, its 80 MiB bound and strict detached restoration remain.
Format 1 saves the generation counter, not registry revision; restart advances
the fresh fixture's registry revision once and consumes its restart authorization.

Content/WorldModel must outlive validation; access requires one serialized writer
and private scratch storage. Runtime scenes/caches are not saved values;
postponed physics rejects. Windows write-through replacement is not a separate
directory-fsync or power-loss proof. Allocation tracking excludes direct C
allocation, other threads and private external allocators.

Server authority, engine-independent networking, trusted caller matching, fixed
transfer owner/service roles, transfer format-4 selections, durability-before-install
and owned publication are unchanged. Borrowed pointers/iterators stay internal.
No complete suites, expensive gates or upstream baseline tests ran. Earlier M1/TR
and independent-networking evidence, the migration base, broad headless dependencies
and baseline provenance debt are inherited unchanged.
