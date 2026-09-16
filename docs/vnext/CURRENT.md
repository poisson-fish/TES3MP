# Current state and next action

## Handoff

- **Direction:** OpenMW-backed authoritative cooperative multiplayer; see
  [README.md](README.md) and [DECISIONS.md](DECISIONS.md).
- **Milestone:** M2 in [PLAN.md](PLAN.md). Test-only non-gold MISC commands now
  transfer in both directions between the same installed owners, deliver owned
  results to independent views, recover delivery through snapshots and continue
  after fresh version-4 restarts. Production durability/live atomic transfer remain
  unproven.
- **Next action:** let two explicitly authorized initiators alternate forward and
  return commands in the same installed test fixture. Separate trusted per-command
  initiator binding from the fixed save-envelope initiator; preserve version-4
  encoding and stable owner/service roles. Resolve stock contexts from the trusted
  caller, reject caller/command mismatches, then prove alternating initiation,
  delivery recovery and fresh-restart continuation. Keep production integration,
  script execution and durable request/notification deduplication deferred.
- **Checkpoint:** `8850e745c298c6de629ef9a5a26bbdddf6aa56e3` preserves the working
  gameplay implementation before the engine-backed pivot.

## Implemented M2 slices

[Command composition](../../apps/tes3mp-server/native/inventory_transfer_command.cpp)
resolves either direction afresh through stock preparation and validation.
The [disposable fixture](../../apps/tes3mp-server/native/transfer_rehearsal.cpp)
constructs operation contexts from its existing owned stores/services, preserving
its authorized initiator. Reverse serialization and installation map inventories,
selections and distinct script-service lists/cursors back to stable save roles;
service 0 still belongs to the original source and unrelated store. Shared services
remain shared. Exact saved revision/generated counters are never reconstructed.
Ownership, storage, lifetime, registry and iterator guards remain mandatory.

All owned success/notification storage prepares before persistence. Acceptance
precedes installation, retirement and publication, which allocate nothing.
[View delivery](../../apps/tes3mp-server/native/inventory_view.cpp) finds each
independent view by owner ID; command direction determines committed counts and
selections, not its storage slot. It performs no gameplay or quantity arithmetic.
The one-shot consumer detaches success before callbacks. A receiver exception
reports `FailedAfterCommit` and its confirmed prefix, discards the suffix and
cannot replay the batch. Recovery snapshots prepare both buffers before publishing
either; failed recovery preserves prior views/storage and never retries gameplay.

[Focused tests](../../apps/tes3mp-server/native/transfer_rehearsal_tests.cpp) cover
outward/return transfers before restart, return commands after fresh installation,
and subsequent opposite-direction continuation after another restart. A further
fresh installation recovers partially delivered continuation views. Independent
installed/save projections check stable roles, script metadata and exact bytes.
Coverage includes both initiators, partial/full removal, stacking, signed counts,
scripted items, shared/distinct services, four listener combinations and
unset/retained/dormant selections. Both initiators are exercised in separate cases;
a fixture's authorized initiator remains fixed. Restart emits no notifications;
script-run counts remain zero. Owned values survive fixture destruction.

## Fresh verification

Windows MSVC via `scripts/setup_msvc_env.ps1 -PreferLatest`, RelWithDebInfo,
`build/vnext-product`, 2026-09-15. Target `tes3mp_native_loadout_tests` and the five
filters below ran individually on the reviewed build; all final exits **0**.
Log prefix: `build/logs/native-return-`.

| Filter | Evidence | Log suffix |
|---|---|---|
| `inventory-transfer-command` | 48 cases/returns; 1,599 allocation failures; 96 view resynchronizations | `command-final.log` |
| `inventory-transfer-return-restart` | 48 cases; 288 returns; 2,717 allocation failures; 192 uncertainty recoveries; 258 receiver allocation failures | `restart-final.log` |
| `inventory-transfer-return-selections` | 32 cases; 192 returns; 1,406 allocation failures; 128 uncertainty recoveries | `selections-final.log` |
| `inventory-transfer-restart-command` | 48 forward cases; 2,783 allocation failures; 192 uncertainty recoveries | `forward-restart.log` |
| `inventory-transfer-selections` | 32 forward cases; 1,406 allocation failures; 128 uncertainty recoveries | `forward-selections.log` |

Build log: `reviewed-build.log`. Reverse delivery covers all twelve intent failure
boundaries plus both actual view-buffer allocations. Measured command cleanup
leaves **0** tracked blocks; delivery retains only expected owned view buffers.
No complete suites, expensive gates or upstream baseline tests ran.
Documentation budget and local-link checks ran individually, exit **0**;
logs use suffixes `docs-budget.log` and `docs-links.log`.

## Remaining limits and inherited evidence

Commands, snapshots and delivery remain test-only, serialized and ephemeral.
The version-4 codec is unchanged. One supplied script/declaration set and non-gold
MISC are supported; views expose IDs/counts/selections, not full presentation state.
Borrowed content/input lifetimes require serialized discipline. Other-store
metadata validates identity/base/configured state, not complete saved values.

Unchanged snapshot/codec/restore/registry/script/installation matrices remain
inherited evidence. File tests use synthetic single-writer Windows I/O, not
crash/power-loss or production durability. POSIX/32-bit bounds, equipment,
gold/other types, Lua/custom state, content deletion and postponed physics remain
outside this slice. Allocation tracking excludes direct C allocation, other
threads and private external allocators. M1 real-content/enchantment evidence was
not rerun; TR remains unverified. Networking and the migration base are unchanged.
Broad headless dependencies and baseline provenance debt remain.
