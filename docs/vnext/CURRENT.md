# Current state and next action

## Handoff

- **Direction:** OpenMW-backed authoritative cooperative multiplayer; see
  [README.md](README.md) and [DECISIONS.md](DECISIONS.md).
- **Active milestone:** M1 in [PLAN.md](PLAN.md). Native static-loadout enumeration
  and bounded owned diagnostic sampling work; M1 remains incomplete.
- **Next action:** implement a filtered native inventory probe using the retained
  engine store, [ManualRef](../../apps/openmw/mwworld/manualref.cpp), and
  [ContainerStore::add](../../apps/openmw/mwworld/containerstore.cpp). Start at
  `add`/`addImp`: expose explicit store/player/service context for the global
  player, WorldModel registration, local scripts, and inventory presentation.
  Keep stock OpenMW calling the shared behavior. Exercise a plain item and a
  scripted item, including OnPCAdd; report any remaining coupling instead of
  supplying silent UI/script stubs. Do not start protocol redesign.
- **Checkpoint:** `8850e745c298c6de629ef9a5a26bbdddf6aa56e3` preserves the previous
  timed/area-magic implementation before the engine-backed pivot.

## Implemented native slice

The opt-in [native targets](../../apps/tes3mp-server/native/CMakeLists.txt) build
`tes3mp_native_loadout_probe` and individually filtered
`tes3mp_native_loadout_tests`. They remain excluded from default builds and
outside the independent server/standalone graph. Existing server, desktop,
baker, and canonical-gameplay callers remain the working migration base.

[Loadout](../../apps/tes3mp-server/native/loadout.hpp) owns actual OpenMW records,
encoder, and cached readers. ConfigurationManager resolves configuration layers,
paths, encoding, and ordered content; Collections handles data/data-local
priority. EsmLoader and ESMStore retain engine overrides, ignored/deleted records,
setup, effect normalization, and cell-key handling. Default TSV enumeration
still covers twelve item categories, spells, enchantments, and GMSTs.

`--sample` now stages [owned diagnostic values](../../apps/tes3mp-server/native/diagnostic.hpp)
from those winning stores in [loadout.cpp](../../apps/tes3mp-server/native/loadout.cpp).
A bounded heap selects the first four IDs per category in engine order. IDs use
OpenMW's ASCII case fold; names, item weight/value, normalized effect fields and
indexes, and typed GMST values are copied. The full engine store remains intact
and available, including unsampled records.

Ceilings are 60 records, 32 effects/record, 4,096 bytes/string, 64 KiB aggregate
string bytes, and 64 KiB staged escaped TSV. Callers can lower limits only.
Counts and strings are checked before copying; report growth is checked before
append. Invalid IDs, non-finite projected numbers, invalid effect ranges, and
limit violations reject the whole sample before touching the output stream.
Stream/device write failures remain visible but cannot be rolled back. This is
an offline diagnostic, not a wire protocol, complete record validator, or new
gameplay catalog; only selected fields/records are projected and validated.

No World, Environment, renderer, UI, audio, or Lua runtime is constructed. TES4
is rejected before its resource-dependent branch; `.omwscripts` paths are stored
without execution. Added globals and dynamic player initialization remain outside
this slice. Load and projection errors exit 1 with diagnostics on stderr.

## Fresh verification

Windows MSVC RelWithDebInfo in `build/vnext-product`, final checks all exit 0:

- Individual builds of `tes3mp_native_loadout_tests` and
  `tes3mp_native_loadout_probe`.
- `sample-owned`: all fifteen categories, winning IDs, decoded text, typed GMSTs,
  normalized effects, deterministic selection, and values surviving store destruction.
- `sample-limits`: exact ceilings, oversized limits, count/string/effect/report
  rejection, and unchanged publication/engine records.
- `sample-malformed`: OpenMW-written plugins with NaN/infinite fields, invalid
  effect range, 4,097-byte text, and 33 effects; no partial publication/mutation.
- `layered`: existing configuration priority, override/deletion, and normalization.
- Documentation budget and local-link methods, each run individually.

Logs are `build/logs/native-sample-*.log`. The first ownership run exposed an
incorrect GMST integer type in the new fixture; it was corrected and rerun.
No complete suites, broad gates, upstream baseline tests, or live clients ran.

Real installed Morrowind.esm with the isolated existing configuration passed
`--sample`: 60 records, 6,819 output bytes, 0.356 seconds native elapsed,
0.560 seconds process wall time, and 90,636,288 bytes sampled peak working set
(about 86 MiB). Report: `build/logs/native-sample-real.tsv`; config/content hashes,
timing, and observed DLLs: `build/native-loadout/real/sample-evidence.json`.
Reproduce after the MSVC target build with:

```text
build/vnext-product/tes3mp_native_loadout_probe.exe --config build/native-loadout/real --replace config --sample
```

Retained prior full enumeration found 3,352 items, 990 spells, 708 enchantments,
and 1,449 GMSTs. Its report/evidence remains in `build/native-loadout/real`;
prior missing-content/master-order/truncated/TES4 filters were not rerun here.

## Remaining limits

The generated `loadout-dependencies-RelWithDebInfo.txt` under the native build
directory records broad `openmw-lib`/`components` OSG/OpenGL/MyGUI edges. Replace
them with extracted content/runtime libraries before production headless packaging.
Linked DLLs do not establish initialized runtime services.

Tamriel Rebuilt validation, inventory operations, scripted/enchanted gameplay,
calculation parity, and engine-backed server authority remain unfinished.
Client-authoritative positions and bounded migration worlds remain. Inherited
whole-baseline provenance debt remains; only touched native entries were updated.
