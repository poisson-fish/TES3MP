# Current state and next action

## Handoff

- **Direction:** OpenMW-backed authoritative cooperative multiplayer; see
  [README.md](README.md) and [DECISIONS.md](DECISIONS.md).
- **Milestone:** M2 in [PLAN.md](PLAN.md). Test-owned plain-shirt equipment now
  covers allocation failures during restart/recovery continuation and its existing
  capacity/generation boundaries. Production integration remains deferred.
- **Next action:** add an owned-ID equip/unequip command boundary for the existing
  test-owned actor stores: match a separately trusted caller, resolve the current
  actor/item and expected registry revision, then use the existing preparation and
  durable commit path. Keep borrowed engine views internal and equipment format 1.
- **Session scope:** complete 2–3 closely related bounded slices sequentially;
  one slice at a time limits concurrent scope, not slices per session. After
  review and commit, summarize results/commit and provide a ready-to-paste next
  session prompt with workspace, reading, concrete scope, verification and commit
  requirements. Carry these session-scope and final-response instructions forward.
- **Checkpoint:** 8850e745c298c6de629ef9a5a26bbdddf6aa56e3 preserves the working
  gameplay implementation before the engine-backed pivot.

## Implemented M2 slices

[PlainEquipmentFixture](../../apps/tes3mp-server/native/equipment_tests.cpp)
exhaustively injects C++ allocation failures into equip/unequip preparation and
`commitEquipment` after restart, separately for the restored and other actor.
The reused harness also runs after a subsequent uncertain commit is recovered
into another explicitly fresh fixture, covering complete prior/new file outcomes.
The old fixture and sink are destroyed before recovery; no committed or uncertain
effects replay. Other uncertain I/O boundaries reuse existing recovery evidence.

Every injected failure preserves canonical nodes, lifetimes, semantic values,
raw membership, slots, selections, registry counters/mappings, listeners, services
and effects. Existing proposal/result/byte storage and values remain unchanged;
both actor files remain exact, no temporary file survives, and tracked allocations
return to zero. Commit attempts use fresh sinks. Retry uses the independent
transition/effect ledger and checks installed/exported/decoded durable values.
The next allocation is armed to fail through the successful commit; installation,
publication and retirement allocate nothing after durable acceptance.

Boundary tests preserve the existing supported behavior. A restarted 64-node
inventory can equip with a split and durably save/restart all 65 nodes. Further
preparation, including unequip, visibly rejects at 65 without pruning dormant
membership or changing state/output/files. No-split equip/unequip continues at 64.
The final generation can be consumed exactly once; unequip/restack still succeeds,
then equip requiring another split rejects. Single-item equip/unequip still works
with the generation counter exhausted. Each boundary covers both actor identities,
fresh restart and complete prior/new uncertain recovery. The unrelated actor
continues independently with operations requiring no new identity.

## Fresh verification

Windows MSVC, scripts/setup_msvc_env.ps1 -PreferLatest, RelWithDebInfo,
build/vnext-product, 2026-09-16. All three incremental builds of
`tes3mp_native_loadout_tests` exited **0**; final log:
build/logs/native-equipment-boundary-build.log. Each filter ran individually,
exit **0**. Logs are `build/logs/native-equipment-<suffix>.log`.

| Filter suffix after inventory-equipment- | Evidence |
|---|---|
| restart-continuation-allocations | 2,558 failures, zero tracked leftovers, 8 verified retries |
| restart-continuation-recovery-allocations | 11,096 failures, 8 fresh recoveries, 32 verified commits |
| restart-continuation-boundaries | 24 fresh restarts, 16 prior/new recoveries, 18 visible rejections, 106 commits |
| restart-continuation | 40 isolated commits |
| restart-continuation-guards | 64 rejection-and-retry cases |
| restart-continuation-persistence | 40 safe retries, 64 uncertain outcomes, 256 blocked actor/sink retries |
| restart-continuation-recovery | 32 subsequent uncertain recoveries, 128 continuation commits |

The four existing filters were rerun after the shared verification changed.
Documentation budget and local links ran individually, exit **0**:
build/logs/docs-budget.log and build/logs/docs-links.log.

## Limits and inherited evidence

Only test coverage and this handoff changed. Earlier restart/codec/file/commit
proofs are inherited. Equipment format 1 retains its 80 MiB bound and detached
restoration. It saves generation, not registry revision; restart advances the
fresh fixture's revision once and consumes its restart authorization. A 65-node
save is restorable but outside the 64-node preparation scope.

Production integration, scripts, enchantments, other slots/types, complete
two-player world saves and durable request/notification deduplication remain
deferred. Content/WorldModel must outlive validation; one serialized writer and
private scratch storage remain required. Runtime scenes/caches are not saved;
postponed physics rejects. Windows write-through replacement is not a separate
directory-fsync or power-loss proof. Allocation tracking excludes direct C
allocation, other threads and private external allocators.

Server authority, one canonical writer, engine-independent networking, trusted
caller matching, fixed transfer owner/service roles, transfer format-4 selections,
durability-before-install and owned publication are unchanged. Borrowed pointers
and iterators stay internal. No complete suites, expensive gates or upstream
baseline tests ran. Earlier M1/TR and networking evidence, the migration base,
broad headless dependencies and baseline provenance debt are inherited unchanged.
