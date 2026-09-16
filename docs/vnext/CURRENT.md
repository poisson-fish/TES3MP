# Current state and next action

## Handoff

- **Direction:** OpenMW-backed authoritative cooperative multiplayer; see
  [README.md](README.md) and [DECISIONS.md](DECISIONS.md).
- **Milestone:** M2 in [PLAN.md](PLAN.md). The test-only inventory command joins
  owned intent, protected preparation, encoded file commit, owned success,
  fresh decode and detached restore/save. Detached restart registry preparation
  now preserves the accepted revision and generation counter without installation.
  Production durability and live atomic transfer remain unproven.
- **Next action:** persist bounded owned restart script-service metadata from the
  complete protected prepared services: shared/distinct service association,
  ordered registration instance IDs/script IDs and cursor positions. Extend the
  test codec with an explicit version change and strict membership/bounds checks;
  prove fresh decode after fixture destruction preserves registered versus
  unregistered configured items, dormant entries and service order/cursors.
  Cover malformed metadata, allocation cleanup and retry without changing caller
  outputs or fixtures. Keep script-service reconstruction, installation and command
  resumption deferred. Exact registration state is needed before detached service
  reconstruction can safely resume the authoritative inventory operation.
- **Checkpoint:** `8850e745c298c6de629ef9a5a26bbdddf6aa56e3` preserves the working
  gameplay implementation before the engine-backed pivot.

## Implemented M2 slice

[Restart preparation](../../apps/tes3mp-server/native/transfer_restart.cpp)
creates a test-only candidate containing stock registry storage. It requires
explicit fresh owner/other-store bindings, empty source/destination fixture stores,
decoded version-2 inventory membership and detached restored nodes. It checks exact
membership, identity collisions, current registration/ownership, independent node
and store lifetimes, storage identity and collection/metadata bounds before staging.
No saved pointer is followed to resolve an old fixture. Borrowed base records and
all inputs must outlive use; access remains synchronous and serialized.

Restoration now captures node lifetime views while staging the owned lists, so
registry preparation does not lazily allocate witnesses in caller nodes. The
candidate retains the saved revision and last-generated ID verbatim, including
counters beyond every surviving node and representable maxima. It never calls
registry insertion/identity generation or assigns node identities/WorldModel.
Publication swaps only the owned candidate pointer without allocation. Failed
preparation preserves the existing output allocation, detached nodes and fixtures;
staging cleanup and healthy retries are exercised. Candidate lookups reject expired
fixture/store/node witnesses and replaced storage.

[Focused tests](../../apps/tes3mp-server/native/transfer_rehearsal_tests.cpp)
encode before destroying the original fixture, then decode, restore and bind a fresh
disposable fixture. They cover plain/scripted items, shared/distinct services,
stacking, full/partial removal, signed/zero counts, empty inventories, malformed
metadata/membership, collisions, stale/missing bindings and changed ownership.
Fresh other-store state is explicitly supplied synthetic state, not reconstructed
from this save. All restart composition links only into `tes3mp_native_loadout_tests`.

Protected transfer ownership/storage/lifetime/iterator guards, persistence-before-
install, preallocated command success and sticky fail-closed uncertainty are
unchanged. Restart preparation also rejects a failed-closed fixture. No listener
notification or script instruction is dispatched by restart preparation.

## Fresh verification

Windows MSVC 14.51 (`scripts/setup_msvc_env.ps1 -PreferLatest`), RelWithDebInfo,
`build/vnext-product`, 2026-09-15; build and filters individually, exit **0**:

- `tes3mp_native_loadout_tests` build.
- `inventory-transfer-restart-registry`: **48** cases, **3,981** malformed/stale
  rejections, **1,432** injected allocation failures; valid preflight and candidate
  publication allocate **0**, tracked blocks after cleanup **0**.
- `inventory-transfer-restore`: **48** cases, **226** malformed rejections,
  **2,866** injected allocation failures, **48** incomplete pairs; tracked blocks
  after cleanup **0**.

Logs: `build/logs/native-restart-registry-`, followed by `build`, `focused` or
`restore`, then `.log`. Documentation budget and local-link checks run individually
also passed, exit **0**, with `docs-budget.log` and `docs-links.log` under that
prefix. No verification failed. No other filters, complete suites, expensive gates
or upstream baseline tests ran.

## Remaining limits and inherited evidence

The save still omits script-service registration membership/order/cursors and
selections; configured locals and item counts cannot establish exact registration
state. The candidate installs nothing and cannot resume commands. Script-service
reconstruction, production callers, notifications, script execution and durable
request deduplication remain deferred. Future resumption must reject revision/ID
exhaustion rather than wrapping or resetting saved values.

Earlier command/file-sink/codec/commit verification is inherited, not rerun here.
File evidence remains synthetic single-writer Windows I/O, not crash/power-loss or
production durability; POSIX and 32-bit bounds were not executed. Equipment,
gold/other types, Lua/custom state, content deletion and postponed physics remain
outside this slice. Allocation tracking excludes direct C allocation, other threads
and private external allocators. M1 real-content/enchantment evidence was not rerun;
TR remains unverified. Networking and the migration base are unchanged. Broad
headless dependencies and baseline provenance debt remain.
