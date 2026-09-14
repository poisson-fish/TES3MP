# Current state and next action

## Handoff

- **Direction:** OpenMW-backed authoritative cooperative multiplayer; see
  [README.md](README.md) and [DECISIONS.md](DECISIONS.md).
- **Active milestone:** M1 in [PLAN.md](PLAN.md), still incomplete. Native loadout
  enumeration, owned sampling, and plain/scripted MISC inventory initialization work.
- **Next action:** separate the content/GMST access in
  [spellutil.cpp](../../apps/openmw/mwmechanics/spellutil.cpp) `calcEffectCost`,
  `getTotalCost`, and `getEnchantmentCharge` into explicit store/settings inputs,
  retaining shared stock behavior. Add a bounded autocalculated enchantment-cost/
  charge probe. Compare with normal OpenMW using the same loadout and a changed
  `fEffectCostMult`; two calls to the same helper are not parity evidence. Keep
  M1 incomplete until that comparison is observed; do not start protocol redesign.
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

`--inventory ID` in [inventory.cpp](../../apps/tes3mp-server/native/inventory.cpp)
now initializes the actual MWScript ScriptManager, engine compiler context/extensions,
and LocalScripts. ScriptManager's QuickFileParser derives declarations from source;
TES3 precompiled local metadata is not substituted. No script instructions run.

[Locals](../../apps/openmw/mwscript/locals.cpp),
[RefData](../../apps/openmw/mwworld/refdata.cpp), and
[LocalScripts](../../apps/openmw/mwworld/localscripts.cpp) accept the actual script
service explicitly. Stock callers retain Environment wrappers over shared behavior.
Global-script creation also has an explicit service overload; new local instances
still inherit existing global locals, including stopped scripts. Re-registration
preserves initialized values. ContainerStoreAddContext supplies both script services,
and the shared add path assigns declared OnPCAdd after script registration for the
explicit player owner.

The disposable operation adds two then one MISC copies using actual ContainerStore
behavior: plain items stack; scripted items stay in separate stacks; normalized gold
retains its engine stacking exception. Version 2 diagnostics verify counts, weight,
WorldModel registration/destruction, compiler-derived locals, script registration/
removal, OnPCAdd assignment, and two presentation requests. Publication is staged.
Script text is capped at 64 KiB before compiler scanning and locals at 256 before
inventory mutation. Existing ID/weight validation and missing-service rejection remain.

The probe constructs no Environment, World, renderer, UI, audio, Lua runtime, NPC
custom data, or actor InventoryStore. Its player reference supplies identity only.
Compiler live-world queries, global script startup, and opcode execution are unused.
This offline operation does not establish transactional rollback for arbitrary
service/allocation failures or durable publication; derived stores and unresolved
container contents remain excluded.

## Fresh verification

Windows MSVC RelWithDebInfo in `build/vnext-product`, each check run individually:

- Builds of `tes3mp_native_loadout_tests` and `tes3mp_native_loadout_probe`: exit 0.
  Initial test build exited 2 for a missing Script include and const fixture store;
  both were fixed before rerunning that target and proceeding.
- `inventory-scripted`: exit 0; OpenMW-written/loadable fixtures with no precompiled
  declarations, all three local types, OnPCAdd without instruction execution,
  normalized scripted gold, non-player ownership, stopped-global inheritance,
  repeated registration, and cleanup.
- `inventory-rejection`: exit 0; absent/either missing script service, normalized
  scripted gold, source/registry/listener/presentation preservation, derived-store
  rejection, copied metadata cleanup, and script text/local limits without partial reports.
- `inventory-plain`: exit 0; insertion/stacking, gold weight, registry lifetime,
  and unchanged retained records.

Formatting, diff whitespace, touched provenance/patch coverage, documentation
budget and local links: exit 0.

Logs: `build/logs/native-script-*.log`. Real installed Morrowind.esm reproduction:

```text
build/native-loadout/capture-inventory.ps1 -Item artifact_bittercup_01 -Label script-bittercup
build/native-loadout/capture-inventory.ps1 -Item misc_dwrv_ark_cube00 -Label script-puzzle
```

Both captures/probes exited 0: three copies, two stacks, two registered/removed
scripts. Bittercup initialized 18 shorts/one float; puzzle box one short. Neither
script declares OnPCAdd; its assignment is synthetic evidence only. Wall times were
0.527/0.483 seconds; sampled peak working sets 94,027,776/91,709,440 bytes.
Reports: `build/logs/native-inventory-script-*.tsv`; hashes, timing, observed DLLs:
`build/native-loadout/real/inventory-script-*-evidence.json`.

## Remaining limits

M1 calculation parity, script execution, equipment, engine-backed authority, and
Tamriel Rebuilt remain unverified/unfinished. Prior real enumeration/sample evidence
remains under `build/native-loadout/real`; it was not rerun here. No complete suites,
upstream baseline tests, or live clients ran. Client-authoritative positions and
migration worlds remain.

The generated `loadout-dependencies-RelWithDebInfo.txt` records broad
`openmw-lib`/`components` OSG/OpenGL/MyGUI edges. Replace them with extracted
content/runtime libraries before production headless packaging. Linked DLLs do not
establish initialized services. Inherited whole-baseline provenance debt remains;
only touched entries were updated.
