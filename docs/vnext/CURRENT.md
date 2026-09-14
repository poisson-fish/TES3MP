# Current state and next action

## Handoff

- **Direction:** OpenMW-backed authoritative cooperative multiplayer; see
  [README.md](README.md) and [DECISIONS.md](DECISIONS.md).
- **Milestone:** M1's bounded probe exit is now met: native real-loadout loading,
  owned sampling, plain/scripted inventory initialization, and observed normal-client
  enchantment-charge parity. M2 in [PLAN.md](PLAN.md) is next; no M2 work is claimed.
- **Next action:** separate the explicit owner/presentation dependencies in
  [containerstore.cpp](../../apps/openmw/mwworld/containerstore.cpp)
  `ContainerStore::remove(const Ptr&, ...)`, preserving stock and InventoryStore
  dispatch. Add a narrow two-owner base-store add/remove check with count/ownership
  rejection before mutation and correctly routed notifications. Keep unresolved
  stores and equipment excluded initially; do not claim atomic cross-container
  transfer until staging and failure handling exist.
- **Checkpoint:** `8850e745c298c6de629ef9a5a26bbdddf6aa56e3` preserves the working
  gameplay implementation before the engine-backed pivot.

## Implemented calculation slice

[spellutil.cpp](../../apps/openmw/mwmechanics/spellutil.cpp) now shares effect,
aggregate, enchantment cast-cost, and maximum-charge calculations through explicit
ESMStore inputs. Stock Environment callers use that behavior. The extracted paths
read the supplied loadout's GMSTs instead of caching the first store's settings in
function-local statics. Actor skill adjustment and effect execution remain separate.

`--enchantment ID` in the existing
[native probe](../../apps/tes3mp-server/native/enchantment.cpp) reports normalized
identity, stored versus calculated cost/charge, autocalc/type, effect count, and
used multipliers. It stages publication after validation. Diagnostic limits are
256 bytes/ID, 32 effects, nonnegative effect fields/base cost/multiplier at most
1e6, and representable rounded integer cost/charge. Malformed input, conversion
risk, and charge multiplication overflow reject without partial reports.

The mode constructs no Environment, World, actor, renderer, UI, audio, or script
runtime. The opt-in [native targets](../../apps/tes3mp-server/native/CMakeLists.txt)
remain outside the default and independent server/standalone graph. No
components/tes3mp interfaces or migration-world callers changed.

## Fresh verification

Windows MSVC RelWithDebInfo in `build/vnext-product`, individually run:

- Builds of `tes3mp_native_loadout_tests`, `tes3mp_native_loadout_probe`, and
  `openmw`: exit 0. The initial test build exited 2 for missing record includes;
  fixed, then the same target passed before tests continued.
- `enchantment-cost`: exit 0. OpenMW-written/loadable fixtures cover all four
  charge types, manual/autocalc selection, target surcharge, rounding, relevant
  effect flags, spell/potion method differences, CLI dispatch, unchanged records,
  and different GMSTs across loads in one process.
- `enchantment-rejection`: exit 0. Effect/ID limits, malformed types/fields,
  nonfinite settings/base cost, integer overflow, missing IDs, and conflicting
  modes reject without partial publication.
- Normal-client/native maximum-charge comparison: exit 0 for both captures in
  each loadout and for the input-hash/result verifier.

The installed Morrowind amulet uses winning `almsivi intervention_en`. Normal
OpenMW creates the item through `world.createObject`, follows its Clothing record's
enchantment ID, and reads production `Item.itemData(...).enchantmentCharge`.
This observes an independent runtime input path, not two direct helper calls:

| Loadout | fEffectCostMult | Charge multiplier | Native raw cost | Native/client charge |
|---|---:|---:|---:|---:|
| Morrowind.esm | 0.5 | 5 | 7.5 | 40 / 40 |
| Morrowind plus synthetic Base.esm/Calculation.esp | 3 | 14 | 45 | 630 / 630 |

Both include the same diagnostic Lua script and Morrowind.bsa; each pair's input
hashes match. The second loadout is explicitly synthetic GMST override evidence.
The client initially exited 1 for missing OSG plugins, then its retry timed out
before gameplay because the loader-only config omitted Morrowind.bsa. Applying the
existing dependency path and adding the archive resolved those capture failures.

Reproduce one command at a time:

```text
build/vnext-product/tes3mp_native_loadout_tests.exe enchantment-cost build/native-loadout/enchantment-cost
build/native-loadout/parity/capture.ps1 -Label base -Mode native
build/native-loadout/parity/capture.ps1 -Label base -Mode client
```

Repeat captures with `-Label changed`, then run
`python build/native-loadout/parity/verify.py`. Captures/configs/Lua, comparison,
hashes, observed DLLs and timing reside under `build/native-loadout/parity`.
Native startup was 0.483/0.489 seconds; sampled peak working sets
94,167,040/93,732,864 bytes. Logs: `build/logs/native-enchantment-*.log` and `.out`.
Formatting, diff whitespace, touched provenance/patch coverage, documentation
budget and local links: exit 0.

## Inherited evidence and remaining limits

Earlier real enumeration/sample evidence remains under `build/native-loadout/real`;
scripted MISC captures include Bittercup and the Dwemer puzzle box. Those checks
were not rerun. Script locals/registration/OnPCAdd initialization works; script
instructions, equipment, enchanted effect execution, live-actor cost adjustment,
transactional inventory publication, and Tamriel Rebuilt remain unverified.
No complete suites or upstream baseline tests ran. This is not an authoritative
engine-backed server; client-authoritative positions and migration worlds remain.

`loadout-dependencies-RelWithDebInfo.txt` still records broad openmw-lib/components
OSG/OpenGL/MyGUI edges. Replace these with extracted content/runtime libraries
before production headless packaging. Linked DLLs do not establish initialized
services. Inherited whole-baseline provenance debt remains; only touched entries
were updated.
