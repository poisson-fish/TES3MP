# Current state and next action

## Handoff

- **Direction:** OpenMW-backed authoritative cooperative multiplayer; see
  [README.md](README.md) and [DECISIONS.md](DECISIONS.md).
- **Milestone:** M2 in [PLAN.md](PLAN.md). Plain-shirt equipment now has
  actor-local durable commits and fresh-fixture restart installation in
  test-owned stores. Production integration remains deferred.
- **Next action:** prove post-restart equip/unequip continuation through
  `PlainEquipmentFixture::commitEquipment` with a fresh `EquipmentFileSink`:
  preserve the other actor, exact counters and dormant selections, then recover
  a subsequent uncertain commit into another fresh fixture. Keep equipment
  format 1 and existing plain-shirt scope.
- **Session scope:** complete 2–3 closely related bounded slices sequentially;
  one slice at a time limits concurrent scope, not slices per session. After
  review and commit, summarize results/commit and provide a ready-to-paste next
  session prompt with workspace, reading, concrete scope, verification and commit
  requirements. Carry these session-scope and final-response instructions forward.
- **Checkpoint:** 8850e745c298c6de629ef9a5a26bbdddf6aa56e3 preserves the working
  gameplay implementation before the engine-backed pivot.

## Implemented M2 slices

[PlainEquipmentFixture](../../apps/tes3mp-server/native/equipment_tests.cpp)
requires an explicitly constructed fresh restart target with empty inventory.
It validates trusted caller/runtime/content/actor bindings, both store lifetimes
and storage identities, script-service lifetime, current registry mappings and
reference lifetimes, supported listeners/slots, and saved generation coverage
for every retained identity. Ordinary, consumed and uncertain fixtures reject
restart. Incoming identities cannot collide with retained mappings.

[RestoredPlainEquipment](../../apps/openmw/mwworld/plainequipment.hpp)
retains its constructing content binding and captures node lifetime witnesses
before publication. Private fixture access validates exact actor/counter/content
and current base-record identity. Staging preserves signed counts, raw dormant
membership, shirt and selection, and prepares a bounded replacement registry.
Rejection preserves detached input and prior live/output state.

[Equipment file restart](../../apps/tes3mp-server/native/equipment_file.cpp)
can return the exact bytes from its single bounded read with the complete
detached owner. Fixture composition finishes reading, decoding, validation,
allocation, byte-for-byte re-encoding and relocation before a nonthrowing
installation/publication block. It publishes owned semantic values and bytes;
borrowed pointers and iterators remain internal. It never replays equip,
removal, inventory-notification or script effects.

Installation preserves the unrelated actor's nodes, values, lifetimes, services,
listeners and registry mappings. It restores the saved generation counter
exactly, including rollover/exhaustion and counters beyond surviving IDs.
Equipment format 1 has no persisted registry revision: the fresh registry's
revision advances once, including unsigned rollover. Installation invalidates
old storage bindings and consumes fresh restart authorization.

Existing durable commits still finish fallible work before file acceptance.
Safe rejection preserves state/output/file and permits retry. Uncertain commits
install/publish nothing and permanently block the whole old fixture, including
another actor, a new sink and attempted restart. Fresh recovery now installs
complete prior/new files from all tested uncertain boundaries without effects.

## Fresh verification

Windows MSVC, scripts/setup_msvc_env.ps1 -PreferLatest, RelWithDebInfo,
build/vnext-product, 2026-09-16. Target tes3mp_native_loadout_tests and each
individual filter below exited **0**. No test/build failures occurred.
Logs are under build/logs; build: native-equipment-restart-final-build.log.

| Filter suffix after inventory-equipment- | Evidence | Log suffix after native-equipment- |
|---|---|---|
| restart-staging | 2 isolated actor preparations | restart-staging.log |
| restart | 14 exact installs, consumed authorization | restart.log |
| restart-guards | 94 stale/binding/lifetime/bound rejections | restart-guards.log |
| restart-read | 24 read/decode failures and safe retries | restart-read.log |
| restart-allocations | 1,046 individually injected failures | restart-allocations.log |
| commit-persistence | 20 safe failures, 32 uncertain fresh recoveries | restart-persistence.log |
| value-allocations | 272 restoration/export regression failures | restart-value-allocations.log |
| file-allocations | 1,848 file regression failures | restart-file-allocations.log |
| commit-allocations | 927 commit regression failures | restart-commit-allocations.log |

Installation/publication/retirement allocated nothing; tracked cleanup was
leak-free. Success includes empty, dormant-only and maximum-node inventories,
signed counts, selections, destroyed source fixtures and exact semantic fields.
Documentation budget and local links ran individually, exit **0**:
docs-budget.log and docs-links.log.

## Limits and inherited evidence

Post-restart command continuation, production integration, scripts, enchantments,
other slots/types and durable request/notification deduplication remain deferred.
This is actor-local equipment recovery, not a complete two-player world save.
Equipment format 1, the 80 MiB bound and strict detached restoration remain.
Content/WorldModel must outlive validation; access requires one serialized writer
and private scratch storage. Runtime scenes/caches are not saved values;
postponed physics rejects. Windows write-through replacement is not a separate
directory-fsync or power-loss proof.

Transfer trusted caller matching, fixed owner/service roles, format-4 selections,
durability-before-install and owned publication are unchanged. Allocation tracking
excludes direct C allocation, other threads and private external allocators.
No full suites, expensive gates or upstream baseline tests ran. Earlier commit,
codec, M1/TR and independent-networking evidence, the migration base, broad
headless dependencies and baseline provenance debt are inherited unchanged.
