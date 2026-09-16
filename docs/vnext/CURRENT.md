# Current state and next action

## Handoff

- **Direction:** OpenMW-backed authoritative cooperative multiplayer; see
  [README.md](README.md) and [DECISIONS.md](DECISIONS.md).
- **Milestone:** M2 in [PLAN.md](PLAN.md). The test-only inventory command now
  continues an installed accepted version-3 save and persists a transfer that
  decodes/restores/installs into another fresh fixture. Production durability
  and live atomic transfer remain unproven.
- **Next action:** implement test-only persistence of source/destination selected
  enchantment-item identities for the existing non-gold MISC transfer. Introduce
  an explicit version-4 test save with owned identity-or-unset metadata; reject
  unsupported versions rather than silently dropping selections. Validate each
  selection against its inventory, including supported dormant nodes, and stage
  receiving iterators before installation. Preserve stock prepared-transfer
  selection semantics through accepted file save, fresh restart installation and
  `executeInventoryTransfer` continuation. Prove rejection/allocation/file failures
  preserve inputs, fixture and caller outputs, with allocation-free successful
  installation/retirement/publication. Production callers, notifications, script
  execution and durable request deduplication remain deferred. This closes one
  remaining state gap toward M2's end-to-end authoritative inventory operation.
- **Checkpoint:** `8850e745c298c6de629ef9a5a26bbdddf6aa56e3` preserves the working
  gameplay implementation before the engine-backed pivot.

## Implemented M2 slice

[executeInventoryTransfer](../../apps/tes3mp-server/native/inventory_transfer_command.cpp)
rejects exhausted saved registry revisions and the version-3 generated-ID namespace
before preparation or I/O, including stacking commands. The bounded format supports
content-file slot -1; commands cannot advance it to -2 or wrap the revision.
Restoration still preserves exhausted counters verbatim. No counters are rebuilt
from surviving identities.

The [focused continuation test](../../apps/tes3mp-server/native/transfer_rehearsal_tests.cpp)
starts with an accepted save after destroying the original fixture. It installs
fresh stock inventory/registry/LocalScripts storage, resolves owned command IDs
and the exact saved revision there, then performs another partial/full transfer
through the existing file sink. After fixture destruction, accepted bytes decode,
restore and install into a second fresh fixture whose engine save matches exactly.
The 48 cases cover shared/distinct services, plain/scripted items, existing/new
stacks, both initiators, begin/middle/end cursors, configured unregistered items,
dormant membership and signed counts. Explicit counter gaps, last usable values
and exhausted saves are exercised without generation or revision reconstruction.

Wrong-owner/stale commands, expired contexts, missing services, safe file failures
and every observed command allocation ordinal in representative plain/scripted
cases preserve fixture state and caller output value/storage. Each allocation
failure retries successfully. Post-replacement uncertainty preserves the old
fixture and output, blocks old/new sinks on that fixture, and keeps the old sink
closed even after a fresh restart. Partial transfers resume successfully only
through a fresh validated fixture and healthy sink. Repeated old revisions reject;
this is not durable request deduplication.

Protected ownership/storage/lifetime/iterator validation, persistence-before-install
and preallocated owned success remain intact. Successful installation, retirement
and publication allocate nothing. Independent state, listeners and script-run
counters remain unchanged. Source/destination selections remain unset after restart.
All composition remains test-target-only; production callers are unchanged.

## Fresh verification

Windows MSVC 14.51 (`scripts/setup_msvc_env.ps1 -PreferLatest`), RelWithDebInfo,
`build/vnext-product`, 2026-09-15; final checks individually, exit **0**:

- `tes3mp_native_loadout_tests` build.
- `inventory-transfer-restart-command`: **48** cases, **2,544** safe rejections,
  **144** stale/repeated commands, **384** uncertain outcomes, **2,779** individual
  allocation failures, **192** fresh-composition recoveries.
- `inventory-transfer-command`: **48** cases, **2,208** safe rejections,
  **384** uncertain outcomes, **1,595** individual allocation failures.
- `inventory-transfer-restart-installation`: **48** cases, **35,488** individual
  allocation failures, **37,192** rejected attempts including allocation failures.

Successful installation/retirement/publication allocations and tracked blocks
remaining after measured cleanup are **0**. Logs use
`build/logs/native-restart-command-` plus `build`, `focused`, `command` or
`installation`, then `.log`. Initial focused exit **1** reported
`post-restart safe file failure installed, published or replaced accepted bytes`:
the test expected the Replace fault at replacement, but this safe seam rejects at
Close. The assertion was fixed and that filter rerun successfully before proceeding.
No complete suites, expensive gates or upstream baseline tests ran.
Documentation budget and local links passed individually, exit **0**; logs use
the same prefix with `docs-budget.log` and `docs-links.log`.

## Remaining limits and inherited evidence

Selections, production callers, notifications, script execution and durable request
deduplication remain deferred. Other-store metadata validates identity/base/configured
state, not full saved values. Borrowed content/inputs must outlive serialized use.
The format retains one supplied script/declaration set and non-gold MISC scope.

File evidence is synthetic single-writer Windows I/O, not crash/power-loss or
production durability. POSIX/32-bit bounds, equipment, gold/other types, Lua/custom
state, content deletion and postponed physics remain outside this slice. Allocation
tracking excludes direct C allocation, other threads and private external allocators.
M1 real-content/enchantment evidence was not rerun; TR remains unverified. Networking
and the migration base are unchanged. Broad headless dependencies and baseline
provenance debt remain.
