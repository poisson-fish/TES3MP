# Current state and next action

## Handoff

- **Direction:** OpenMW-backed authoritative cooperative multiplayer; see
  [README.md](README.md) and [DECISIONS.md](DECISIONS.md).
- **Milestone:** M2 in [PLAN.md](PLAN.md). Test-owned plain-shirt equipment now
  accepts owned-ID commands through restart and uncertain-commit recovery.
  Production integration remains deferred.
- **Next action:** add an app-local owned equipment snapshot boundary for these
  test-owned actors: trusted actor matching, complete signed/dormant item counts,
  shirt/selection IDs and current registry revision, with atomic publication and
  no borrowed engine values. Use it to obtain fresh command inputs after restack
  and restart; registry revision is not a per-operation sequence or dedup token.
- **Session scope:** complete 2–3 closely related bounded slices sequentially;
  one slice at a time limits concurrent scope, not slices per session. After
  review and commit, summarize results/commit and provide a ready-to-paste next
  session prompt with workspace, reading, concrete scope, verification and commit
  requirements. Carry these session-scope and final-response instructions forward.
- **Checkpoint:** 8850e745c298c6de629ef9a5a26bbdddf6aa56e3 preserves the working
  gameplay implementation before the engine-backed pivot.

## Implemented M2 slices

[EquipmentCommand and EquipmentSuccess](../../apps/tes3mp-server/native/equipment_command.hpp)
contain only owned IDs, requested state and counters. The test-only executor in
[PlainEquipmentFixture](../../apps/tes3mp-server/native/equipment_tests.cpp)
matches a separately trusted caller, resolves current actor/item identities and
registry revision, then uses existing engine preparation and commitEquipment.
Success storage is staged before persistence and swapped after durable
installation. Borrowed references remain internal; no new canonical writer,
networking dependency or save format was introduced.

Focused guards reject caller/actor mismatches, unset/missing/expired/remapped
identities, stale/future revisions, dormant items, invalid/already-requested
states and wrong save bindings. Rejection preserves canonical nodes/lifetimes,
values, raw membership, slots/selections, registry counters/mappings, services,
listeners/effects, output allocation/value, exact actor files and temporary-file
absence. Valid operations cover both actors and retained dormant fixture anchors.

The reused independent transition/effect ledger checks command continuation,
installed/exported/decoded values and actor isolation after restart. Safe I/O
failures permit retry; uncertain outcomes block both actors and fresh sinks
without allocation. Subsequent recovery destroys the old fixture and sink before
installing a complete prior/new file into an explicitly fresh fixture. Neither
committed nor uncertain effects replay. Allocation sweeps cover the entire command,
including preparation and owned success staging, with zero tracked leftovers.
Successful retries arm the next allocation to fail; installation, publication and
retirement allocate nothing after acceptance.

Existing semantics remain: 64-node preparation may split and save/restart 65
nodes; further preparation at 65 rejects without pruning dormant membership.
No-split operations work at 64 and with exhausted generation. The final generated
identity is consumed once, unequip/restack still works, and further splits reject.
Stock revision and negative-file generation rollover remain supported.

## Fresh verification

Windows MSVC, scripts/setup_msvc_env.ps1 -PreferLatest, RelWithDebInfo,
build/vnext-product, 2026-09-16. Final tes3mp_native_loadout_tests build exited **0**:
build/logs/native-equipment-command-final-build.log. Each filter below ran
individually, exit **0**; logs are build/logs/native-equipment-<suffix>.log.

| Filter suffix after inventory-equipment- | Evidence |
|---|---|
| command | 8 isolated commits; owned success survives fixture destruction |
| command-guards | 96 atomic rejections |
| command-boundaries | 24 restarts, 16 prior/new recoveries, 18 rejections, 106 commits |
| command-restart | 40 isolated commits, including counter rollover |
| command-persistence | 40 safe retries, 64 uncertain outcomes, 256 blocked retries |
| command-allocations | 2,566 injected failures, zero leftovers, 8 verified retries |
| command-recovery | 32 subsequent fresh recoveries, 128 continuation commits |
| command-recovery-allocations | 11,128 failures, 8 fresh recoveries, 32 commits |

All seven affected existing restart-continuation filters were rerun individually,
exit **0**: the base filter and suffixes -guards, -persistence, -recovery,
-allocations, -recovery-allocations and -boundaries. Documentation budget and
local links ran individually, exit **0**: build/logs/docs-budget.log and
docs-links.log. No complete suites, expensive gates or upstream baseline tests ran.

## Limits and inherited evidence

Equipment format 1 remains bounded to 80 MiB and saves generation, not registry
revision. Restart advances the fresh fixture's revision once and consumes its
restart authorization. Commands do not provide durable request deduplication;
revision need not change on unequip or no-split equip. A 65-node save is restorable
but outside preparation scope.

Production integration, scripts, enchantments, other slots/types, complete
two-player world saves and durable request/notification deduplication remain
deferred. Content/WorldModel lifetime, one serialized writer and private scratch
storage remain required. Runtime scenes/caches are not saved; postponed physics
rejects. Windows write-through replacement is not a separate directory-fsync or
power-loss proof. Allocation tracking excludes direct C allocation, other threads
and private external allocators.

Server authority, engine-independent networking, fixed transfer owner/service
roles, transfer format-4 selections and durability-before-install remain unchanged.
Earlier codec/file/restart proofs, M1/TR/networking evidence, the migration base,
broad headless dependencies and baseline provenance debt are inherited unchanged.
