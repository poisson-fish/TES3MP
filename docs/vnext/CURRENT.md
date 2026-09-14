# Current state and next action

## Handoff

- **Direction:** OpenMW-backed authoritative cooperative multiplayer; see
  [README.md](README.md) and [DECISIONS.md](DECISIONS.md).
- **Milestone:** M1's bounded probe exit remains met. This checkout already had
  native loading, owned sampling, inventory initialization, and normal-client
  enchantment-charge parity. The latest slice fixes normalized identity in full
  enumeration. M2 in [PLAN.md](PLAN.md) is next; no M2 work is claimed.
- **Next action:** separate the explicit owner/presentation dependencies in
  [containerstore.cpp](../../apps/openmw/mwworld/containerstore.cpp)
  `ContainerStore::remove(const Ptr&, ...)`, preserving stock and InventoryStore
  dispatch. Add a narrow two-owner base-store add/remove check with count/ownership
  rejection before mutation and correctly routed notifications. Keep unresolved
  stores and equipment excluded initially; do not claim atomic cross-container
  transfer until staging and failure handling exist.
- **Checkpoint:** `8850e745c298c6de629ef9a5a26bbdddf6aa56e3` preserves the working
  gameplay implementation before the engine-backed pivot.

## Implemented enumeration correction

The app-local [native loader](../../apps/tes3mp-server/native/loadout.cpp) retains
OpenMW ConfigurationManager, Utf8Encoder, ReadersCache, EsmLoader, and ESMStore
loading/setup/validation. Full enumeration now applies OpenMW's ASCII case fold
to winning record IDs, matching bounded samples. Previously it exposed the first
spelling interned process-wide, including spelling from an earlier load.
Display text, engine records, override/deletion handling, and report ordering are
preserved. The CLI help documents this identity convention.

The [filtered regression](../../apps/tes3mp-server/native/loadout_tests.cpp)
uses OpenMW-written TES3 fixtures and checks all 15 categories, earlier interned
spellings, repeated loads, enumeration/sample identity agreement, winners beyond
the sample cap, deleted records, effect normalization, text escaping, unchanged
records, and default CLI dispatch. This is a diagnostic correction, not another
gameplay model.

## Fresh verification

Windows MSVC RelWithDebInfo in `build/vnext-product`, individually run:

- Builds of `tes3mp_native_loadout_tests` and
  `tes3mp_native_loadout_probe`: exit 0.
- `enumeration-normalized`: exit 0.
- Real installed Morrowind.esm enumeration: exit 0; 6,499 winning records across
  15 categories, normalized IDs, no duplicate identities, matching category
  counts, and the completion marker.
- Real-report verifier initially exited 1 because its content-path assertion
  assumed forward slashes. After fixing the verifier's Windows path comparison,
  the same captured report passed with exit 0; the probe had succeeded.

Reproduce one command at a time, with the existing dependency DLL directory on
PATH and output redirected to `build/logs`:

```text
build/vnext-product/tes3mp_native_loadout_tests.exe enumeration-normalized build/native-loadout/enumeration-normalized
build/native-loadout/capture-enumeration.ps1
python build/native-loadout/verify-enumeration.py
```

The capture invokes the default probe with `--config build/native-loadout/real
--replace config` (resolved to an absolute config directory). Local configuration,
input/executable SHA-256 hashes, observed DLLs, and timing are in
`build/native-loadout/real/enumeration-evidence.json`. Wall startup/run time was
0.530 seconds; sampled peak working set was 90,923,008 bytes. The report is
`build/logs/native-enumeration-real.tsv`; all new logs use
`build/logs/native-enumeration-*`. No complete suites or upstream baseline tests ran.

## Inherited evidence and remaining limits

Earlier real samples and scripted MISC captures (Bittercup and the Dwemer puzzle
box) remain under `build/native-loadout/real`. Compiler-derived locals,
registration, and OnPCAdd initialization are implemented; script instructions,
equipment, enchanted effect execution, live-actor cost adjustment, transactional
inventory publication, and Tamriel Rebuilt remain unverified.

Earlier normal-client/native charge captures remain under
`build/native-loadout/parity`: the installed amulet's winning
`almsivi intervention_en` produced charge 40/40 with Morrowind.esm and 630/630
with synthetic GMST overrides. The client used `world.createObject` and
`Item.itemData(...).enchantmentCharge`; paired input hashes matched. This evidence
was not rerun. Shared store-explicit calculations remain in
[spellutil.cpp](../../apps/openmw/mwmechanics/spellutil.cpp).

The opt-in [native targets](../../apps/tes3mp-server/native/CMakeLists.txt) stay
outside the default independent server/standalone graph. No networking or
migration-world callers changed. Enumeration constructs no Environment, World,
renderer, UI, audio, actor, or script runtime. This is not an authoritative
engine-backed server; client-authoritative positions and migration worlds remain.

`loadout-dependencies-RelWithDebInfo.txt` still records broad
openmw-lib/components OSG/OpenGL/MyGUI edges. Replace these with extracted
content/runtime libraries before production headless packaging. Linked DLLs do
not establish initialized services. Inherited whole-baseline provenance debt
remains; only touched entries were updated.
