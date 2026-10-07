# Development and fresh-session workflow

## Working loop

1. Read [CURRENT](CURRENT.md), this file, [README](README.md), active [PLAN](PLAN.md);
   consult [DECISIONS](DECISIONS.md) for boundary/persistence changes.
2. Code/tests/`git status` establish behavior; consult history/retired documents only for gaps.
3. Bounded slices: one writer, preserved migration, shared handlers/checks/dependencies. No additional plan.
4. Run the smallest affected check; fix/rerun failures.
5. Review diffs; update CURRENT's behavior/limitations/evidence and consequential decisions.
   Commit when authorized; report executed checks only.

Measurements/logs belong in ignored `build/`. Never track assets, credentials/tokens,
saves or machine-local content paths. Git archives development.

## Dependency and migration rules

Keep `components/tes3mp` independently buildable behind owned interfaces; forbid
OpenMW/rendering/OpenXR/OS/public transport-library types. Preserve dependency checks.

`apps/tes3mp-server` uses OpenMW through owned commands/IDs/results/snapshots/persistence
and explicit CMake dependencies. Broad probes cannot prove production headless boundaries.

Separate engine context/presentation; share single-player logic. Null UI/listeners
must preserve gameplay. Plain items cannot prove scripts/constants.

Migration paths cannot share mutation. After replacement/failure proof, remove obsolete
code/build entries/schemas/configuration/hooks/assertions; redirect useful tests. Do not polish retired gameplay.

Update touched proof/JSON inputs. Repair whole-baseline provenance/compare patches only for specific checks.

## Narrow verification

Broad suites/gates/baselines require requested scope. Log to `build/logs`; inspect
exit codes/short tails. Source-contract failures report compact mismatches, never source files.

Documentation checks, separately; stop on failure:

```powershell
New-Item -ItemType Directory -Force -Path build/logs | Out-Null
python -m unittest scripts.tests.test_vnext_documentation.VnextDocumentationTests.test_active_document_set_stays_small_and_unambiguous *> build/logs/docs-budget.log
# Inspect $LASTEXITCODE before the next command.
python -m unittest scripts.tests.test_vnext_documentation.VnextDocumentationTests.test_active_local_links_resolve *> build/logs/docs-links.log
```

Build the owning C++ target/dependencies in an existing tree; run one executable/filter,
never aggregate `_tests_run`. Update CURRENT evidence.

NPC playtests, optional stock navigation:

```text
tes3mp_native_bootstrap CONFIG_DIR NEW_OUTPUT_DIR START NPC_A NPC_B 1 X:Y:Z NAV_SETTINGS
```

Emits V77 with `actors auto`; startup validates actor/cell/resource bounds.
Registrations bind the new manifest.

V79 (capabilities 6/36/38): append `player-physics` after `NAV_SETTINGS`; register
two fresh profiles. `chargen` retains V78. Never upgrade saves.
Choices/completion use the configured safe point.
Optional trailing `starting_items COUNT`: quoted-record/quantity/slot triples
(`-1` unequipped, at most 64). Grants commit once at completion; omission stays empty.
Policy changes require fresh campaigns; scripted rewards await M5.
Fresh V80 bootstrap: append `shared_script RECORD globals COUNT GLOBAL...` after `player-physics`
(0–16 globals); validate winning content/ownership before publication. Omission retains V79.
V81/V82 descriptors also require `personal_scripts COUNT story ID script "RECORD" globals COUNT GLOBAL...`
(1–8 stories, mirrored per character). Native trusted calls stage source-free core
invocations in prepared host ticks. Fresh V82 schedules each established online character/story
once per host tick; offline/skipped ticks never catch up. Markers recover with gameplay;
V81 remains trusted-call-only. Personal output delivery stays closed. Bootstrap still emits V80.
V77 requires established characters; V78 requires capabilities 6/36.
Desktop captures emit race/head/hair/sex as `player_appearance_sample`.
Compare profiles through join/reentry/reconnect/restart; creation has no body.
Wire/native fixtures remain synthetic.

`build_windows.bat -Target server`, `client` and `desktop-evidence` initialize MSVC
and log to `build/logs`; inspect build cost first. Builds do not prove gameplay.
Wrappers `protocol`, `server-logic`, `adapter-tests`, `contracts`, `standalone` run
groups; `baseline`, `checks`, `full`, unfiltered unittest discovery and default
standalone presets are not narrow defaults. Linux/macOS presets: root CMakePresets.json.

Record bounded synthetic/loadout/live evidence. Captures prove exercised paths; mod data is external.

## Documentation contract

Keep exactly five active Markdown files under `docs/vnext`:
README (vision), CURRENT (state/evidence/next action, ≤850 words), PLAN (sole roadmap,
≤1,500), DEVELOPMENT (workflow), DECISIONS (constraints/proposals).
Combined ceiling: 5,000 words; dependency proofs exempt. Replace stale handoffs,
preserving links/evidence. No duplicate trackers/plans/diaries/reports.
