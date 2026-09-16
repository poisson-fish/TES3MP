# Current state and next action

## Handoff

- **Direction:** OpenMW-backed authoritative cooperative multiplayer; see
  [README.md](README.md) and [DECISIONS.md](DECISIONS.md).
- **Milestone:** M2 in [PLAN.md](PLAN.md). The test-only non-gold MISC command now
  covers accepted persistence, installation, owned notification publication and
  consumption, fresh restart and further command continuation. Production
  durability and live atomic transfer remain unproven.
- **Next action:** add a bounded test-only owned inventory-view snapshot from a
  validated installed fixture, carrying owner/item IDs, signed counts, selected
  identity (including dormant nodes) and exact revision. Publish by swap after
  complete preparation. Use it to resynchronize two independent owner views
  after notification delivery failure, then continue the same command path after
  a version-4 restart. Prove views converge with authoritative accepted state
  without replaying gameplay. Keep production integration, script execution and
  durable request/notification deduplication deferred.
- **Checkpoint:** `8850e745c298c6de629ef9a5a26bbdddf6aa56e3` preserves the working
  gameplay implementation before the engine-backed pivot.

## Implemented M2 slice

[InventoryTransferSuccess](../../apps/tes3mp-server/native/inventory_transfer_command.hpp)
owns a fixed four-slot notification batch: optional item removal, source inventory
update, optional item addition, destination inventory update. Each intent carries
owner, initiator and item IDs, quantity and committed revision. Existing prepared
transfer interfaces provide the decisions; no additional engine extraction was
needed. All success/notification storage is prepared before persistence. Accepted
installation precedes publication; installation, retirement and publication
allocate nothing. No engine pointers or iterators enter the batch or save.

The [test-only consumer](../../apps/tes3mp-server/native/inventory_transfer_command.cpp)
detaches the published success before calling receivers with owned values. It
consumes the batch once on both success and failure. A receiver exception returns
`FailedAfterCommit`, the committed revision and confirmed prefix. The failing
intent may already have been received; the undelivered suffix is discarded. No
batch retry, gameplay retry, rollback or durable delivery guarantee is implied.
Repeated/reentrant consumption of the same empty slot emits nothing.

The [focused tests](../../apps/tes3mp-server/native/transfer_rehearsal_tests.cpp)
route intents into separate owner mailboxes while retaining the explicit initiator.
An independent stock remove/add oracle checks callback ordering. Coverage includes
both initiators, partial/full removal, stacking, signed counts, scripted items,
shared/distinct services and all four listener-presence combinations. At every
intent boundary, failures before receipt, after receipt and in receiver allocation
preserve installed gameplay and accepted bytes. Rejection, preparation allocation
failure, safe file rejection and sticky uncertainty preserve caller output and
publish no new batch; healthy allocation retries publish the exact prepared batch.

Restart tests now seed saves through `executeInventoryTransfer`, destroy the
original fixture, consume its surviving owned batch, install a fresh fixture and
continue through the command and consumer. Another fresh restart accepts a new
revision-bound command and its own batch; a third installation verifies that save.
Restart itself emits no notifications. Version-4 selection persistence, exact saved
counters, ownership/storage/lifetime/iterator guards and unchanged unrelated
state/listeners/script-run counts remain covered. Production callers are unchanged.

## Fresh verification

Windows MSVC 14.51 (`scripts/setup_msvc_env.ps1 -PreferLatest`), RelWithDebInfo,
`build/vnext-product`, 2026-09-15. Target `tes3mp_native_loadout_tests` and each
filter ran separately; exits **0**:

| Filter suffix (`inventory-transfer-`) | Evidence |
|---|---|
| `command` | 48 cases; 1,599 allocation failures; 384 uncertain outcomes; 4 delivered batches; 44 delivery failures after commit |
| `restart-command` | 48 cases; 2,783 allocation failures; 384 uncertain outcomes; 192 fresh recoveries; 515 delivered batches; 133 delivery failures after commit |
| `selections` | 32 cases; 1,406 allocation failures; 256 uncertain outcomes; 128 fresh recoveries |

Logs: `build/logs/native-notification-` with suffixes `reviewed-build.log`,
`reviewed-command.log`, `restart-command.log` and `selections.log`. Both command
filters cover all twelve delivery failure boundaries. Measured cleanup leaves
**0** tracked blocks. Documentation budget and local-link checks ran individually,
exit **0**, with suffixes `docs-budget.log` and `docs-links.log`. No complete suites,
expensive gates or upstream baseline tests ran.

## Remaining limits and inherited evidence

Delivery is test-only and ephemeral; recipient view recovery is the next action.
The unchanged codec accepts version 4 only. Previous codec/restore/registry/script
and installation matrices remain inherited evidence, not rerun here. Other-store
metadata validates identity/base/configured state, not full saved values. Borrowed
content/inputs require serialized lifetime discipline. One supplied script/declaration
set and non-gold MISC remain the supported scope.

File evidence is synthetic single-writer Windows I/O, not crash/power-loss or
production durability. POSIX/32-bit bounds, equipment, gold/other types, Lua/custom
state, content deletion and postponed physics remain outside this slice. Allocation
tracking excludes direct C allocation, other threads and private external allocators.
M1 real-content/enchantment evidence was not rerun; TR remains unverified. Networking
and the migration base are unchanged. Broad headless dependencies and baseline
provenance debt remain.
