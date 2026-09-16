# Current state and next action

## Handoff

- **Direction:** OpenMW-backed authoritative cooperative multiplayer; see
  [README.md](README.md) and [DECISIONS.md](DECISIONS.md).
- **Milestone:** M2 in [PLAN.md](PLAN.md). The test-only non-gold MISC inventory
  command preserves selected enchantment-item identities through accepted
  version-4 file saves, fresh installation and command continuation. Production
  durability and live atomic transfer remain unproven.
- **Next action:** add a test-only owned inventory notification-intent batch to
  `InventoryTransferSuccess` for the existing non-gold MISC transfer. Project
  stock prepared removal/addition and inventory-updated intents into bounded
  values containing owner/initiator/item IDs, quantity and committed revision;
  preallocate the complete success before persistence and publish it only after
  accepted installation. Cover both initiators, partial/full transfers, stacking,
  scripted items and continuation from version-4 saves. Prove rejection,
  allocation/file failure and sticky uncertainty publish no intents, healthy
  retries publish exactly the prepared batch, and installation/retirement/
  publication allocate nothing. Keep callback dispatch, script execution,
  production callers and durable request deduplication deferred. This separates
  presentation effects for M2's end-to-end authoritative inventory operation.
- **Checkpoint:** `8850e745c298c6de629ef9a5a26bbdddf6aa56e3` preserves the working
  gameplay implementation before the engine-backed pivot.

## Implemented M2 slice

The [test save codec](../../apps/tes3mp-server/native/transfer_save_codec.cpp)
accepts version 4 only. Each inventory carries an owned selected identity or
canonical unset value; no pointers or iterators enter the format. Both encoding
and decoding validate membership, including supported dormant nodes. Unsupported
versions (including the actual version-3 layout), missing/duplicate/malformed
fields, foreign inventory/owner/other-store IDs and invalid namespaces reject
without changing inputs or caller output value/storage.

[Restart installation](../../apps/tes3mp-server/native/transfer_restart.cpp)
validates carried selections against decoded inventory identities, retains the
existing ownership/storage/lifetime/iterator guards, and rejects foreign selections
on empty receiving stores. It stages receiving iterators over raw detached nodes
before installation, including nodes skipped by public inventory iteration.
Selections survive list swaps; saved revision/generation counters remain exact.

The [focused selection test](../../apps/tes3mp-server/native/transfer_rehearsal_tests.cpp)
uses accepted file saves, destroys the original fixture, installs a fresh fixture,
continues through `executeInventoryTransfer`, then restores its accepted save into
another fresh fixture. Its 32 cases cover shared/distinct services, plain/scripted
items, both initiators, unset selections, the transferred source item, unrelated
live source selections and dormant selections. Partial removal retains selection;
full removal clears only a selected source item. Destination selection stays on
its original identity, including dormant nodes when stock addition creates a
separate stack. The existing 48-case matrices also retain signed counts,
configured/unregistered items, script cursors and exhausted/gapped counters.

Malformed/stale selections and bindings, every measured allocation ordinal in the
selected paths, safe file failures and sticky uncertainty preserve fixture and
caller state. Healthy retries succeed; uncertain compositions remain closed and
recovery requires a fresh validated fixture/sink. Persistence precedes installation,
and success is preallocated. Successful installation, retirement and publication
allocate nothing. Independent state, listeners and script-run counters stay exact.
All composition remains test-target-only; production callers are unchanged.

## Fresh verification

Windows MSVC 14.51 (`scripts/setup_msvc_env.ps1 -PreferLatest`), RelWithDebInfo,
`build/vnext-product`, 2026-09-15. Target build and filters ran individually;
final exits **0**:

| Filter suffix (`inventory-transfer-`) | Evidence |
|---|---|
| `selections` | 32 cases; 1,406 allocation failures; 256 uncertain outcomes; 128 fresh recoveries |
| `codec` | 48 cases; 18,834 codec allocation failures; 3,742 malformed rejections |
| `restore` | 48 cases; 3,040 allocation failures; 236 malformed rejections |
| `restart-installation` | 48 cases; 35,680 allocation failures; 37,720 rejected attempts |
| `restart-command` | 48 cases; 2,783 allocation failures; 384 uncertain outcomes; 192 fresh recoveries |
| `command` | 48 cases; 1,599 allocation failures; 384 uncertain outcomes |
| `restart-registry` | 48 cases; 1,432 allocation failures; 3,951 rejections |
| `restart-scripts` | 48 cases; 768 allocation failures; 3,920 rejections |

Logs: `build/logs/native-selection-` plus `build`, `focused`, `codec`, `restore`,
`installation`, `restart-command`, `command`, `registry` or `scripts`, then `.log`.
Tracked blocks remaining after measured cleanup: **0**. Initial focused exit **1**
reported `selected dormant destination was not revived in place`. Source inspection
confirmed stock iteration skips zero-count nodes; the assertion was corrected to
require retained dormant selection and a different receiving stack, then rebuilt
and rerun successfully before continuing. Documentation budget and local-link
checks passed individually, exit **0**, with the same log prefix and suffixes
`docs-budget.log` and `docs-links.log`. No complete suites, expensive gates or
upstream baseline tests ran.

## Remaining limits and inherited evidence

Notifications, script execution, production callers and durable request deduplication
remain deferred. Other-store metadata validates identity/base/configured state,
not full saved values. Borrowed content/inputs must outlive serialized use. The
format retains one supplied script/declaration set and non-gold MISC scope.

File evidence is synthetic single-writer Windows I/O, not crash/power-loss or
production durability. POSIX/32-bit bounds, equipment, gold/other types, Lua/custom
state, content deletion and postponed physics remain outside this slice. Allocation
tracking excludes direct C allocation, other threads and private external allocators.
M1 real-content/enchantment evidence was not rerun; TR remains unverified. Networking
and the migration base are unchanged. Broad headless dependencies and baseline
provenance debt remain.
