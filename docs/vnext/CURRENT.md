# Current state and next action

## Handoff

- **Direction:** OpenMW-backed authoritative cooperative multiplayer; see
  [README.md](README.md) and [DECISIONS.md](DECISIONS.md).
- **Active milestone:** M1 in [PLAN.md](PLAN.md), still incomplete. Native loadout
  enumeration, owned sampling, and a disposable plain-item inventory probe work.
- **Next action:** separate [MWScript::Locals](../../apps/openmw/mwscript/locals.cpp)
  `configure`/`setVar` and [LocalScripts::add](../../apps/openmw/mwworld/localscripts.cpp)
  from Environment's ScriptManager. Pass the actual script service explicitly;
  preserve engine compiler-derived declarations and existing global-script local
  initialization. Wire it into `ContainerStoreAddContext`, then make the native
  probe add a scripted MISC item with declared OnPCAdd. Verify actual local
  initialization, registration, and OnPCAdd assignment; distinguish those from
  executing script instructions. Keep stock OpenMW on shared behavior. Do not
  start protocol redesign or replace compiler behavior with TES3 precompiled locals.
- **Checkpoint:** `8850e745c298c6de629ef9a5a26bbdddf6aa56e3` preserves the working
  gameplay implementation before the engine-backed pivot.

## Implemented native slice

The opt-in [native targets](../../apps/tes3mp-server/native/CMakeLists.txt) remain
excluded from default builds and outside the independent server/standalone graph.
Existing server, desktop, baker, and canonical-gameplay callers remain the working
migration base. No `components/tes3mp` interfaces changed.

[Loadout](../../apps/tes3mp-server/native/loadout.hpp) retains OpenMW's configured
records, encoding, and readers. ConfigurationManager, Collections, EsmLoader,
ESMStore setup/validation, winning-record enumeration, and bounded owned `--sample`
remain available. TES4 is rejected; `.omwscripts` paths are retained without execution.

`--inventory ID` now runs [inventory.cpp](../../apps/tes3mp-server/native/inventory.cpp):
load a configured MISC record, construct ManualRefs, add two then one copies using
actual ContainerStore behavior, verify one stack and WorldModel lookup, record two
presentation requests, and verify deregistration after container destruction.
Gold piles use engine `gold_001` normalization. IDs are limited to 256 bytes without
control characters, counts are fixed, and effective record weight is validated.
The bounded diagnostic report is staged until the disposable operation completes.

[ContainerStore::add](../../apps/openmw/mwworld/containerstore.cpp) shares its
mutation path between stock callers and an explicit context containing the store,
registry, player/owner, local-script service, and presentation consumer. Stacking
uses an explicit ESMStore; InventoryStore retains its equipment-aware override.
WorldModel has an explicit cache-size constructor, avoiding Settings initialization.
Stock entry points retain their real services and equipment dispatch.

The probe initializes engine classes, WorldModel with a one-entry cache, ManualRef,
and a base ContainerStore. Its player reference provides identity only; this is not
a live actor InventoryStore. No Environment, World, renderer, UI, audio, LocalScripts,
ScriptManager, Lua runtime, or NPC custom data is initialized.

Absent script services reject before inventory mutation, including scripted
`gold_001` reached through normalization. Scripted items are **not supported yet**;
OnPCAdd is not silently skipped. Explicit-context calls also reject derived stores,
unresolved container contents, and missing presentation consumers. This is an
offline probe, not transactional server mutation; arbitrary service/allocation or
device failures do not establish rollback guarantees.

## Fresh verification

Windows MSVC RelWithDebInfo in `build/vnext-product`, checks run individually:

- Builds of `tes3mp_native_loadout_tests` and `tes3mp_native_loadout_probe`: exit 0.
- `inventory-plain`: exit 0; OpenMW-written/loadable fixtures, insertion/stacking,
  gold normalization/weight, registry lifetime, unchanged retained records.
- `inventory-rejection`: exit 0; declared OnPCAdd script, normalized scripted gold,
  missing presentation service, derived-store rejection, copied metadata cleanup,
  and unchanged items/source/registry/listeners/publication on rejected adds.
- `layered`: exit 0; retained configuration/override/deletion/normalization path.
- Formatting, touched provenance/patch coverage, documentation budget and local
  links: exit 0. No complete suites, upstream baseline tests, or live clients ran.

Logs: `build/logs/native-inventory-*.log`. Real installed Morrowind.esm:

```text
build/vnext-product/tes3mp_native_loadout_probe.exe --config build/native-loadout/real --replace config --inventory misc_com_bottle_01
```

Exit 0: three bottles, one stack, weight 3, two presentation requests, registration
and cleanup verified; 0.566 seconds wall time, 94,044,160 bytes sampled peak working
set (about 90 MiB). `artifact_bittercup_01` correctly exits 1 with the missing
LocalScripts/ScriptManager/OnPCAdd diagnostic and zero report bytes; its capture
check exits 0. Reports are `build/logs/native-inventory-real*.tsv`; config/content/
executable hashes, timing, and observed DLLs are in
`build/native-loadout/real/inventory-*-evidence.json`. Reproduction capture:
`build/native-loadout/capture-inventory.ps1`.

Prior real enumeration and 60-record sample evidence remains under
`build/native-loadout/real`; the real sample was not rerun here.

## Remaining limits

The generated `loadout-dependencies-RelWithDebInfo.txt` records broad
`openmw-lib`/`components` OSG/OpenGL/MyGUI edges. Replace these with extracted
content/runtime libraries before production headless packaging. Linked DLLs do
not establish initialized services. Tamriel Rebuilt, script execution, equipment,
calculation parity, and engine-backed authority remain unverified/unfinished.
Client-authoritative positions and migration worlds remain. Inherited whole-baseline
provenance debt remains; only touched entries were updated.
