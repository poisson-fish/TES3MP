# Current state and next action

## Handoff

- **Direction:** OpenMW-backed authoritative cooperative multiplayer; see
  [README.md](README.md) and [DECISIONS.md](DECISIONS.md).
- **Milestone:** M2 in [PLAN.md](PLAN.md). Plain-shirt equipment now has
  actor-local preparation and a durable commit boundary in test-owned stores.
  Production integration remains deferred.
- **Next action:** stage actor-local restart installation from equipment format 1
  through [RestoredPlainEquipment](../../apps/openmw/mwworld/plainequipment.hpp)
  into a fresh test-owned fixture. Validate explicit runtime/content/actor,
  lifetime, registry and saved generation-counter bindings before relocating
  dormant membership, shirt and selection; install without replaying operation
  effects or changing the unrelated actor. Keep the existing file format.
- **Session scope:** complete 2–3 closely related bounded slices sequentially;
  one slice at a time limits concurrent scope, not slices per session. After
  review and commit, summarize results/commit and provide a ready-to-paste next
  session prompt with workspace, reading, concrete scope, verification and commit
  requirements. Carry these session-scope and final-response instructions forward.
- **Checkpoint:** 8850e745c298c6de629ef9a5a26bbdddf6aa56e3 preserves the working
  gameplay implementation before the engine-backed pivot.

## Implemented M2 slices

[PlainEquipmentFixture](../../apps/tes3mp-server/native/equipment_tests.cpp)
alone accesses a private, revalidated candidate from PreparedPlainEquipment.
It matches the trusted caller to the actor and exact receiving store, checks
reference/store/script-service lifetimes and current registry/listener witnesses,
and stages registry entries, slot/selection iterators, effects and owned results.
Registry preparation is bounded to 132 entries; each actor's owned removal and
inventory-notification queues are bounded to 64 entries. Saturation rejects.

Stock shared equip/unequip and split/restack produce the candidate and effects.
Split registration preserves exact generation and revision counters, including
rollover. Test-owned listeners stage actual callbacks against isolated owned
payload storage; inventory updates retain the actor identity. Plain restack
cleanup requires no script registration. Foreign or absent effect listeners,
unknown effect kinds, scripts and enchantments reject visibly before persistence.
This proves the supported synthetic consumers, not production mechanics/UI.

The fixture composes [EquipmentFileSink](../../apps/tes3mp-server/native/equipment_file.cpp)
with fixed synthetic runtime/content bindings and per-call actor matching.
All fallible work precedes file acceptance. A nonthrowing block then exchanges
stock nodes and registry storage, installs shirt/selection and prepared effects,
publishes owned results/bytes, and detaches retired nodes from the registry.
No borrowed pointer or iterator is serialized or published. The other actor's
nodes, values, lifetimes, services, effects and registry mappings stay intact.

Safe persistence rejection preserves prior live state, output values/storage
and file bytes and permits retry. Uncertain replacement installs nothing and
publishes no success/effects; it permanently blocks both actors in that fixture,
including retries through a fresh sink. Reopening and detached restoration prove
a complete prior/new file. Recovery installation is the next slice.

## Fresh verification

Windows MSVC, scripts/setup_msvc_env.ps1 -PreferLatest, RelWithDebInfo,
build/vnext-product, 2026-09-16. Target tes3mp_native_loadout_tests and every
individual filter below exited **0**. Logs are under build/logs.

| Filter suffix after inventory-equipment- | Evidence | Log |
|---|---|---|
| installation | 4 isolated preparations | native-equipment-installation-final.log |
| commit | 18 two-actor cases, exact saved/installed values | native-equipment-commit-final.log |
| commit-guards | 62 stale/binding/lifetime/effect/bound rejections | native-equipment-commit-guards-final.log |
| commit-persistence | 20 safe failures, 32 uncertain outcomes | native-equipment-commit-persistence-final.log |
| commit-allocations | 927 injected failures, leak-free cleanup | native-equipment-commit-allocations-final.log |
| preparation | stock split/restack and effects | native-equipment-preparation-regression.log |
| guards | 82 rejections | native-equipment-guards-regression.log |
| allocations | 186 injected failures | native-equipment-allocations-regression.log |

Final build: native-equipment-commit-final-build.log. Initial build exited 2
(missing iterator initialization); initial commit filter exited 1 (duplicate
fixture record). Both were fixed and their checks rerun successfully before
continuing. Final tests include maximum nodes, signed/dormant membership,
selection, replacement shirts, exhausted/rolling counters, and zero allocations
during installation/publication/retirement.

Documentation budget and local links ran individually, exit **0**:
docs-budget.log and docs-links.log.

## Limits and inherited evidence

Restart installation, production integration, scripts, enchantments, other
slots/types and durable request/notification deduplication remain deferred.
Equipment format 1, the 80 MiB file bound, strict detached restart and existing
file/codec coverage are unchanged. Runtime scenes/caches are not saved values;
postponed physics remains rejected. Content/WorldModel outlive validation;
access requires one serialized writer and a private scratch directory.
Windows write-through replacement is not a separate directory-fsync or
power-loss proof.

Transfer trusted caller matching, fixed owner/service roles, format-4 selections,
exact counters, persistence-before-install and owned publication are unchanged.
Allocation tracking excludes direct C allocation, other threads and private
external allocators. No full suites, expensive gates or upstream baseline tests
ran. M1/TR evidence, independent networking, migration base, broad headless
dependencies and baseline provenance debt are unchanged.
