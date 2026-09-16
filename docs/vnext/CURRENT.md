# Current state and next action

## Handoff

- **Direction:** OpenMW-backed authoritative cooperative multiplayer; see
  [README.md](README.md) and [DECISIONS.md](DECISIONS.md).
- **Milestone:** M2 in [PLAN.md](PLAN.md). Two explicitly authorized callers now
  alternate non-gold MISC transfers in the same installed test fixture, recover
  notification delivery through snapshots, and continue after fresh version-4
  restarts. Production durability/live atomic transfer remain unproven.
- **Next action:** prove serialized two-caller contention in the installed fixture:
  construct both authorized intents against one revision, exercise both arrival
  orders, require one committed winner and atomic stale rejection of the loser,
  then resynchronize the loser and accept a newly issued intent. Carry that
  sequence through delivery failure and fresh version-4 restart. Complete 2–3
  closely related bounded slices sequentially toward this capability; one slice
  at a time limits concurrent scope, not slices per session. Keep production
  integration, script execution and durable request/notification deduplication
  deferred.
- **Checkpoint:** `8850e745c298c6de629ef9a5a26bbdddf6aa56e3` preserves the working
  gameplay implementation before the engine-backed pivot.

## Implemented M2 slices

[Command composition](../../apps/tes3mp-server/native/inventory_transfer_command.cpp)
accepts an owned trusted caller binding separately from command input. Either fixed
owner can be explicitly authorized by the test composition. Caller/command
mismatches and ineligible identities reject before preparation or I/O. Current
registry and owner lifetime checks precede stock context resolution. The fixed
save-envelope initiator remains independently validated; switching callers cannot
rewrite it. This boundary does not implement authentication.

The [disposable fixture](../../apps/tes3mp-server/native/transfer_rehearsal.cpp)
passes the resolved caller through stock preparation, serialization validation
and commit revalidation without changing stored contexts. Direction maps inventory,
selection and script-service state back to stable save roles. Service 0 still
belongs to the original source and unrelated store; shared services stay shared.
Version-4 encoding and exact saved revision/generated counters are unchanged.
Ownership, storage, lifetime, registry and iterator guards remain mandatory.

All owned success/notification storage prepares before persistence. Acceptance
precedes installation, retirement and publication, which allocate nothing.
Existing [view delivery](../../apps/tes3mp-server/native/inventory_view.cpp)
routes by owner ID and checks the committed initiator. The one-shot consumer
reports receiver exceptions as `FailedAfterCommit`, consumes the batch and never
retries gameplay. Authoritative snapshots stage both view buffers before
publication; failed recovery preserves previous values/storage.

[Focused tests](../../apps/tes3mp-server/native/transfer_rehearsal_tests.cpp)
prove forward/return/forward alternation within one fixture, opposite-caller
continuation after restart, another alternation within a restored fixture, and
fresh installation recovering partially delivered views. Independent stock
contexts and installed/save projections check initiator-sensitive `OnPCAdd`,
stable service roles, exact bytes and selections. Non-default script locals make
incorrect caller resolution observable. Coverage includes both starting callers,
partial/full removal, stacking, signed counts, scripted items, shared/distinct
services, four listener combinations, unset/retained/dormant selections, stale
input, caller substitution, diagnostic/result allocation failure and safe/uncertain
file failures. Restart emits no notifications; script-run counts remain zero.
Owned values survive fixture destruction.

## Fresh verification

Windows MSVC via `scripts/setup_msvc_env.ps1 -PreferLatest`, RelWithDebInfo,
`build/vnext-product`, 2026-09-15. Target `tes3mp_native_loadout_tests` and all five
filters ran individually on the reviewed build; final exits **0**.
Log prefix: `build/logs/native-alternating-`.

| Filter | Same-fixture alternations / allocation failures | Log suffix |
|---|---|---|
| `inventory-transfer-command` | 96 / 1,599; 48 cases | `command-final.log` |
| `inventory-transfer-return-restart` | 240 / 2,717; 48 cases | `return-restart-final.log` |
| `inventory-transfer-return-selections` | 160 / 1,406; 32 cases | `return-selections-final.log` |
| `inventory-transfer-restart-command` | 240 / 2,783; 48 cases | `forward-restart.log` |
| `inventory-transfer-selections` | 160 / 1,406; 32 cases | `forward-selections.log` |

Build log: `reviewed-build.log`. Both alternating directions cover all twelve
notification failure boundaries plus both actual view-buffer allocations.
Restart-command filters each cover 192 fresh uncertainty recoveries and 290
receiver allocation failures. Measured command cleanup leaves **0** tracked blocks;
delivery retains only expected owned view buffers. No complete suites, expensive
gates or upstream baseline tests ran. Documentation budget and local-link checks
ran individually, exit **0**; suffixes `docs-budget.log` and `docs-links.log`.

## Remaining limits and inherited evidence

Commands, authorization, snapshots and delivery remain test-only, serialized and
ephemeral. One supplied script/declaration set and non-gold MISC are supported;
views expose IDs/counts/selections, not full presentation state. Borrowed input
lifetimes require serialized discipline. Other-store metadata validates
identity/base/configured state, not complete saved values.

Unchanged snapshot/codec/restore/registry/script/installation matrices remain
inherited evidence. File tests use synthetic single-writer Windows I/O, not
crash/power-loss or production durability. POSIX/32-bit bounds, equipment,
gold/other types, Lua/custom state, content deletion and postponed physics remain
outside this slice. Allocation tracking excludes direct C allocation, other
threads and private external allocators. M1 real-content/enchantment evidence was
not rerun; TR remains unverified. Networking and the migration base are unchanged.
Broad headless dependencies and baseline provenance debt remain.
