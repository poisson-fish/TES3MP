# Current state and next action

## Handoff

- **Direction:** OpenMW-backed authoritative cooperative multiplayer; see
  [README.md](README.md) and [DECISIONS.md](DECISIONS.md).
- **Milestone:** M2 in [PLAN.md](PLAN.md). Fully resolved non-gold MISC pairs now
  support a reversible, effect-free installation rehearsal on disposable stock
  stores/services. Production installation remains unavailable; no live atomic
  transfer exists.
- **Next action:** add focused allocation-failure injection for the disposable
  rehearsal: cover fallible validation/setup and post-rollback revalidation,
  and prove inventory/script/registry exchange and rollback allocate nothing
  with the observer disabled. Fail each observed allocation individually;
  require exact original state, no notifications and safe consumed-pair discard.
  Keep the same complete-resolution requirement and test-only ownership boundary;
  do not enable production installation, durability or effect execution.
- **Checkpoint:** `8850e745c298c6de629ef9a5a26bbdddf6aa56e3` preserves the working
  gameplay implementation before the engine-backed pivot.

## Implemented M2 slice

[DisposableTransferRehearsal](../../apps/tes3mp-server/native/transfer_rehearsal.hpp)
is defined and linked only in `tes3mp_native_loadout_tests`. It owns both transfer
stores, an additional supplied store, WorldModel registry and LocalScripts
services. Engine headers grant private test access; no production installation
or effect API was added. External service targets and reentrant rehearsals reject.

The rehearsal consumes a protected pair and runs full `validateTransfer` before
accessing or exchanging its stock storage. Completeness must cover the entire
registry and every script entry. Existing pair/context/collection identities,
separate proposed identities, ownership/storage/lifetime witnesses, read-only
views and owned-node/iterator guards remain unchanged. Unresolved keys are never
followed and no additional objects are resolved.

It temporarily assigns proposed identities to the owned stock nodes, exchanges
source then destination inventory lists and selections, exchanges shared scripts
once or distinct services in source-then-destination order, then exchanges the
registry map, revision and generated counter. Original nodes stay intact in the
pair's storage. A scoped rollback reverses completed exchanges and restores
original selections, exact script nodes/registration identities/cursors, registry
nodes/metadata and cache flags/values. End cursors are reconstructed against the
restored list; saved node iterators retain their original allocations.

Rollback clears the temporary identities/WorldModel links from detached nodes.
Success fully revalidates and returns the same pair; exceptions roll back before
destroying it. There is no commit/release path. Test observers may inspect or
throw at each exchange checkpoint; they must not mutate/destroy the fixture,
its dependencies or borrowed pair views. No gameplay or notification consumer
is invoked by the rehearsal.

## Fresh verification

Windows MSVC 14.51 (`scripts/setup_msvc_env.ps1 -PreferLatest`), RelWithDebInfo,
`build/vnext-product`, individually:

- Focused `tes3mp_native_loadout_tests` build: exit 0.
- `inventory-transfer-rehearsal`: exit 0.
- `inventory-transfer-preparation`: exit 0.
- `inventory-two-owners`: exit 0.
- Documentation budget/links, patch-registry semantic fields, formatting and
  whitespace: exit 0.

The new synthetic filter covers 48 combinations of plain/scripted items,
existing/empty destinations, full/partial removal, shared/distinct services and
begin/middle/end script cursors. Dormant nodes and selections remain represented.
It compares installed storage against protected relocation, repeats returned
pairs, injects failures after every exchange step, and verifies discard.
Incomplete registry-only/script-only pairs, stale values/cursors/counters,
corrupted completeness/inventory/registry/iterator storage, same-address node reconstruction,
foreign pairs/services and moved-from pairs reject before the exchange observer.
Snapshots compare original allocations, identities, flags/locals, selections,
cursors, registry nodes/revision/counter and notifications. An independent
WorldModel remains unchanged throughout partial and complete rehearsal.

Initial builds exited 2 for missing explicit type includes; the first rehearsal
check exited 1 for reversed player/container arguments in the additional-store
fixture. Formatting initially exited 1 for mixed line endings. Each failure was
fixed and its check rerun before continuing. Final
logs: `build/logs/native-rehearsal-build-final.log`, `native-rehearsal-test-final.log`,
`native-rehearsal-preparation.log` and `native-rehearsal-two-owners.log`.
No complete suites, expensive gates or upstream baseline tests ran.

## Remaining limits and inherited evidence

Borrowed content/readers/script-manager/cell services must outlive use. Checks
assume serialized engine access and current state/lifetime, not complete mutation
history. The test observer contract is not a live-world concurrency boundary.
Allocator-failure coverage and allocation-free exchange/rollback evidence remain
unproven. Production installation, durability and notification execution are
unavailable. Unresolved stores, equipment, gold/other types, Lua/custom-state
transfer, persistence and stable multiplayer mapping remain outside this slice.

Inherited M1 real-Morrowind/enchantment evidence under `build/native-loadout/real`
and `build/native-loadout/parity` was not rerun; TR remains unverified. Independent
networking/standalone targets and the migration base are unchanged. Broad
openmw-lib rendering dependencies still need extraction before production
headless packaging. Whole-baseline provenance debt remains; only relevant
patch-registry entries changed.
