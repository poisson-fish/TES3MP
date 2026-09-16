# Current state and next action

## Handoff

- **Direction:** OpenMW-backed authoritative cooperative multiplayer; see
  [README.md](README.md) and [DECISIONS.md](DECISIONS.md).
- **Milestone:** M2 in [PLAN.md](PLAN.md). The test-only inventory command joins
  owned intent, protected preparation, encoded file commit, owned success,
  fresh decode, detached restore and save. Version 2 now preserves the accepted
  registry revision and last-generated instance ID. Production durability and
  live atomic transfer remain unproven.
- **Next action:** prepare a test-only detached restart registry candidate for a
  fresh disposable fixture, using decoded version-2 metadata, restored inventory
  nodes and explicit fresh owner/other-store bindings. Preserve the saved revision
  and counter verbatim; validate complete membership, identity collisions,
  ownership/lifetimes and bounds before publication. Cover missing/stale bindings,
  allocation cleanup/retry and counters beyond surviving IDs. Keep installation,
  script-service reconstruction and command resumption deferred. This supplies
  the registry preparation needed for later end-to-end authoritative inventory
  command resumption in a fresh fixture.
- **Checkpoint:** `8850e745c298c6de629ef9a5a26bbdddf6aa56e3` preserves the working
  gameplay implementation before the engine-backed pivot.

## Implemented M2 slice

[Owned restart metadata](../../apps/tes3mp-server/native/transfer_rehearsal.hpp)
contains a 64-bit revision and engine instance counter, separately from inventory
ObjectStates and identity associations.
[Serialization](../../apps/tes3mp-server/native/transfer_rehearsal.cpp) captures
both from the complete protected prepared registry after stock validation and
before sink acceptance. Pair publication swaps inventories and metadata together
without throwing. The existing command result remains preallocated while detached;
persistence still precedes installation, retirement and success publication.

[Codec version 2](../../apps/tes3mp-server/native/transfer_save_codec.cpp) requires
ordered, exact-width revision/counter fields. Revision must be positive and fit
local `size_t`; the counter must be positive with content-file index `-1`, retaining
the codec's existing generated-ID domain. Every generated item, owner and initiator
ID must be at or below the counter. Representable maximum values round-trip;
future command resumption must guard exhaustion rather than wrap or reset them.
Version 1 is rejected without migration or reconstruction from surviving nodes.

Fresh file reopen/decode and detached restore/save preserve metadata and canonical
bytes after the original fixture is destroyed. Tests generate and retire an actual
registry node before preparation; stacking retains its counter beyond surviving
item IDs. Malformed fields, framing, identities and allocation failures preserve
caller outputs/storage and fixture state. Rejection paths check cleanup and healthy
retries. Protected ownership, storage, lifetime and iterator guards remain intact.

Safe file failures preserve prior bytes. Uncertain outcomes retain the uninstalled
fixture/result and latch both sink and fixture closed, including retry with a fresh
sink. Detached decoding is the only recovery exercised. No listener notification
or script instruction is dispatched. All transfer composition remains linked only
into `tes3mp_native_loadout_tests`.

## Fresh verification

Windows MSVC 14.51 (`scripts/setup_msvc_env.ps1 -PreferLatest`), RelWithDebInfo,
`build/vnext-product`, 2026-09-15; build and each filter individually, exit **0**:

- `tes3mp_native_loadout_tests` build.
- `inventory-transfer-command`: **48** cases, **2,208** safe rejections,
  **96** stale/repeated intents, **384** fail-closed outcomes, **1,571** injected
  allocation failures, including both result allocations.
- `inventory-transfer-file-sink`: **48** cases, **384** safe file rejections,
  **384** fail-closed outcomes, **31** invalid-file rejections and **988**
  injected allocation failures.
- `inventory-transfer-codec`: **48** cases, **3,412** malformed rejections,
  **33,328** commit-path and **17,798** encoding/decoding allocation failures.
- `inventory-transfer-commit`: **48** cases, **30,182** allocation failures.
- `inventory-transfer-restore`: **48** cases, **226** malformed rejections,
  **2,588** allocation failures.

Installation, retirement, success-publication allocations and tracked blocks after
cleanup remain **0**. Logs: `build/logs/native-inventory-restart-`, then `build`,
`command`, `file-sink`, `codec`, `commit` or `restore`, followed by `.log`.
Documentation budget and local-link checks run individually also passed, exit **0**,
with `docs-budget.log` and `docs-links.log` under that prefix. No verification failed;
no complete suites, expensive gates or upstream baseline tests ran.

## Remaining limits and inherited evidence

Commands are synchronous, serialized and fixture-only. Revision rejection is
in-process contention protection, not authentication or durable deduplication.
Restoration still creates no registry, services or selections and cannot resume
commands. Production callers, live restore installation, notifications, script
execution and durable request deduplication remain deferred.

File evidence is synthetic single-writer Windows I/O in isolated scratch space,
not crash/power-loss or production durability; POSIX and 32-bit bounds were not
executed. Runtime/content bindings remain synthetic. Equipment, gold/other types,
Lua/custom state, content deletion and postponed physics remain outside this slice.
Borrowed dependencies must outlive use; consumers forbid mutation/reentry.
Allocation tracking excludes direct C allocation, other threads and private external
allocators. M1 real-content/enchantment evidence was not rerun; TR remains unverified.
Networking and migration base are unchanged. Broad headless dependencies and
baseline provenance debt remain.
