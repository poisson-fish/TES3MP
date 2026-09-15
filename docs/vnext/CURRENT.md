# Current state and next action

## Handoff

- **Direction:** OpenMW-backed authoritative cooperative multiplayer; see
  [README.md](README.md) and [DECISIONS.md](DECISIONS.md).
- **Milestone:** M2 in [PLAN.md](PLAN.md). The protected MISC transfer pair now
  resolves unaffected source-owner, destination-owner and initiator bindings from
  explicit contexts. Other unaffected entries stay unresolved; installation and
  effects remain deferred. No atomic transfer exists.
- **Next action:** prepare lifetime-checked resolution of unaffected non-gold MISC
  registry/script bindings from one explicitly supplied, resolved third base
  ContainerStore. Bind its registered owner, storage and current nodes to the same
  protected pair; leave all remaining entries unresolved and installation/effects
  deferred.
- **Checkpoint:** `8850e745c298c6de629ef9a5a26bbdddf6aa56e3` preserves the working
  gameplay implementation before the engine-backed pivot.

## Implemented M2 slice

[ContainerStore::prepareTransfer](../../apps/openmw/mwworld/containerstore.cpp)
returns one move-only `PreparedContainerTransfer`. Its context bindings name the
exact private iterator binding object, source owner, destination owner and optional
initiator, with their original identities and read-only Ptr views. Aliased roles
resolve the same reference; absent initiators add no binding. Both shared and
distinct script services retain their stock registration order and cursors.

[Ptr](../../apps/openmw/mwworld/ptr.hpp) now captures a weak lifetime witness when
constructed from a current reference. Copies, including conversion to ConstPtr,
preserve that witness without touching the reference. Reference copy/move
construction creates a different lifetime; assignment preserves the existing
object's lifetime. [LiveCellRef](../../apps/openmw/mwworld/livecellref.cpp)
invalidates witnesses before deregistration and RefData teardown. Tokens are lazy,
private to reference ownership, and cannot keep references alive. This adds one
lifetime token per referenced node; it is separate from the existing opt-in
prepared-node iterator identities.

Preparation establishes context liveness before reading saved pointers, then
checks reference identity, WorldModel ownership, exact registry Ptr/lifetime/cell
bindings, and LocalScripts membership, registration identity, script/locals identity
and cell/container ownership. It repeats lifetime checks after fallible script and
consumer preparation. A registry match or reused address alone cannot establish
liveness. Unchanged script registrations consider only explicit contexts;
relocated registration identities select owned inventory nodes, so unrelated stale
keys cannot resolve merely by reusing an owned address.

Joint validation still binds removal quantity, both inventories and selections,
original/proposed item values, script membership/results/storage/cursors, registry
membership/results/storage/revision/generated counter, contexts and deferred
notifications to one pair. Corrupted, expired, replaced, foreign and inconsistent
bindings reject before publication. Registry/script item access rejects expired
references. Full validation remains necessary to check current live state.

Existing stock inventory owners, owned-node identities and guarded selection/script
iterators remain intact. Detached item values own their RefData; proposed identities
remain separate and no live RefNum is assigned. Dormant nodes, signed counts,
new-stack append, in-place replacement, raw selections and stock zero-count skipping
remain covered. Moves preserve the pair's nodes, owners and sentinels; moved-from
access rejects. No production mutation caller or gameplay authority changed.

## Fresh verification

Windows MSVC 14.51 (`setup_msvc_env.ps1 -PreferLatest`), RelWithDebInfo,
`build/vnext-product`, individually:

- `tes3mp_native_loadout_tests` focused build: exit 0.
- `inventory-transfer-preparation`: exit 0.
- `inventory-two-owners`: exit 0.
- Documentation budget/links, patch-registry semantic fields, formatting and
  whitespace checks: exit 0.

Synthetic disposable-stock comparisons cover shared/distinct services, scripted
owners, context aliases, absent/distinct initiators, corrupt lifetime witnesses,
expired/reconstructed references, stale/inconsistent registrations, consumer-copy
failure/destruction, moves and discard. Snapshots preserve live inventories,
RefData flags/locals, scripts/cursors, selections, WorldModel mappings/revision/
counter and notifications. Reference copy/move/assignment and expiry before RefData
teardown also pass. No Environment, World, UI or Lua runtime initialized.

Logs: `build/logs/native-context-bindings-*`. No complete suites, expensive gates
or upstream baseline tests ran.

## Remaining limits and inherited evidence

Content, WorldModel, LocalScripts, live stores and cell services remain borrowed;
they must outlive their use. Reference expiry rejects without pinning objects.
Checks assume serialized engine access and witness current state/lifetime, not
complete mutation history or stable multiplayer identity. Public views expire with
their owning state or referenced lifetime.

Other unaffected references, installation, durability and effect execution remain
unprepared. No atomic transfer, installation-failure or allocator-fault proof exists.
Unresolved stores, equipment, gold/other types, Lua/custom-state transfer,
persistence and stable multiplayer instance mapping remain outside this slice.

Inherited M1 real-Morrowind/enchantment evidence under `build/native-loadout/real`
and `build/native-loadout/parity` was not rerun; TR remains unverified. Independent
networking/standalone targets and the migration base are unchanged. Broad
openmw-lib rendering dependencies still need extraction before production
headless packaging. Whole-baseline provenance debt remains; only touched
patch-registry entries changed.
