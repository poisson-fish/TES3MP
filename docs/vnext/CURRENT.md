# Current state and next action

## Handoff

- **Direction:** OpenMW-backed authoritative cooperative multiplayer; see
  [README.md](README.md) and [DECISIONS.md](DECISIONS.md).
- **Milestone:** M2 in [PLAN.md](PLAN.md). The protected MISC transfer pair accepts
  an explicitly supplied collection of up to 16 resolved base ContainerStore
  witnesses. Identical aliases coalesce; the entire collection validates before
  publication. Other unaffected entries stay unresolved. Installation/effects
  remain deferred; no atomic transfer exists.
- **Next action:** add a protected resolution-completeness result for every
  prepared registry mapping and both LocalScripts lists. Derive unresolved counts
  and bounded compare-only diagnostics from validated storage, coalesce shared
  services, and reject stale/corrupted results. Do not follow unresolved keys or
  resolve more objects. Completeness must not authorize installation or effects.
- **Checkpoint:** `8850e745c298c6de629ef9a5a26bbdddf6aa56e3` preserves the working
  gameplay implementation before the engine-backed pivot.

## Implemented M2 slice

[ContainerStore::prepareTransfer](../../apps/openmw/mwworld/containerstore.cpp)
accepts a borrowed span of
[ContainerStoreResolution](../../apps/openmw/mwworld/containerstore.hpp) inputs.
The 16-input bound counts duplicates and rejects before preparation allocation.
Every supplied lifetime, owner and storage witness validates before node capture
or alias coalescing. Identical aliases retain first-occurrence order; inconsistent
aliases and overlap with either transfer owner/store reject. A supplied owner may
still alias the initiator. The pair owns copies of the coalesced witnesses.

Each store has a weak lifetime separate from its retained storage identity.
Current registered owner identity, exact WorldModel binding, resolved base-store
type and storage are checked before saved node access. Copy/move replacement,
destruction and same-address reconstruction reject. Only supplied owners and
non-gold MISC nodes resolve, including dormant nodes and signed counts. Gold,
other types and unsupplied stores remain unresolved.

The pair owns detached original node values and separately exposes borrowed
read-only owner/node bindings through `getResolvedStoreBindings()`. Entire
collection membership/order and each store's owner, nodes, lifetimes, values and
raw selection validate against current storage. LocalScripts checks registration
identity, Ptr lifetime, script/locals identity and container ownership, allowing
stock owner-cell or null hints. Shared/distinct services preserve stock order and
cursors.

Collection validation runs after fallible script preparation and destination
consumer copying, and again after the final source consumer copy. Each exposed
entry names the same private iterator-binding object, tying it to quantity, both
inventories, detached/proposed values and separate identities, selections,
scripts/cursors/storage/results, registry membership/storage/results/revision/
counter, explicit contexts and deferred notifications. Existing owned-node and
iterator guards remain intact. Moves preserve stable nodes and sentinels;
moved-from access rejects.

No live removal, deregistration, registration, installation, effect execution,
production mutation caller or gameplay authority changed.

## Fresh verification

Windows MSVC 14.51 (`setup_msvc_env.ps1 -PreferLatest`), RelWithDebInfo,
`build/vnext-product`, individually:

- `tes3mp_native_loadout_tests` focused build: exit 0.
- `inventory-transfer-preparation`: exit 0.
- `inventory-two-owners`: exit 0.
- Documentation budget/links, patch-registry semantic fields, formatting and
  whitespace checks: exit 0.

Synthetic disposable-stock comparisons cover empty/single/multiple collections,
16 distinct stores, duplicate inputs at/over the bound, alias ordering/conflicts,
shared/distinct services, signed/dormant nodes, gold exclusion, corrupted
collections/bindings/values/selections, stale scripts and registry witnesses,
later-entry failure, replacement/destruction, preparation failure, moves and
discard. Snapshots preserve inventories, RefData flags/locals, scripts/cursors,
selections, WorldModel mappings/revision/counter and notifications. No Environment,
World, UI or Lua runtime initialized.

Two test failures were corrected: the new compiler-lookup fault now exercises
player-destination OnPCAdd preparation, and an inherited expired-script assertion
uses immutable registration identity/order instead of an address that an owned
node can reuse. Final build/transfer logs: `build/logs/native-store-collection-*-retry2.log`;
other focused checks use the same prefix. No complete suites, expensive gates or
upstream baseline tests ran.

## Remaining limits and inherited evidence

Content, WorldModel, LocalScripts, transfer source/destination stores and cell
services remain borrowed and must outlive use. Supplied-store/reference expiry
rejects without pinning objects. Checks assume serialized engine access and
witness current state/lifetime, not complete mutation history or stable multiplayer
identity. Read-only views expire with their owning state or referenced lifetime;
full validation remains necessary. The collection bound does not bound existing
engine inventory or registry size.

Resolution completeness, installation, durability and effect execution remain
unprepared. No atomic transfer, installation-failure or allocator-fault proof
exists. Unresolved stores, equipment, gold/other types, Lua/custom-state transfer,
persistence and stable multiplayer mapping remain outside this slice.

Inherited M1 real-Morrowind/enchantment evidence under `build/native-loadout/real`
and `build/native-loadout/parity` was not rerun; TR remains unverified. Independent
networking/standalone targets and the migration base are unchanged. Broad
openmw-lib rendering dependencies still need extraction before production headless
packaging. Whole-baseline provenance debt remains; only relevant patch-registry
entries changed.
