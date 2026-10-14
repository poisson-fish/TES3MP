# Development and fresh-session workflow

## Working loop

Follow [AGENTS.md](../../AGENTS.md): code/tests/`git status`, one writer,
focused checks/fixes, reviewed diffs/current evidence. Commit when authorized;
report executed checks only.

Evidence: ignored `build/`; never track assets/credentials/saves/local paths.

## Dependency and migration rules

Keep `components/tes3mp` independently buildable; exclude engine/rendering/OpenXR/OS/public
transport types. Preserve owned interfaces/dependency checks.

`apps/tes3mp-server` owns OpenMW commands/IDs/results/snapshots/persistence and explicit
CMake dependencies. Broad probes cannot prove production headless boundaries.

Separate engine context/presentation; share single-player logic. Null UI/listeners
must preserve gameplay. Plain items cannot prove scripts/constants.

After replacement/failure proof, remove obsolete code/build/schema/config/hooks/assertions;
redirect tests. Never share migration writers or polish retired gameplay.

Update touched proof/JSON; repair provenance/compare patches only for specific checks.

## Building and CMake presets

- **macOS / Linux:** [`./build.sh`](../../build.sh) `<target>` (`product`, `client`,
  `server`, `headless`, `checks`, `protocol`, `server-logic`, `adapter-tests`,
  `desktop-evidence`, `doctor`, `baseline`; `--clean`, `--verbose`, `--config`).
- **Windows:** [`build_windows.bat`](../../build_windows.bat) `-Target <target>`
  (delegates to [`build_windows.ps1`](../../build_windows.ps1) with automatic MSVC setup).
- **Provisioning:** [`scripts/provision_vnext_transport.py`](../../scripts/provision_vnext_transport.py)
  builds verified static OpenSSL/Protobuf/Abseil/GNS/c-ares inputs and writes
  `build/vnext-transport-dependencies/manifest.json`. On macOS/Windows,
  `python3 scripts/run_vnext_baseline.py provision --vcpkg-only` provisions
  `deps/openmw-deps` and cached `deps/{flatbuffers-25.12.19,recastnavigation,yaml-cpp}`.
- **VSCode / CMake:** [`CMakePresets.json`](../../CMakePresets.json) provides
  symmetric presets for `windows`, `linux`, and `macos` (`vnext-product-<os>`,
  `vnext-product-{client,server,headless,checks}-<os>`, `vnext-desktop-evidence-<os>`,
  `vnext-{protocol-contracts,server-logic,adapter-tests}-<os>`).
  [`cmake/TES3MPPlatformDefaults.cmake`](../../cmake/TES3MPPlatformDefaults.cmake)
  resolves `VNEXT_VCPKG_ROOT`, `VNEXT_VCPKG_TRIPLET`, `VNEXT_QT_ROOT`, and local
  `FetchContent` directories automatically when presets are activated directly in VSCode.

## Narrow verification

Broad gates (`baseline`/`checks`/`full`, unfiltered discovery, default standalone
presets) require authorization. Inspect `build/logs` exits/short tails;
source-contract failures report mismatches only. Builds do not prove gameplay.

Documentation checks, separately; stop on failure:

```sh
mkdir -p build/logs
python3 -m unittest scripts.tests.test_vnext_documentation.VnextDocumentationTests.test_active_document_set_stays_small_and_unambiguous > build/logs/docs-budget.log 2>&1
python3 -m unittest scripts.tests.test_vnext_documentation.VnextDocumentationTests.test_active_local_links_resolve > build/logs/docs-links.log 2>&1
```

Build owning targets/dependencies in an existing tree; one filter, never `_tests_run`.

NPC playtests, optional stock navigation:

```text
tes3mp_native_bootstrap CONFIG_DIR NEW_OUTPUT_DIR START NPC_A NPC_B 1 X:Y:Z NAV_SETTINGS
```

