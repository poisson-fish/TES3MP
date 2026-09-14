# Current state and next action

## Handoff

- **Direction:** OpenMW-backed authoritative cooperative multiplayer; see
  [README.md](README.md) and [DECISIONS.md](DECISIONS.md).
- **Active milestone:** M1, native loadout and headless runtime probe in
  [PLAN.md](PLAN.md). Native static-loadout enumeration now works; M1 is incomplete.
- **Next action:** add M1's bounded, owned diagnostic projection over the loaded
  engine records in [native/loadout.cpp](../../apps/tes3mp-server/native/loadout.cpp).
  Stage a sample with explicit field/count/byte limits, test malformed/oversized
  rejection without partial publication, and retain the engine store for the
  subsequent inventory/service-context probe. Do not start protocol redesign.
- **Checkpoint:** `8850e745c298c6de629ef9a5a26bbdddf6aa56e3` preserves the previous
  timed/area-magic implementation before the engine-backed pivot.

## Implemented slice

The opt-in [native targets](../../apps/tes3mp-server/native/CMakeLists.txt) build
`tes3mp_native_loadout_probe` and individually filtered
`tes3mp_native_loadout_tests`. They are excluded from default builds and absent
from the independent server/standalone graph. No existing server, desktop,
baker, or canonical-gameplay caller was replaced.

[Loadout](../../apps/tes3mp-server/native/loadout.hpp) owns actual OpenMW records,
encoder, and cached readers. ConfigurationManager resolves configuration layers,
relative/token paths, composing/replacement settings, encoding, and ordered
content. Collections supplies case-insensitive data/data-local priority.
EsmLoader performs TES3/master loading; ESMStore handles ignored/deleted records,
overrides, setUp, and validateRecords, including effect correction and cell key
marking. The probe enumerates all winning identities in twelve item categories,
spells, enchantments, and GMSTs, with selected diagnostic fields.

No World, Environment, renderer, UI, audio, or Lua runtime is constructed. TES4
input is explicitly rejected before its resource-dependent loader branch.
`.omwscripts` paths are registered in the store without script execution.
World's added globals and dynamic player initialization are outside this static
record slice. Load/normalization failures produce exit 1 and no record report;
engine diagnostics go to stderr. The TSV is an offline diagnostic, not a bounded
network export or a transactional publication format.

## Fresh verification

Windows MSVC RelWithDebInfo, 2026-09-13; all final checks below exited 0:

- Individual target builds: `tes3mp_native_loadout_probe` and
  `tes3mp_native_loadout_tests`, in `build/vnext-product`. Configuration also ran
  existing independent-boundary checks and explicit native dependency guards.
- Synthetic filters, run separately: `layered`, `missing-content`, `master-order`,
  `truncated`, `tes4`. OpenMW ESMWriter generates the fixtures. Coverage includes
  nested config/content ordering, relative paths, data-local priority,
  case-insensitive overrides, win1251 decoding, ignored/deleted items, GMST
  override, and spell/enchantment normalization. Failure filters assert no
  partial record report.
- Real load: the existing `build/tes3mp-vnext-openmw.cfg` selects the installed
  Morrowind.esm. There is no user openmw.cfg. An isolated copy with replacement
  directives lives in `build/native-loadout/real/openmw.cfg`; no user config was
  changed. Native enumeration found 3,352 items, 990 spells, 708 enchantments,
  and 1,449 GMSTs. Observed native elapsed time was 0.375 seconds; process wall
  time 0.519 seconds; sampled peak working set 94,261,248 bytes (about 90 MiB).
- Documentation budget and local-link methods passed individually.

Logs: `build/logs/native-loadout-{build,tests-build,layered,missing-content,master-order,truncated,tes4,real}.log`
and `build/logs/docs-{budget,links}.log`. Real records, input configuration,
source-config hash, timings, and observed DLLs are in `build/native-loadout/real`.
CMake generates `build/vnext-product/apps/tes3mp-server/native/loadout-dependencies-RelWithDebInfo.txt`.

Reproduce the build with the existing MSVC environment and
`cmake --build build/vnext-product --target tes3mp_native_loadout_probe --parallel 2`.
Run `build/vnext-product/tes3mp_native_loadout_probe.exe --config build/native-loadout/real --replace config`,
redirecting stdout/stderr to ignored build artifacts. For one synthetic check:
`build/vnext-product/tes3mp_native_loadout_tests.exe layered build/native-loadout/synthetic-layered`.

## Remaining limits

`openmw-lib` and `components` are temporary broad dependencies. Observed DLLs
include OSG/OpenGL/MyGUI; a successful loader is not a lightweight production
headless graph. Replace those edges with extracted content/runtime libraries
before production headless packaging. Networking boundaries remain independent.

Tamriel Rebuilt is unverified. Inventory operations, scripted/enchanted gameplay,
calculation parity, owned export limits, and engine-backed server authority are
unfinished. Existing client-authoritative positions and bounded migration worlds
remain. No full suites, upstream baseline tests, live clients, or broad gates ran.
Inherited whole-baseline provenance debt remains; only touched registry entries
were updated. Continue from the native loader, not the retired gameplay roadmap.
