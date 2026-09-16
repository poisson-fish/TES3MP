# Current state and next action

## Handoff

- **Direction:** OpenMW-backed authoritative cooperative multiplayer; see
  [README.md](README.md) and [DECISIONS.md](DECISIONS.md).
- **Milestone:** M2 in [PLAN.md](PLAN.md). The test-only inventory command joins
  owned intent, complete protected preparation, encoded file commit, owned success,
  fresh reopen/decode, detached restore and save over disposable fixtures.
  Production durability and live atomic transfer remain unproven.
- **Next action:** persist the accepted registry revision and last-generated
  instance ID as owned restart metadata in a new test-only transfer codec version.
  Capture both from the complete prepared registry before acceptance; carry them
  through command/file commit, fresh decode, detached restore and save. Validate
  bounds and identity relationships, including a counter beyond surviving item
  IDs; never reconstruct the counter only from surviving nodes. Reject old-version
  and malformed metadata without changing outputs or fixtures. This supplies the
  missing revision/identity basis for later fresh-fixture command resumption;
  live restore installation and durable request deduplication remain deferred.
- **Checkpoint:** `8850e745c298c6de629ef9a5a26bbdddf6aa56e3` preserves the working
  gameplay implementation before the engine-backed pivot.

## Implemented M2 slice

The [command adapter](../../apps/tes3mp-server/native/inventory_transfer_command.cpp)
and its [owned values](../../apps/tes3mp-server/native/inventory_transfer_command.hpp)
are linked only into `tes3mp_native_loadout_tests`. Commands contain source and
destination owner, initiator and item-instance IDs, positive quantity and expected
registry revision. Results contain the accepted command, destination instance,
signed source/destination counts and installed revision; no engine objects,
pointers, services or iterators cross that value boundary.

Each call resolves current registry identities against the fixture's fixed
source/destination stores and configured initiator, checks lifetime and save-owner
bindings, and supplies the fixture's other store for complete protected resolution.
Stock preparation/validation retains ownership, storage, lifetime, script and
iterator guards. The success allocation is prepared while the pair is detached.
[Disposable commit](../../apps/tes3mp-server/native/transfer_rehearsal.cpp) encodes
with existing `SaveBindings` and invokes
[TransferFileSink](../../apps/tes3mp-server/native/transfer_file_sink.cpp).
Only accepted installation/retirement is followed by a nonthrowing result swap.
No notification, listener callback or script instruction is dispatched.

Safe validation, allocation and pre-replacement file failures preserve fixture
state and caller result storage; file rejection also preserves prior bytes and
permits retry. Foreign staging files remain untouched. Uncertainty preserves the
uninstalled fixture/result, leaves coherent prior/new bytes in the exercised
fault cases, and latches the sink and fixture closed. Subsequent commands reject
without allocation or I/O even with a fresh sink. Recovery here is fresh detached
decode only, never installation into the uncertain fixture.

## Fresh verification

The existing adapter at `2d0d31a9ae5e907f2e8e0d5deefb55f485d72695` was reviewed
against the command slice and freshly verified on 2026-09-15. No code changes
were needed; restart metadata remains the next implementation slice.

Windows MSVC 14.51 (`scripts/setup_msvc_env.ps1 -PreferLatest`), RelWithDebInfo,
`build/vnext-product`, individually, exit **0**:

- `tes3mp_native_loadout_tests` build.
- `inventory-transfer-command`: **48** plain/scripted cases, shared/distinct
  script services, both configured player initiators, partial/full removal,
  stacking, signed counts, dormant nodes and cursor positions. **2,208** safe
  rejections, **96** stale/repeated intents, **384** fail-closed outcomes and
  **1,567** injected allocation failures, including both result allocations.
  Plain and scripted representative command paths exhaust every observed ordinal;
  every matrix case checks successful cleanup with the next ordinal armed.
  Installation, retirement and publication allocations and remaining tracked
  blocks are **0**. Accepted results survive fixture destruction; fresh file
  reopen/decode, detached restore and save reproduce the committed bytes.
- `inventory-transfer-file-sink`: **48** cases, **384** safe file rejections,
  **384** fail-closed outcomes, **28** invalid-file rejections and **981**
  injected allocation failures.
- `inventory-transfer-codec`: **48** cases, **3,344** invalid-input rejections,
  **33,232** commit-path and **17,600** codec allocation failures.
- `inventory-transfer-commit`: **48** cases, **30,182** allocation failures.
- `inventory-transfer-restore`: **218** rejections, **2,588** allocation failures.

Logs use `build/logs/native-inventory-command-recheck-`, with `build`, `command`,
`file-sink`, `codec`, `commit` and `restore` suffixes followed by `.log`.
The individual documentation budget and local-link checks also passed, exit **0**
(`docs-budget.log` and `docs-links.log` under the same prefix). No verification
failed. No complete suites, expensive gates or upstream baseline tests ran.

## Remaining limits and inherited evidence

The command is synchronous, serialized and fixture-only. Its revision rejection
is in-process contention protection, not authentication or durable deduplication.
Codec version 1 persists neither registry revision nor the last-generated counter;
detached restoration reconstructs no registry, services or selections and cannot
resume commands. Runtime/content identities remain synthetic trusted bindings.

File evidence is synthetic Windows I/O with one writer and an isolated scratch
directory, not crash/power-loss or production durability; POSIX was not executed.
Production callers, live restore installation, notifications and script execution
remain deferred. Equipment, gold/other types, Lua/custom state, content-deletion
state and postponed physics remain outside this slice. Borrowed dependencies must
outlive use; consumers forbid mutation/reentry. Allocation tracking excludes direct
C allocation, other threads and private external allocators. M1 real-content and
enchantment evidence was not rerun; TR remains unverified. Networking and migration
base are unchanged. Broad headless dependencies and baseline provenance debt remain.
