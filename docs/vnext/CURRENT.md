# Current state and next action

## Handoff

- **Direction:** OpenMW-backed authoritative cooperative multiplayer; see
  [README.md](README.md) and [DECISIONS.md](DECISIONS.md).
- **Milestone:** M2 in [PLAN.md](PLAN.md). The test-only inventory command joins
  owned intent, protected preparation, encoded file commit and owned success.
  Fresh-fixture restart now installs restored inventory nodes and detached stock
  registry/LocalScripts storage after complete revalidation and preallocation.
  Production durability and live atomic transfer remain unproven.
- **Next action:** implement test-only post-restart continuation of one existing
  source-to-destination inventory transfer through `executeInventoryTransfer`.
  Start from an installed accepted version-3 save, resolve owned command IDs and
  the exact saved revision against that fresh fixture, then persist the next
  transfer through the existing file sink before installation/publication. Prove
  its accepted save decodes/restores/installs into a second fresh fixture. Reject
  stale/wrong-owner inputs and revision/generated-ID exhaustion before mutation
  or persistence; never wrap or rebuild saved counters. Exercise rejection,
  allocation/file failure, sticky uncertainty and healthy retries. Keep selections,
  production callers, notifications, script execution and durable request
  deduplication deferred. This joins installed restart state to M2's eventual
  end-to-end authoritative inventory operation.
- **Checkpoint:** `8850e745c298c6de629ef9a5a26bbdddf6aa56e3` preserves the working
  gameplay implementation before the engine-backed pivot.

## Implemented M2 slice

[Restart installation](../../apps/tes3mp-server/native/transfer_restart.cpp)
consumes the three preparation-produced owned candidates only on success.
It reuses complete registry/script validation, including exact fixture owners,
store membership/storage identities, service associations, weak lifetimes,
restored nodes and other-store bindings. Retained immutable registrations let
stock `LocalScripts::validateStorage` revalidate list nodes, bindings and cursor
addresses. Configured unregistered nodes are checked too. Unsupported transient
engine state rejects. No saved pointer or iterator bypasses its guards.

Before writing, the installer serializes restored engine values and compares both
that encoding and the decoded input against the supplied accepted version-3 bytes.
It preallocates the owned success save, assigned CellRefs, replacement storage
identities and cursor positions, then revalidates. Installation uses nonthrowing
swaps/assignments, installs shared services once and preserves distinct/empty
services, registration order, begin/middle/end cursors and dormant membership.
Saved revision/generation values remain verbatim, including exhausted maxima.
Selections remain unset. Old service/registry storage and consumed candidates
retire before nonthrowing result publication. Repeated restart installation into
an already installed fixture rejects, including empty inventories.

The [focused test](../../apps/tes3mp-server/native/transfer_rehearsal_tests.cpp)
accepts an encoded save synchronously, destroys the original fixture, then
reconstructs fresh fixtures in all 48 matrix cases. Installed engine save bytes
match the accepted bytes exactly and decode/restore/save coherently. Coverage
includes malformed/stale bindings, missing candidates, different/destroyed nodes,
same-address store/service replacement, fixture destruction, corrupted cursors,
configured unregistered items, dormant entries, reversed/empty registrations and
exhausted counters. Every injected allocation failure preserves inputs, fixture
and caller output, cleans up and retries successfully with retained candidates.
Sticky uncertainty requires discarding the fixture; installation cannot clear it.
Independent state, listeners and script-run counters remain unchanged.

Protected ownership/storage/lifetime/iterator guards, persistence-before-install
and preallocated command success remain intact. All restart composition remains
test-target-only. Command continuation has not yet been exercised after restart.

## Fresh verification

Windows MSVC 14.51 (`scripts/setup_msvc_env.ps1 -PreferLatest`), RelWithDebInfo,
`build/vnext-product`, 2026-09-15; final checks individually, exit **0**:

- `tes3mp_native_loadout_tests` build.
- `inventory-transfer-restart-installation`: **48** cases, **35,488** individual
  allocation failures, **37,192** rejected attempts including allocation failures.
- `inventory-transfer-restart-scripts`: **48** cases, **3,920** malformed/stale
  rejections, **768** allocation failures.
- `inventory-transfer-restart-registry`: **48** cases, **3,978** malformed/stale
  rejections, **1,432** allocation failures.
- `inventory-transfer-restore`: **48** cases, **226** malformed rejections,
  **3,040** allocation failures, **48** incomplete pairs.

Successful installation, retirement and publication allocate **0**. Tracked
blocks after cleanup are **0** in every filter. Logs use
`build/logs/native-restart-install-` plus `build`, `focused`, `scripts`, `registry`
or `restore`, then `.log`. Initial build exit **2** exposed moved test-helper
bindings; initial focused exits **1** counted retained fixture allocations before
fixture destruction. Both were fixed and rerun successfully before proceeding.
Documentation budget and local links passed individually, exit **0**, in
`docs-budget.log` and `docs-links.log` under that prefix. No complete suites,
expensive gates or upstream baseline tests ran.

## Remaining limits and inherited evidence

Selections, command continuation, production callers, notifications, script
execution and durable request deduplication remain deferred. Other-store metadata
validates identity/base/configured state, not full saved values. Content and
borrowed inputs must outlive use and remain serialized/unchanged. The format
retains one supplied script/declaration set and non-gold MISC scope.

Inherited file evidence is synthetic single-writer Windows I/O, not crash/power-loss
or production durability. POSIX/32-bit bounds, equipment, gold/other types,
Lua/custom state, content deletion and postponed physics remain outside this slice.
Allocation tracking excludes direct C allocation, other threads and private external
allocators. M1 real-content/enchantment evidence was not rerun; TR remains unverified.
Networking and the migration base are unchanged. Broad headless dependencies and
baseline provenance debt remain.
