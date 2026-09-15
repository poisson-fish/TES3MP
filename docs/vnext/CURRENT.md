# Current state and next action

## Handoff

- **Direction:** OpenMW-backed authoritative cooperative multiplayer; see
  [README.md](README.md) and [DECISIONS.md](DECISIONS.md).
- **Milestone:** M2 in [PLAN.md](PLAN.md). Fully resolved non-gold MISC pairs now
  support persistence-gated disposable-fixture commit through a **test-only
  bounded binary codec**, followed by fresh detached restore/save. No durable
  file adapter, production durability or live atomic transfer is implemented.
- **Next action:** implement a test-only synchronous durable byte-file sink for
  this codec, composed with disposable-fixture commit in an isolated scratch
  directory. Inspect [the existing file adapter](../../apps/tes3mp-server/canonical_persistence_file.cpp)
  before choosing the concrete app-local write/flush/replace protocol; reuse
  suitable file mechanics without adopting its independent gameplay schema.
  Bound reads before allocation. Distinguish safe pre-acceptance rejection from
  post-replacement uncertainty, which must fail closed. Prove accepted bytes
  survive fresh reopen/decode/detached restore/save, and injected file/allocation
  failures preserve coherent prior/new files, fixture state and acceptance rules.
  Keep live restore installation, production callers, notifications and script
  execution deferred. This chooses and proves M2's file sink before live wiring.
- **Checkpoint:** `8850e745c298c6de629ef9a5a26bbdddf6aa56e3` preserves the working
  gameplay implementation before the engine-backed pivot.

## Implemented M2 slice

[The codec](../../apps/tes3mp-server/native/transfer_save_codec.cpp) is linked only
into `tes3mp_native_loadout_tests`. Its explicit version-1 envelope binds OpenMW
save format 37, a caller-supplied runtime identity, a 32-byte content identity,
source/destination owners and initiator. Ordered source/destination records carry
complete supported ObjectStates and separate unique instance identities. No raw
objects, pointers, services, registry storage, selections or iterators are encoded.

OpenMW ObjectState/CellRef/locals/animation serializers write the engine fields.
Explicit extensions preserve scale outside stock clamps, fractional charge,
inactive teleport/lock fields, destination/key state, position float bits and
nonpositive animation times. Stock save behavior is unchanged. Modern wire state
normalizes to the existing detached ObjectState convention after version checks.

Allocation-free byte preflight checks envelope, complete record membership,
unique identities, owner collisions, framing, known field sizes, booleans and
nested limits before constructing the engine reader. Caps are 8 MiB total,
256 KiB/object, 1,024 objects/inventory, 4,096-byte text, 1,024 locals and 256
animations/object. RefIds must match supplied already-interned content IDs;
foreign names cannot pollute OpenMW's global pool. Shared restore validation
checks complete supplied base/script/locals bindings and supported semantic state.
Canonical re-encoding rejects duplicate, reordered or inconsistent fields. Both
encoder bytes and decoded inventories publish only through nonthrowing swaps.

The [codec filter](../../apps/tes3mp-server/native/transfer_rehearsal_tests.cpp)
checks 48 plain/scripted, existing/empty-destination, full/partial-removal,
shared/distinct-service and cursor combinations. Independent prepared engine
copies verify values and quantities. Commit encodes before sink acceptance;
accepted installation/retirement still allocate zero. Retained bytes restore into
fresh detached storage and save identically, including after fixture destruction.
Dormant nodes, signed restocking counts, locals, activation flags and numeric edge
values survive. Invalid input and injected failures preserve prior output values
and storage, input, fixtures, supplied content and unrelated WorldModel state.
Cleanup leaves zero tracked blocks; preserved/fresh retries succeed. Production
preparation and ownership/storage/lifetime/iterator guards remain unchanged.

## Fresh verification

Windows MSVC 14.51 (`scripts/setup_msvc_env.ps1 -PreferLatest`), RelWithDebInfo,
`build/vnext-product`, individually, final exit **0**:

- `tes3mp_native_loadout_tests` build/rebuilds.
- `inventory-transfer-codec`: **48** cases, **3,344** malformed/truncated/oversized/
  mismatched rejections, **33,232** commit-path and **17,600** codec allocation
  failures, including rejection paths; installation/retirement/remaining blocks **0**.
- `inventory-transfer-commit`: **48** cases, **30,182** allocation failures.
- `inventory-transfer-restore`: **218** rejections, **2,588** allocation failures.
- `inventory-transfer-object-state`: **6,219** allocation failures.
- `inventory-transfer-serialization`: **5,273** allocation failures.
- Documentation budgets/links, registry semantic fields and touched-file formatting.

Logs use `build/logs/native-inventory-codec-`. Codec initially exited **1** on a
mutation targeting an absent short-local wire tag, then **1** on an old test
assuming every destination cell has a name. Tests now target the actual integer
local field and compare OpenMW's derived destination-cell identity. The same
filter reran **0** before subsequent filters. No complete suites, expensive gates
or upstream baseline tests ran.

## Remaining limits and inherited evidence

This is a little-endian development format with synthetic caller-supplied content/
runtime identities, not a production fingerprint or authenticated save. Detached
restoration reconstructs no services, registry or selections. Scene/content-deletion
state and postponed physics remain absent. Equipment, gold/other types, Lua/custom
state and stable multiplayer mapping remain outside scope. Borrowed content must
outlive use; the sink forbids mutation/reentry and assumes serialized engine access.
Allocation tracking excludes direct C allocation, other threads and private
external allocators. M1 real-content/enchantment evidence was not rerun; TR remains
unverified. Networking and migration base are unchanged. Broad openmw-lib
headless dependencies and whole-baseline provenance debt remain.
