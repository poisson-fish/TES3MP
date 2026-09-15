# Current state and next action

## Handoff

- **Direction:** OpenMW-backed authoritative cooperative multiplayer; see
  [README.md](README.md) and [DECISIONS.md](DECISIONS.md).
- **Milestone:** M2 in [PLAN.md](PLAN.md). The protected non-gold MISC transfer
  pair now reports resolution completeness for its entire prepared registry and
  both LocalScripts lists. Installation/effects remain deferred; no atomic
  transfer exists.
- **Next action:** add a reversible, effect-free installation rehearsal for a
  fully resolved MISC pair on disposable stores/services only. Validate before
  exchanging inventory, script and registry storage; retain original nodes,
  identities, selections, cursors and registry revision/counter for exact
  rollback. Reject incomplete/stale pairs before mutation. Keep production
  installation, durability and notification execution unavailable; a rehearsal
  must not be claimed as a live atomic transfer.
- **Checkpoint:** `8850e745c298c6de629ef9a5a26bbdddf6aa56e3` preserves the working
  gameplay implementation before the engine-backed pivot.

## Implemented M2 slice

[ContainerStore::validateTransfer](../../apps/openmw/mwworld/containerstore.cpp)
returns the pair's protected read-only
[ResolutionCompleteness](../../apps/openmw/mwworld/containerstore.hpp) result
only after full current validation. `getResolutionCompleteness()` exposes the
saved snapshot; neither a borrowed view nor a copied result independently proves
freshness or authorizes installation/effects.

Counts come from every validated prepared registry mapping and script entry,
including dormant inventory nodes and unaffected entries. Shared LocalScripts
services contribute their combined result once; distinct services remain in
source-then-destination order, including empty lists. Registry-only and
script-only unresolved entries independently prevent completeness.

One 16-entry diagnostic cap covers registry identity order followed by service
and stock script-list order. Diagnostics retain compare-only registry bindings
or immutable script registration identities and positions. Counting continues
after the cap; explicit truncation never turns partial resolution into complete
resolution. No unresolved key is followed and no additional object is resolved.

The result names the exact private iterator bindings, contexts and coalesced
supplied-store collection. Validation recomputes it after checking the pair's
quantity, inventories, detached values, separate proposed identities, selections,
scripts/cursors, registry storage/results/revision/counter, context ownership,
supplied-store lifetimes/storage/values and deferred consumers. Corrupted counts,
service order, diagnostics, truncation and foreign/discarded pair bindings reject.
Storage readers establish current context registry ownership and identity before
accessing saved context references; owned-node and iterator guards remain intact.

The result is built before the final fallible consumer copy. Failure discards it
with the pair; moves preserve its address and storage associations. Moved-from
access rejects. No live removal, deregistration, registration, installation,
effect execution, production mutation caller or gameplay authority changed.

The existing supplied-store bound remains 16 inputs before alias coalescing.
Only explicit owners/initiator, both owned inventory projections and supplied
non-gold MISC nodes resolve. Gold, other types and unsupplied stores remain
unresolved; completeness does not broaden this policy.

## Fresh verification

Windows MSVC 14.51 (`setup_msvc_env.ps1 -PreferLatest`), RelWithDebInfo,
`build/vnext-product`, individually:

- `tes3mp_native_loadout_tests` focused build: exit 0.
- `inventory-transfer-preparation`: exit 0.
- `inventory-two-owners`: exit 0.
- Documentation budget/links, patch-registry semantic fields, formatting and
  whitespace checks: exit 0.

Synthetic disposable-stock comparisons now check completeness alongside all
existing inventory, script and registry comparisons. Exact isolated fixtures
cover complete/partial results, empty/shared/distinct services, registry-only
and script-only unresolved keys, cap boundaries and truncation, expired script
lifetimes at retained addresses, report corruption, stale values, moves, discard
and final-consumer-copy failure. Existing collection, ownership, storage,
registration, lifetime, cursor and failure cases still pass. Snapshots preserve
live inventories, flags/locals, selections, scripts/cursors, registry mappings/
revision/counter and notifications. No Environment, World, UI or Lua runtime
initialized.

The first transfer check failed because a new fixture freed script allocations
that a prepared registration could reuse. Retaining those allocations while
expiring the original lifetimes corrected the fixture. Final build and transfer
logs: `build/logs/native-completeness-*-retry.log`; other checks use the same
prefix. No complete suites, expensive gates or upstream baseline tests ran.

## Remaining limits and inherited evidence

Content, WorldModel, LocalScripts, transfer stores and cell services remain
borrowed and must outlive use. Supplied-store/reference expiry rejects without
pinning objects. Checks assume serialized engine access and witness current
state/lifetime, not complete mutation history or stable multiplayer identity.
Diagnostic and collection caps do not bound existing engine inventory or registry
size. Read-only views require full current validation and expire with their state
or referenced lifetime.

Installation, durability and effect execution remain unprepared. No atomic
transfer, installation-failure or allocator-fault proof exists. Unresolved stores,
equipment, gold/other types, Lua/custom-state transfer, persistence and stable
multiplayer mapping remain outside this slice.

Inherited M1 real-Morrowind/enchantment evidence under `build/native-loadout/real`
and `build/native-loadout/parity` was not rerun; TR remains unverified. Independent
networking/standalone targets and the migration base are unchanged. Broad
openmw-lib rendering dependencies still need extraction before production
headless packaging. Whole-baseline provenance debt remains; only the relevant
patch-registry entry changed.
