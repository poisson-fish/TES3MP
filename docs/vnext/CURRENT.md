# Current state and next action

## Handoff

- **Direction:** OpenMW-backed authoritative cooperative multiplayer; see
  [README.md](README.md) and [DECISIONS.md](DECISIONS.md).
- **Milestone:** M2 in [PLAN.md](PLAN.md). The test-only non-gold MISC transfer
  now drives two owned inventory views through committed notifications, snapshot
  resynchronization, fresh version-4 restart and subsequent commands. Production
  durability and live atomic transfer remain unproven.
- **Next action:** extend the test-only command composition to transfer in either
  direction between the same two installed owners while preserving stable save
  owner roles. Reuse stock preparation/validation and persistence-before-install;
  prove a return transfer after partial/full removal, view delivery failure and
  fresh restart, including scripted items and retained selections. Keep production
  integration, script execution and durable request/notification deduplication
  deferred.
- **Checkpoint:** `8850e745c298c6de629ef9a5a26bbdddf6aa56e3` preserves the working
  gameplay implementation before the engine-backed pivot.

## Implemented M2 slice

The [inventory view snapshot](../../apps/tes3mp-server/native/inventory_view.cpp)
projects current fixture-owned storage into two independent owned owner views.
Each carries owner/item IDs, signed counts, selection identity (including dormant
nodes) and the exact registry revision. Validation bounds each store to 1,024
non-gold MISC nodes and checks current owner, registry, lifetime, membership and
selection bindings before allocating. Both view buffers prepare completely before
noexcept swap publication; failure preserves prior values and storage. No borrowed
engine pointer or iterator escapes. Snapshot values survive fixture destruction.

[Committed transfer results](../../apps/tes3mp-server/native/inventory_transfer_command.hpp)
now also carry stock-prepared selection IDs. The test-only view consumer copies
owned success values and applies final counts/selections on inventory-updated
intents, with an exact prior-revision check and independent owner publication.
It performs no gameplay or quantity arithmetic. Existing remove/add notification
ordering and explicit initiator routing remain intact. All success/notification
storage is prepared before persistence; installation, retirement and publication
allocate nothing. Production callers and the version-4 codec are unchanged.

The one-shot consumer still detaches success before delivery. Receiver failure
returns `FailedAfterCommit` with the committed revision and confirmed prefix; the
failing intent may already have arrived. The suffix is discarded, and the batch
cannot be retried. Recovery takes a new authoritative snapshot; failure allocating
either recovery buffer preserves both previous views. It never replays a command,
rolls back commitment or treats delivery failure as command rejection.

The [focused tests](../../apps/tes3mp-server/native/transfer_rehearsal_tests.cpp)
compare views against independently projected accepted save values. They cover
both initiators, partial/full removal, stacking, signed counts, scripted items,
shared/distinct services, four listener combinations and retained/unset/dormant
selections. Delivery faults cover all twelve intent boundaries plus both actual
view-buffer allocations. Tests retain views/results beyond fixture destruction,
install fresh version-4 saves, continue commands, then recover partially delivered
views through another fresh installation. Restart emits no notifications. Exact
saved counters, unrelated state, listeners and script-run counts remain checked.

## Fresh verification

Windows MSVC 14.51 (`scripts/setup_msvc_env.ps1 -PreferLatest`), RelWithDebInfo,
`build/vnext-product`, 2026-09-15. Target `tes3mp_native_loadout_tests` and these
filters ran individually; final exits **0**:

| Filter | Evidence |
|---|---|
| `inventory-view-snapshot` | 32 populated + 32 empty cases; 64 allocation failures; validation/publication allocate 0 |
| `inventory-transfer-command` | 48 cases; 1,599 command allocation failures; 48 view resynchronizations; 6 actual receiver allocation failures |
| `inventory-transfer-restart-command` | 48 cases; 2,783 command allocation failures; 192 uncertainty recoveries; 288 view resynchronizations; 138 actual receiver allocation failures |
| `inventory-transfer-selections` | 32 cases; 1,406 command allocation failures; 128 uncertainty recoveries |

Logs: `build/logs/native-view-` with suffixes `snapshot-build.log`, `snapshot.log`,
`command-build.log`, `command.log`, `restart-build.log`, `restart-command.log`,
`selections.log`, `reviewed-build.log`, `docs-budget.log` and `docs-links.log`.
The initial build exited 2 for two new interface mistakes; both were fixed before
its successful rerun. Measured snapshot/command cleanup leaves **0** tracked blocks;
delivery retains only the expected owned view buffers. Documentation budget and
local-link checks ran individually, exit **0**. No complete suites, expensive gates
or upstream baseline tests ran.

## Remaining limits and inherited evidence

Snapshots/delivery are test-only, serialized and ephemeral. Command source and
destination roles remain fixed. Views expose IDs/counts/selections, not complete
client presentation state. One supplied script/declaration set and non-gold MISC
remain supported; script instructions are not executed. Borrowed content/input
lifetimes require serialized discipline. Other-store metadata validates
identity/base/configured state, not complete saved values.

Unchanged codec/restore/registry/script/installation matrices remain inherited
evidence. File evidence is synthetic single-writer Windows I/O, not crash/power-loss
or production durability. POSIX/32-bit bounds, equipment, gold/other types,
Lua/custom state, content deletion and postponed physics remain outside this slice.
Allocation tracking excludes direct C allocation, other threads and private
external allocators. M1 real-content/enchantment evidence was not rerun; TR remains
unverified. Networking and the migration base are unchanged. Broad headless
dependencies and baseline provenance debt remain.
