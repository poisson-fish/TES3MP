# Current state and next action

## Handoff

- **Direction:** OpenMW-backed authoritative cooperative multiplayer; see
  [README.md](README.md) and [DECISIONS.md](DECISIONS.md).
- **Milestone:** M2 in [PLAN.md](PLAN.md). Serialized same-item contention between
  two explicitly authorized callers is now proven in the installed transfer
  fixture, including delivery recovery and fresh version-4 continuation.
  Production durability/live atomic transfer remain unproven.
- **Next action:** prove opposing-direction contention: construct two authorized
  commands with different live source item IDs and unequal quantities against
  one revision, one in each direction. Exercise both arrival orders, preserve
  the stale loser's independent output, then resynchronize and newly issue its
  still-valid intended transfer. Carry the sequence through failed delivery,
  fresh version-4 restart and relevant allocation/persistence failures without
  changing fixed owner/service roles or replaying committed gameplay.
- **Session scope:** complete 2–3 closely related bounded slices sequentially;
  one slice at a time limits concurrent scope, not slices per session. After
  review and commit, summarize results/commit and provide a ready-to-paste next
  session prompt with workspace, reading, scope, verification and commit
  requirements. Carry these session-scope and final-response instructions forward.
- **Checkpoint:** `8850e745c298c6de629ef9a5a26bbdddf6aa56e3` preserves the working
  gameplay implementation before the engine-backed pivot.

## Implemented M2 slices

The [focused tests](../../apps/tes3mp-server/native/transfer_rehearsal_tests.cpp)
construct both trusted caller/command pairs before arrival. Independent stock
contexts establish each candidate's viability and initiator-sensitive `OnPCAdd`
result at the same revision. Both caller orders start from identical accepted
bytes. One winner commits; the loser rejects before preparation or I/O, retaining
its output allocation/value, both owner views and exact installed state. Rejection
is also checked with allocation failure armed, including after fixture destruction
and fresh installation. This is serialized contention, not concurrent engine access.

Winner delivery exercises all twelve notification failure boundaries and both
actual owner-view allocations. The one-shot batch is consumed even on
`FailedAfterCommit`. Independent views retain their confirmed updates, then recover
from authoritative snapshots in the same fixture or after fresh restart. Both
snapshot allocation failures preserve prior values/storage. The loser then issues
a new current-revision intent and receives an independent committed result;
recovery never replays the winner's gameplay.

Safe file failures and exhaustive allocation injection on representative plain
and scripted contenders preserve both pending intents and outputs. The other
already-issued intent remains eligible to win. Uncertain persistence blocks both
callers through old and fresh sinks without allocation or I/O; only a discarded
fixture and validated fresh composition can continue. Existing uncertainty
recovery distinguishes coherent prior/new bytes from failed delivery after
commitment. Script-run counts remain zero; restart emits no notifications.

The [command boundary](../../apps/tes3mp-server/native/inventory_transfer_command.cpp),
[fixture](../../apps/tes3mp-server/native/transfer_rehearsal.cpp) and
[view consumer](../../apps/tes3mp-server/native/inventory_view.cpp) required no
behavior changes. Trusted caller matching, the fixed save-envelope initiator,
owner/service roles, exact saved revision/generated counters and version-4
selections remain intact. Ownership, storage, lifetime, registry and iterator
guards remain mandatory. Owned success/notification data prepares before
persistence; installation, retirement and publication allocate nothing. No borrowed
pointers or iterators enter saves or published values.

## Fresh verification

Windows MSVC through `scripts/setup_msvc_env.ps1 -PreferLatest`, RelWithDebInfo,
`build/vnext-product`, 2026-09-15. Target `tes3mp_native_loadout_tests` and each
filter below exited **0**. Log prefix: `build/logs/native-contention-`.

| Filter | Contention winners / stale checks / allocation failures | Log suffix |
|---|---|---|
| `inventory-transfer-return-restart` | 192 / 576 / 5,434 | `return-restart.log` |
| `inventory-transfer-restart-command` | 192 / 576 / 5,566 | `forward-restart.log` |
| `inventory-transfer-return-selections` | 128 / 384 / 2,812 | `return-selections.log` |
| `inventory-transfer-selections` | 128 / 384 / 2,812 | `forward-selections.log` |

New contention evidence totals 640 winners, 320 fresh restarts, 1,600 safe file
failures, 584 failed deliveries and 80 owner-view allocation failures. Matrices
cover partial/full removal, stacking, signed counts, scripted items, shared/distinct
services and unset/retained/dormant selections. Existing command failure,
alternation and listener-shape coverage also passes in these filters. Measured
rejection cleanup retains zero tracked blocks; delivery retains only expected view
buffers. Build: `reviewed-build.log`. Documentation budget and local-link checks
ran individually, exit **0**: `docs-budget.log`, `docs-links.log`.

## Remaining limits and inherited evidence

Commands, authorization, snapshots and delivery remain test-only, serialized and
ephemeral. Production integration, script execution and durable request/notification
deduplication remain deferred. One supplied script/declaration set and non-gold
MISC are supported. Views expose IDs/counts/selections, not full presentation state.
Borrowed inputs require serialized lifetimes. Other-store metadata validates
identity/base/configuration, not complete saved values.

Unchanged snapshot/codec/restore/registry/script/installation matrices and the
initial command filter remain inherited evidence. No complete suites, expensive
gates or upstream baseline tests ran. File tests use synthetic single-writer
Windows I/O, not crash/power-loss or production durability. POSIX/32-bit bounds,
equipment, gold/other types, Lua/custom state, content deletion and postponed
physics remain outside this slice. Allocation tracking excludes direct C allocation,
other threads and private external allocators. M1 real-content/enchantment evidence
was not rerun; TR remains unverified. Networking and the migration base are
unchanged. Broad headless dependencies and baseline provenance debt remain.
