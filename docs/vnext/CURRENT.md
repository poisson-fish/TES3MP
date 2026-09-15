# Current state and next action

## Handoff

- **Direction:** OpenMW-backed authoritative cooperative multiplayer; see
  [README.md](README.md) and [DECISIONS.md](DECISIONS.md).
- **Milestone:** M2 in [PLAN.md](PLAN.md). The protected MISC transfer pair now
  optionally resolves the registered owner and unaffected non-gold MISC nodes of
  one explicitly supplied, resolved third base ContainerStore. Other unaffected
  entries stay unresolved; installation/effects remain deferred. No atomic transfer
  exists.
- **Next action:** generalize the single optional third-store witness to a bounded
  explicitly supplied collection of resolved base ContainerStores. Validate the
  entire collection before publishing any pair; coalesce identical aliases and
  reject inconsistent aliases or overlap with the transfer owners/stores. Resolve
  only supplied owners and non-gold MISC nodes, leaving other entries unresolved
  and installation/effects deferred.
- **Checkpoint:** `8850e745c298c6de629ef9a5a26bbdddf6aa56e3` preserves the working
  gameplay implementation before the engine-backed pivot.

## Implemented M2 slice

[ContainerStoreResolution](../../apps/openmw/mwworld/containerstore.hpp) captures
an explicitly supplied current store, its owner Ptr and storage identity, plus a
weak store-lifetime witness. The lazy lifetime is separate from retained storage
tokens; neither an owner reference nor a storage token can establish store liveness.
Store destruction invalidates it before node/RefData teardown. Store copy/move
construction starts another lifetime; assignment retains lifetime but replaces
storage identity.

[ContainerStore::prepareTransfer](../../apps/openmw/mwworld/containerstore.cpp)
checks this witness before reading the saved store pointer. It verifies the
registered owner, exact WorldModel ownership/binding and resolved base-store type,
then acquires current non-gold MISC nodes, including dormant nodes and signed
counts. Source/destination owner or store aliases reject; the third owner may
alias the initiator. Gold and other item types remain unresolved.

The pair owns detached original value witnesses for those nodes and separately
exposes borrowed read-only owner/node bindings. Third-store membership, reference
lifetimes, identities, values and raw selection are checked against current storage.
LocalScripts verifies registration identity, Ptr lifetime, script/locals identity
and container ownership, allowing stock owner-cell or null script hints. Shared
and distinct services retain their stock registration order and cursors.

Resolution is rechecked after fallible script preparation and destination consumer
copying, and after the final source consumer copy. Stale, destroyed, reconstructed,
replaced, foreign or inconsistent bindings reject without publishing a pair.
Third-store bindings name the exact private iterator-binding object, tying them to
quantity, both inventories, detached/proposed values and identities, selections,
scripts/cursors/storage/results, registry membership/storage/results/revision/
counter, explicit contexts and deferred notifications.

Existing owned-node and iterator guards remain intact. Moves preserve the pair's
stable nodes, owners and sentinels; moved-from access rejects. No live removal,
deregistration, registration, installation, effect execution, production mutation
caller or gameplay authority changed.

## Fresh verification

Windows MSVC 14.51 (`setup_msvc_env.ps1 -PreferLatest`), RelWithDebInfo,
`build/vnext-product`, individually:

- `tes3mp_native_loadout_tests` focused build: exit 0.
- `inventory-transfer-preparation`: exit 0.
- `inventory-two-owners`: exit 0.
- Documentation budget/links, patch-registry semantic fields, formatting and
  whitespace checks: exit 0.

Synthetic disposable-stock comparisons cover third-store owner/initiator aliases,
shared/distinct services, signed and dormant nodes, gold exclusion, corrupted
bindings/lifetimes/values/selections, stale scripts and registrations, store
copy/move replacement, same-address store/node reconstruction, owner/store
destruction, consumer-copy failure/destruction, pair moves and discard. Snapshots
preserve live inventories, RefData flags/locals, scripts/cursors, selections,
WorldModel mappings/revision/counter and notifications. No Environment, World, UI
or Lua runtime initialized.

Logs: `build/logs/native-third-store-*`; final C++ build/transfer evidence uses
`*-retry3.log`. No complete suites, expensive gates or upstream baseline tests ran.

## Remaining limits and inherited evidence

Content, WorldModel, LocalScripts, transfer source/destination stores and cell
services remain borrowed and must outlive use. Third-store/reference expiry
rejects without pinning objects. Checks assume serialized engine access and witness
current state/lifetime, not complete mutation history or stable multiplayer identity.
Read-only views expire with their owning state or referenced lifetime; full
validation remains necessary to check live state.

Multiple supplied stores, other unaffected references, installation, durability and
effect execution remain unprepared. No atomic transfer, installation-failure or
allocator-fault proof exists. Unresolved stores, equipment, gold/other types,
Lua/custom-state transfer, persistence and stable multiplayer mapping remain outside
this slice.

Inherited M1 real-Morrowind/enchantment evidence under `build/native-loadout/real`
and `build/native-loadout/parity` was not rerun; TR remains unverified. Independent
networking/standalone targets and the migration base are unchanged. Broad
openmw-lib rendering dependencies still need extraction before production headless
packaging. Whole-baseline provenance debt remains; only touched patch-registry
entries changed.
