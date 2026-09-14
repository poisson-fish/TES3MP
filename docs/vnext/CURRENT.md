# Current state and next action

## Handoff

- **Direction:** OpenMW-backed authoritative cooperative multiplayer; see
  [README.md](README.md) and [DECISIONS.md](DECISIONS.md).
- **Active milestone:** M1 in [PLAN.md](PLAN.md). Native static-loadout enumeration
  works; M1 is incomplete.
- **Next action:** add M1's bounded, owned diagnostic projection over the loaded
  engine records in [native/loadout.cpp](../../apps/tes3mp-server/native/loadout.cpp).
  Stage a sample with explicit field/count/byte limits, test malformed/oversized
  rejection without partial publication, and retain the engine store for the
  subsequent inventory/service-context probe. Do not start protocol redesign.
- **Latest maintenance:** inventory/object source-contract tests were reduced
  from 20 overlapping checks to six focused guards. Executable gameplay coverage
  and independent networking boundaries remain. No production code changed.
- **Checkpoint:** `8850e745c298c6de629ef9a5a26bbdddf6aa56e3` preserves the previous
  timed/area-magic implementation before the engine-backed pivot.

## Native loadout state

The opt-in [native targets](../../apps/tes3mp-server/native/CMakeLists.txt) build
`tes3mp_native_loadout_probe` and individually filtered
`tes3mp_native_loadout_tests`. They are excluded from default builds and absent
from the independent server/standalone graph. Existing server, desktop, baker,
and canonical-gameplay callers remain the working migration base.

[Loadout](../../apps/tes3mp-server/native/loadout.hpp) owns actual OpenMW records,
encoder, and cached readers. ConfigurationManager resolves configuration layers,
relative/token paths, composing/replacement settings, encoding, and ordered
content. Collections supplies case-insensitive data/data-local priority.
EsmLoader loads TES3 masters/plugins; ESMStore handles overrides, ignored/deleted
records, setUp, and validateRecords, including effect correction and cell keys.
The report enumerates winning identities and selected fields in twelve item
categories, spells, enchantments, and GMSTs.

No World, Environment, renderer, UI, audio, or Lua runtime is constructed. TES4
is rejected before its resource-dependent branch; `.omwscripts` paths are stored
without execution. Added globals and dynamic player initialization are outside
this slice. Load/normalization failures exit 1 without a record report; engine
diagnostics go to stderr. TSV is an offline diagnostic, not a bounded network
export or transactional publication format.

## Test cleanup and fresh verification

The [inventory guards](../../scripts/tests/test_inventory_contract.py) retain UI
interception ordering and release-active assertions. The
[object guards](../../scripts/tests/test_interactive_object_contract.py) retain
activation ordering, desktop door validation, and complete numeric mapping
parsing. Remaining source assertions report compact failures and explicitly do
not claim executable gameplay proof.

Engine/legacy dependency checks are consolidated in
[test_tes3mp_target_boundaries.py](../../scripts/tests/test_tes3mp_target_boundaries.py),
including the existing client/adapter legacy-type exclusions. CMake dependency
checks remain intact. Immutable player/container/world-item lookup types are
checked by static assertions in
[inventory_world_tests.cpp](../../components/tes3mp/tests/inventory_world_tests.cpp).
Removed filename/symbol/capability-array checks duplicated compilation and the
existing catalog, world, replication, reducer, server, and adapter tests; those
executable tests were retained. Other phase-test files were not retired here.

Windows MSVC RelWithDebInfo cleanup verification, 2026-09-13, all exit 0:

- Both `InventoryContractTests` methods and all three
  `InteractiveObjectContractTests` methods, each run individually.
- `TES3MPTargetBoundaryTests.test_inventory_and_object_migration_sources_remain_engine_independent`.
- Individual builds and runs of `tes3mp_inventory_world_tests` and
  `tes3mp_interactive_object_world_tests` in `build/vnext-product`.
- Documentation budget and local-link methods, run individually.

Logs are `build/logs/test-cleanup-*.log`. No complete suites, upstream baseline
checks, live clients, or broad gates ran. Native-loadout checks were not rerun
for this test-only cleanup.

## Retained loader evidence and limits

The prior native slice built both native targets and passed the separate
`layered`, `missing-content`, `master-order`, `truncated`, and `tes4` filters.
OpenMW ESMWriter fixtures exercise config priority/order, win1251 decoding,
overrides/deletion/ignored records, normalization, and failed-load publication.

The existing `build/tes3mp-vnext-openmw.cfg` selects installed Morrowind.esm;
there is no user openmw.cfg. The isolated real run found 3,352 items, 990 spells,
708 enchantments, and 1,449 GMSTs. Native elapsed time was 0.375 seconds; process
wall time 0.519 seconds; sampled peak working set 94,261,248 bytes (about 90 MiB).
Records, config/content hashes, timings, and observed DLLs remain under
`build/native-loadout/real`; original logs are `build/logs/native-loadout-*.log`.
CMake generates `build/vnext-product/apps/tes3mp-server/native/loadout-dependencies-RelWithDebInfo.txt`.

Reproduce with the MSVC environment and
`cmake --build build/vnext-product --target tes3mp_native_loadout_probe --parallel 2`,
then `build/vnext-product/tes3mp_native_loadout_probe.exe --config build/native-loadout/real --replace config`,
redirecting output to ignored build artifacts.

`openmw-lib`/`components` still bring broad OSG/OpenGL/MyGUI dependencies; replace
these edges with extracted content/runtime libraries before production headless
packaging. Tamriel Rebuilt, inventory operations, scripted/enchanted gameplay,
calculation parity, owned export limits, and engine-backed server authority are
unfinished. Client-authoritative positions and bounded migration worlds remain.
Inherited whole-baseline provenance debt remains; only touched entries were
updated. Resume native M1 work rather than the retired gameplay roadmap.
