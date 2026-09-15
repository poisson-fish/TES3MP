# Current state and next action

## Handoff

- **Direction:** OpenMW-backed authoritative cooperative multiplayer; see
  [README.md](README.md) and [DECISIONS.md](DECISIONS.md).
- **Milestone:** M2 in [PLAN.md](PLAN.md). Fully resolved non-gold MISC pairs now
  support persistence-gated disposable-fixture commit through the bounded codec
  and a **test-only synchronous byte-file sink**, followed by fresh reopen,
  decode, detached restore and save. Production durability and live atomic
  transfer remain unproven.
- **Next action:** implement a test-only app-local inventory command adapter over
  the disposable fixture. Accept owned source/destination owner, initiator and
  item-instance IDs, quantity and expected registry revision; resolve current
  contexts, prepare the complete protected pair, encode and commit through the
  file sink. Publish an owned success result only after accepted installation.
  Prove stale/repeated intents, invalid bindings, file/allocation failures and
  sticky uncertainty cannot mutate or report success. Keep production callers,
  live restore installation, notifications, scripts and durable request deduplication
  deferred; this joins the proven pieces into one explicit authoritative intent.
- **Checkpoint:** `8850e745c298c6de629ef9a5a26bbdddf6aa56e3` preserves the working
  gameplay implementation before the engine-backed pivot.

## Implemented M2 slice

[The file sink](../../apps/tes3mp-server/native/transfer_file_sink.cpp) is linked
only into `tes3mp_native_loadout_tests`. It reuses OS mechanics inspected in
`canonical_persistence_file.cpp`, without its independent gameplay schema:
exclusive sibling `.tmp` creation, complete short-write loops, flush on the writing
handle, checked close, replacement and fresh byte-for-byte readback before
acceptance. Windows uses `FlushFileBuffers` and `MoveFileExW` with replacement,
write-through and bounded lock/share retries. The POSIX branch uses file `fsync`,
rename and parent-directory `fsync`; that branch was not executed here.

Paths are allocated before writes. Native write/flush/replace/readback and cleanup
allocate no observed C++ state. Opened-handle regular-file size checks enforce the
8 MiB cap before buffer allocation; exact reads and an EOF check reject short or
growing files. Read output publishes by nonthrowing swap. Existing foreign staging
files are preserved; cleanup removes only staging created by this sink.

Safe pre-replacement rejection preserves prior bytes and fixture state and allows
retry. Replacement errors, failed post-replacement barriers or failed readback
return explicit uncertainty. The sink latches closed, and
[disposable commit](../../apps/tes3mp-server/native/transfer_rehearsal.cpp) throws
an allocation-free uncertainty signal and permanently blocks both commit entry
points and rehearsal. No acceptance, installation or notification follows.
Tests reopen coherent prior/new files after uncertainty without installing them.
Acceptance still precedes nonthrowing, allocation-free installation/retirement;
protected ownership, storage, lifetime and iterator validation remain intact.

The [codec](../../apps/tes3mp-server/native/transfer_save_codec.cpp) is unchanged:
version 1 binds OpenMW format 37, caller-supplied runtime/content identity and
source/destination owners plus initiator. Complete supported ObjectStates and
separate unique instance identities retain engine values, script locals, dormant
nodes and signed counts. Allocation-free framing/binding preflight, canonical
re-encoding and supplied interned RefIds preserve its existing validation limits.
No pointers, services, registry storage, selections or iterators enter the file.

## Fresh verification

Windows MSVC 14.51 (`scripts/setup_msvc_env.ps1 -PreferLatest`), RelWithDebInfo,
`build/vnext-product`, individually, final exit **0**:

- `tes3mp_native_loadout_tests` build/rebuilds.
- `inventory-transfer-file-sink`: **48** cases, **384** safe file rejections,
  **384** fail-closed outcomes, **28** invalid-file rejections and **981** injected
  allocation failures. Constructor/read/decode/rejection and representative
  commit ordinals are exhausted; successful cleanup is observed in every case.
  Installation/retirement allocations and remaining tracked blocks are **0**.
- `inventory-transfer-codec`: **48** cases, **3,344** invalid-input rejections,
  **33,232** commit-path and **17,600** codec allocation failures.
- `inventory-transfer-commit`: **48** cases, **30,182** allocation failures.
- `inventory-transfer-restore`: **218** rejections, **2,588** allocation failures.

Logs use `build/logs/native-inventory-file-sink-`. The new filter initially exited
**1** on an assertion comparing distinct prepared-node addresses; semantic
ownership/order checks fixed it. A later run exited **1** at prior-file setup;
bounded Windows replacement retries and diagnostics were added. Each failed
filter reran **0** before continuing. No complete suites, expensive gates or
upstream baseline tests ran.

## Remaining limits and inherited evidence

Evidence is synchronous synthetic Windows file I/O, not process-crash, power-loss,
filesystem-independent or production durability. One writer, an isolated scratch
directory and serialized access are assumed. Uncertain fixtures are discarded;
no live recovery is implemented. The little-endian format uses synthetic trusted
identities, not production fingerprints/authentication or durable request IDs.
Detached restoration reconstructs no services, registry or selections. Equipment,
gold/other types, Lua/custom state, scene/content-deletion state and postponed
physics remain outside scope. Borrowed content must outlive use; sinks forbid
mutation/reentry. Allocation tracking excludes direct C allocation, other threads
and private external allocators. M1 real-content/enchantment evidence was not
rerun; TR remains unverified. Networking and migration base are unchanged. Broad
openmw-lib headless dependencies and whole-baseline provenance debt remain.
