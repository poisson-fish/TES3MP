# Current state and next action

## Handoff

- **Direction:** OpenMW-backed authoritative cooperative multiplayer; see
  [README.md](README.md) and [DECISIONS.md](DECISIONS.md).
- **Milestone:** M2 in [PLAN.md](PLAN.md). The test-only inventory command joins
  owned intent, protected preparation, encoded file commit, owned success and
  fresh decode. Detached restart now prepares both registry and stock script-service
  storage from restored inventories and explicit fresh fixture bindings.
  Production durability and live atomic transfer remain unproven.
- **Next action:** implement test-only fresh-fixture restart installation consuming
  restored inventory nodes and their detached registry/script candidates. Revalidate
  exact ownership, membership, storage, lifetimes and cursor bindings before any
  write; preallocate installation and success publication. Install each shared
  service once, preserve saved revision/generation verbatim, and prove coherent
  installed save/restore against the accepted version-3 save. Rejection/allocation
  failure must preserve fixtures, inputs and caller outputs with healthy retries;
  successful installation/retirement/publication must allocate nothing. Preserve
  persistence-before-install and sticky fail-closed uncertainty. Keep selections,
  command resumption, production callers, notifications, script execution and
  durable request deduplication deferred. This supplies the installed engine state
  needed for M2's eventual end-to-end authoritative inventory operation.
- **Checkpoint:** `8850e745c298c6de629ef9a5a26bbdddf6aa56e3` preserves the working
  gameplay implementation before the engine-backed pivot.

## Implemented M2 slice

[Restart preparation](../../apps/tes3mp-server/native/transfer_restart.cpp) binds
version-3 script metadata to exact restored nodes, fresh owners/stores/services,
other-store items and the detached registry candidate. Complete registry validation
is reused without building a second registry. Service association, identity,
base-record pointers, configured locals identity/shape/ranges, membership, bounds,
registration witnesses and cursors validate before staging. Successful validation
allocates nothing. Fresh service capture uses opt-in weak lifetime witnesses;
same-address service/store replacement cannot validate stale bindings.

The owned candidate reuses stock `LocalScripts` list preparation and storage.
Shared services share one candidate; distinct services retain separate storage,
even when empty. Ordered registrations and begin/middle/end cursors survive.
Configured locals do not imply registration, and dormant registrations remain.
No locals initialization or script instructions occur. Candidate access checks
borrowed owner/store/service/item lifetimes, including unregistered inventory
nodes. Registry destruction does not invalidate the independently owned script
storage; fixture or inventory destruction does. Publication is a nonthrowing
owned-pointer swap. Inputs and fresh fixture state remain unchanged.

The [focused test](../../apps/tes3mp-server/native/transfer_rehearsal_tests.cpp)
encodes before original fixture destruction and then decodes/restores against a
fresh fixture in all 48 cases. It covers malformed/missing/duplicate/stale bindings,
metadata disagreement, different or destroyed restored nodes, configured
unregistered items, dormant entries, empty services, reversed registration order,
exhausted saved counters, individual allocation cleanup and successful retries.
Independent state, listeners and script-run counters remain unchanged.

Saved revision/generation values remain verbatim. Protected ownership/storage/
lifetime/iterator guards, persistence-before-install, preallocated command success
and sticky fail-closed uncertainty remain intact. All restart composition remains
test-target-only; nothing is installed by this slice.

## Fresh verification

Windows MSVC 14.51 (`scripts/setup_msvc_env.ps1 -PreferLatest`), RelWithDebInfo,
`build/vnext-product`, 2026-09-15; final build and filters individually, exit **0**:

- `tes3mp_native_loadout_tests` build.
- `inventory-transfer-restart-scripts`: **48** fresh reconstructions, **3,920**
  malformed/stale rejections, **768** injected allocation failures;
  validation/publication allocate **0**.
- `inventory-transfer-script-metadata`: **48** cases, **4,704** malformed
  rejections, **46,234** injected allocation failures.
- `inventory-transfer-restart-registry`: **48** cases, **3,963** malformed/stale
  rejections, **1,432** allocation failures; validation/publication allocate **0**.
- `inventory-transfer-restore`: **48** cases, **226** malformed rejections,
  **3,040** allocation failures, **48** incomplete pairs.

Tracked blocks after cleanup are **0** in every filter. Logs are
`build/logs/native-restart-scripts-` plus `build`, `focused`, `metadata`, `registry`
or `restore`, then `.log`. Initial focused runs exited **1** because the new test
assumed diagnostic exceptions always allocate through C++ hooks; the harness now
uses observed allocation counts and passed before further checks. Documentation
budget and local-link checks individually passed, exit **0** (`docs-budget.log`
and `docs-links.log` under that prefix). No complete suites, expensive gates or
upstream baseline tests ran.

## Remaining limits and inherited evidence

Restart installation, selections, command resumption, production callers,
notifications, script execution and durable request deduplication remain deferred.
Other-store metadata validates identity/base/configured state, not its full saved
values. Content and borrowed inputs must outlive use and remain serialized and
unchanged. Future installation needs complete revalidation, not lifetime checks
alone; future resumption must reject revision/ID exhaustion rather than wrap.
The format retains the single supplied script/declaration set and non-gold MISC scope.

Inherited file evidence is synthetic single-writer Windows I/O, not crash/power-loss
or production durability. POSIX/32-bit bounds, equipment, gold/other types,
Lua/custom state, content deletion and postponed physics remain outside this slice.
Allocation tracking excludes direct C allocation, other threads and private external
allocators. M1 real-content/enchantment evidence was not rerun; TR remains unverified.
Networking and the migration base are unchanged. Broad headless dependencies and
baseline provenance debt remain.
