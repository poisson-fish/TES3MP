# Current state and next action

## Handoff

- **Direction:** OpenMW-backed authoritative cooperative multiplayer; see
  [README.md](README.md) and [DECISIONS.md](DECISIONS.md).
- **Milestone:** M2 in [PLAN.md](PLAN.md). Fully resolved non-gold MISC pairs
  support an allocation-tested, reversible, effect-free installation rehearsal
  on disposable stock stores/services. Production installation remains
  unavailable; no live atomic transfer exists.
- **Next action:** extend test-only allocation-failure injection to
  `ContainerStore::prepareTransfer` itself for the same fully resolved non-gold
  MISC matrix, including its final fallible consumer copy. Fail each observed
  allocation individually; require exact original state, no effects, safe
  partial-preparation cleanup and successful fresh preparation/rehearsal after
  each failure. Keep complete resolution, protected witnesses and test-only
  ownership; do not enable production installation, durability or effects.
- **Checkpoint:** `8850e745c298c6de629ef9a5a26bbdddf6aa56e3` preserves the working
  gameplay implementation before the engine-backed pivot.

## Implemented M2 slice

[DisposableTransferRehearsal](../../apps/tes3mp-server/native/transfer_rehearsal.hpp)
and [allocation instrumentation](../../apps/tes3mp-server/native/test_allocations.hpp)
are defined and linked only in `tes3mp_native_loadout_tests`. The rehearsal owns
both transfer stores, an additional supplied store, WorldModel registry and
LocalScripts services. External service targets and reentrant rehearsals reject.
There is no production installation, commit/release or effect API.

Full `validateTransfer` and complete registry/script resolution precede stock
storage access. Protected pair/context/collection bindings, separate proposed
identities, ownership/storage/lifetime witnesses, read-only views and owned-node/
iterator guards remain intact. Unresolved keys are never followed; no additional
objects are resolved. Shared scripts exchange once; distinct services exchange
in source-then-destination order.

The rehearsal retains original inventory, script and registry nodes through
temporary exchanges, then restores original selections, registrations, cursors,
registry revision/generated counter and cache flags/values. Rollback clears
temporary identities/WorldModel links from detached nodes. Success fully
revalidates and returns the same pair; exceptions discard it after restoration.

Thread-local instrumentation counts C++ scalar/array, aligned and nothrow
allocations, injecting one `std::bad_alloc` at a time. Fixed-size phase counters
separate validation, setup, exchange, rollback and post-rollback revalidation.
The observer is empty throughout measured calls. Instrumentation remains active
through unwinding and consumed-pair destruction; assertions and snapshots run
after it is disabled. Hook self-checks cover all eight allocation routes,
zero-size requests, alignment and throwing/nothrow semantics.

The 48 synthetic combinations cover plain/scripted MISC, existing/empty
destinations, full/partial removal, shared/distinct services and begin/middle/end
script cursors, with dormant nodes and selections. Every failure checks exact
original nodes, reference lifetimes, identities, values/locals, selections,
script nodes/registrations/cursors, registry nodes/revision/counter and caches.
Snapshots also cover supplied-store raw order, recharge-cache storage/capacity,
resolved flags and listeners. No notifications, script execution or unrelated
WorldModel changes occur. All consumed owned inventory references expire, and
a fresh pair successfully rehearses and discards on the same fixture after
every failure. Repeated successful pairs retain bindings and allocation counts.

## Fresh verification

Windows MSVC 14.51 (`scripts/setup_msvc_env.ps1 -PreferLatest`), RelWithDebInfo,
`build/vnext-product`, individually:

- Focused `tes3mp_native_loadout_tests` build: exit 0.
- `inventory-transfer-rehearsal-allocations`: exit 0; **6,998** allocations
  failed individually: validation 3,454, setup 90, revalidation 3,454.
  Exchange and rollback each observed **zero allocations**.
- `inventory-transfer-rehearsal`: exit 0, including checkpoint exceptions,
  corruption/staleness rejection, complete-resolution guards and service order.
- `inventory-transfer-preparation`: exit 0.
- Documentation budget/links, patch-registry semantic fields, formatting and
  whitespace: exit 0.

The initial allocation check exited 1 because it incorrectly required setup
allocations in every case; plain fixtures have empty script lists. That assertion
was corrected to require fallible setup coverage across the matrix, then the
build and failed check passed before continuing. Logs are in `build/logs/`:
`native-rehearsal-allocations-build-final.log`,
`native-rehearsal-allocations-test-final.log`,
`native-rehearsal-allocations-regression.log` and
`native-rehearsal-allocations-preparation.log`.
No complete suites, expensive gates or upstream baseline tests ran.

## Remaining limits and inherited evidence

Allocation injection covers rehearsal calls on already prepared pairs, not
pair construction, direct C allocation, other threads or external libraries'
private allocators. Borrowed content/readers/script-manager/cell services must
outlive use. Checks assume serialized engine access and current state/lifetime,
not complete mutation history or live-world concurrency safety.

Production installation, durability and notification execution remain unavailable.
Unresolved stores, equipment, gold/other types, Lua/custom-state transfer,
persistence and stable multiplayer mapping remain outside this slice.
Inherited M1 real-Morrowind/enchantment evidence under `build/native-loadout/real`
and `build/native-loadout/parity` was not rerun; TR remains unverified. Independent
networking/standalone targets and the migration base are unchanged. Broad
openmw-lib rendering dependencies still need extraction before production
headless packaging. Whole-baseline provenance debt remains; only relevant
patch-registry test entries changed.
