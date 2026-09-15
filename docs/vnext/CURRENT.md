# Current state and next action

## Handoff

- **Direction:** OpenMW-backed authoritative cooperative multiplayer; see
  [README.md](README.md) and [DECISIONS.md](DECISIONS.md).
- **Milestone:** M2 in [PLAN.md](PLAN.md). Fully resolved non-gold MISC pairs now
  support a persistence-gated **disposable-fixture commit**, in addition to
  preparation, reversible rehearsal and detached save/restore/save. The sink is
  a test double. No production durability or live atomic transfer exists.
- **Next action:** implement a test-only bounded binary save codec for this
  accepted inventory pair. Wrap the complete owned ObjectStates and separate
  instance identities in an explicit format/runtime/content and source-owner/
  destination-owner/initiator identity envelope. Reuse OpenMW field serialization;
  encode no pointers or service/iterator storage. Validate lengths, membership,
  identities and bindings before publishing decoded output. Prove
  commit -> owned bytes -> fresh detached restore -> save, malformed/truncated/
  oversized/mismatched input rejection, allocation failures and unchanged prior
  outputs. Keep durable files, live restore installation, production command
  callers, notifications and script execution deferred. This is the payload
  boundary needed before choosing/wiring M2's durable file sink.
- **Checkpoint:** `8850e745c298c6de629ef9a5a26bbdddf6aa56e3` preserves the working
  gameplay implementation before the engine-backed pivot.

## Implemented M2 slice

[DisposableTransferRehearsal](../../apps/tes3mp-server/native/transfer_rehearsal.cpp)
consumes a protected pair and stages its complete owned save, receiving inventory
selections, script cursor positions and registry revision/counter. Complete
current-witness validation finishes before the synchronous test sink. Proposed
IDs are assigned only to detached owned nodes before the sink; no fixture state
changes until acceptance. Decline or exception discards the consumed pair and
preserves the entire fixture.

After acceptance, a nonthrowing installation exchanges both inventories, each
shared/distinct script service once, and the registry. It installs prepared
selections/cursors, invalidates caches and removes cached old references. End
cursors resolve against the receiving list after swap. Old nodes are detached
from the WorldModel before ordered pair destruction, avoiding deregistration of
installed replacements. No callback, allocation, validation, notification or
script execution follows acceptance. All composition remains in the test target;
production preparation and its ownership/storage/lifetime/iterator guards remain
unchanged.

The [commit filter](../../apps/tes3mp-server/native/transfer_rehearsal_tests.cpp)
checks **48** plain/scripted, existing/empty-destination, full/partial-removal,
shared/distinct-service and cursor combinations. Dormant nodes/selections, signed
restocking counts, engine locals/activation flags/animation and numeric edge values
are covered. Independent prepared engine copies and intended quantities check
committed results; the installed save matches the accepted save, which restores
into fresh storage and saves again. Old-node witnesses expire; installed nodes
survive pair consumption and expire at fixture destruction.

Consumed reuse, retired/current stale witnesses, incomplete resolution, corrupted
bindings/storage and reconstructed inventory/script nodes reject before the sink.
**144** explicit sink decline/exception checks preserve state. Every observed
allocation through preparation, serialization, final validation and sink copying
is failed individually: **30,182** failures, with successful commits on the same
preserved fixtures afterward. The next allocation ordinal never fires through
accepted installation and complete destruction. Installation/retirement allocate
**0**; failure and success cleanup leave **0** tracked blocks. Supplied stores,
independent WorldModel, listeners and script execution remain unchanged.

## Fresh verification

Windows MSVC 14.51 (`scripts/setup_msvc_env.ps1 -PreferLatest`), RelWithDebInfo,
`build/vnext-product`, individually, final exit **0**:

- `tes3mp_native_loadout_tests` build and focused rebuilds.
- `inventory-transfer-commit`: **48** cases, **30,182** allocation failures.
- `inventory-transfer-restore`: **218** rejections, **2,588** allocation failures.
- `inventory-transfer-locals-restore`: **837** allocation failures.
- `inventory-transfer-object-state`: **6,219** allocation failures.
- `inventory-transfer-serialization`: **5,273** allocation failures.
- `inventory-transfer-preparation` and `inventory-transfer-rehearsal`.
- `inventory-transfer-preparation-allocations`: **13,970** failures; peak **237**.
- `inventory-transfer-rehearsal-allocations`: **6,998** failures; exchange/rollback **0**.
- Documentation budgets/links, registry semantic fields and touched-file formatting.

Logs use `build/logs/native-inventory-commit-`. The new filter initially exited
**1** on unchanged fixture storage, then **1** on committed script/cursor state.
The stale-input test's string-buffer restoration and swapped-list end handling
were fixed; the same filter reran **0** before the remaining filters. Formatting
also exited **1**, was corrected, and reran **0**. No complete suites, expensive
gates or upstream baseline tests ran.

## Remaining limits and inherited evidence

The owned save has no binary format/content/owner envelope yet. Restoration builds
detached inventories; it does not reconstruct services, registry or selections.
Scene state, content-deletion state and postponed physics are not serialized.
Unresolved stores, equipment, gold/other types, Lua/custom state and stable
multiplayer mapping remain outside scope. Borrowed dependencies must outlive use;
the sink contract forbids mutation/reentry and assumes serialized engine access.
Allocation tracking excludes direct C allocation, other threads and private
external allocators. M1 real-content/enchantment evidence was not rerun; TR
remains unverified. Networking and the migration base are unchanged. Broad
openmw-lib dependencies still require extraction for headless packaging.
Whole-baseline provenance debt remains; only relevant registry entries changed.