Emits V77 (`actors auto`, established characters); startup validates actor/cell/resource
bounds and binds the manifest. V78 requires capabilities 6/36.
V79 (capabilities 6/36/38): append `player-physics` after `NAV_SETTINGS`; register
two fresh profiles (`chargen` retains V78; never upgrade saves). Safe-point choices;
optional `starting_items COUNT` quoted-record/quantity/slot triples (`-1` unequipped,
≤64) commit once at completion. Policy changes require fresh campaigns; scripted
rewards await M5.
V80: append `shared_script RECORD globals COUNT GLOBAL...` (0–16 globals); validate
winning content/ownership before publication.
V81–83: `personal_scripts COUNT story ID scripts COUNT script "RECORD" running 0|1 ...
globals COUNT GLOBAL...`; 1–8 stories/character, 1–4 ordered members/story, ≤16
mirrored scripts. V81 is trusted-call-only; V82–83 schedule online characters
once/tick without catch-up; banks recover with gameplay.
V84 adds `dialogues COUNT "RECORD"...` per story (1–16 DIAL, 64 INFO, 16 conditions/INFO).
V85 (capability 40): ≤14 mirrored scripts reserve two dialogue calls/tick, one/character.
First topic seeds menus; living unscripted speakers validate reach/sight/life.
Stock filters read staged attributes/skills/resources/health% (disposition 0–100).
Substitutions use staged social state/globals/cell names; action bindings reject.
Journal freezes text/calendar (64 entries/story, 4,096B/entry, 16,384B total,
32,768B frames); notices follow durability. Source-only ModDisposition persists
≤64 relationships/character/story by record/life. Stat writes, scene changes and
silent migration reject.
V86–88 (capability 41): one dry interior/living unscripted NPC, static geometry.
Source-only Disable/GetDisabled; ≤12 mirrored scripts reserve four discovery/retry
calls. Terminal scopes suspend banks/interactions, ground mutations and summons.
V87 returns grounded online bodies at a quiescent hull boundary (NPC/peer gaps
≥256/128 plus radii+32); dormant outcomes freeze. V88 runs fixed stock source
Enable against the original life (≤16 instructions/one call in 16-call tick budget).
V89–90 (capability 42): source-only Position/PositionCell/GetPos/GetAngle, same
interior, coordinates ≤1,000,000, displacement ≤4,096, rotation ≤360°. Validate
scoped hull before durability and geometry on recovery; one inert pose, occupied
scope, ≤37 capabilities. V90 returns grounded bodies beyond original/destination
NPC (≥256) and peer (≥128) hulls (plus radii+32) with living original life.
Filters: `relocation-return`, `story-relocation-return`; capture: `--relocate-source --return-path`.
V91–93 (capability 41): one foreign-death dependency/story preserves pending
context/script/instruction/reference/life/deadline, banks and paused clock.
GetDisabled discovers waits; GetSecondsPassed advances 30 Hz after bank success
(16-call wake budget). V92 OnDeath: ≤256 source-life events, character/story cursor,
pre-quest history, whole-bank rollback, commit-only consumption; canonical player
killers only. V93 reserves oldest wait's next life (ties: character/story) across
wake/offline/restart until credit, protecting one canonical opportunity.
Filters: `source-waits`, `life-events`, `opportunities` (host: `story-` prefix).
Source-only GetHealth/GetHealthGetRatio use staged stats; health writes, targeted
StartScript and calendar arithmetic reject.
V94 (capability 42): one nonrepeating source-only AiTravel/AiEscort player
(duration 0), same interior, V89 bounds, ≤2 active private frames (≤65,536B/frame),
two 60 Hz substeps/30 Hz commit. Freezes walk speed at quiescent admission;
temporary effects/paralysis/levitation/overencumbrance reject. Offline owners
suspend; arrival retains private destination without auto-credit/loot.
Filters: `activities`, `story-activities`, `story-departure`.
V95 (`native-inventory-95`, capabilities 41–42): unifies V85–95 (Disable/Enable/safe
returns, relocation, activities, source waits, OnDeath and fair opportunities); admits
read-only source-bank GetPos/GetAngle/GetJournalIndex/CellChanged/MenuMode/GetPCSleep
(rejects `player`, journal writes, and Position/PositionCell in banks), steps private NPC
banks against private relocated/activity pose, staged journal state and detached spawn
stats after activity substeps, suspends terminal Disable banks until safe-return
Enable, and enforces fair-opportunity reservations on relocation/travel.
Filters: `npc-banks`, `story-npc-banks`, `script-banks`, `quest-audit`.
Desktop captures emit `player_appearance_sample` across join/reentry/reconnect/restart.

## Documentation contract

Five active files: README, CURRENT (≤850 words), PLAN (sole roadmap, ≤1,500),
DEVELOPMENT, DECISIONS; combined ≤5,000. Dependency proofs are exempt.
Replace handoffs; keep links/evidence. No duplicate trackers/plans/diaries/reports.
