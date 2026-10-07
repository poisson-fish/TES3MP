# Development and fresh-session workflow

## Working loop

1. Read [CURRENT.md](CURRENT.md), this file, [README.md](README.md), active
   [PLAN.md](PLAN.md), and [DECISIONS.md](DECISIONS.md) for boundary/persistence changes.
2. Inspect owning code/tests and `git status`; they establish behavior. Search
   history/retired documents only for specific unanswered questions.
3. Advance bounded slices with one writer; preserve migration, share handlers/checks
   and follow dependencies. No additional plan.
4. Run the smallest affected check individually; stop, fix and rerun on failure.
5. Review the diff; update CURRENT's behavior/limitations/evidence and durable
   decisions. Commit only when authorized; report only run checks.

Use ignored `build/` for measurements/logs. Never track game/mod assets, credentials,
tokens, user saves or machine-local content paths. Git is the development archive.

## Dependency and migration rules

Keep `components/tes3mp` independently buildable behind owned interfaces; no OpenMW,
rendering, OpenXR, OS or public transport-library types. Preserve boundary checks.

App-local `apps/tes3mp-server` runtime targets may depend on OpenMW. Expose owned
commands/IDs/results/snapshots/persistence; declare CMake dependencies explicitly.
Broad probe links do not establish production headless boundaries.

Refactor owning engine code to separate context/presentation; share logic with
single-player. Null UI/listeners must preserve gameplay. Plain items do not prove
scripts/constants.

Migration paths cannot share live mutation. After replacement/failure checks,
remove obsolete implementations, build entries, schemas, configuration, hooks and
assertions; redirect useful tests. Do not polish retired gameplay before advancing.

Update touched proof/JSON tooling inputs. Repair whole-baseline provenance and
compare baseline patches only when a specific check requires it.

## Narrow verification

Broad suites/gates/baselines require requested scope. Log to `build/logs`; inspect
exit codes and short failure tails.
Source-contract failures report compact mismatches, never source files.

Run documentation checks separately; stop on first failure:

```powershell
New-Item -ItemType Directory -Force -Path build/logs | Out-Null
python -m unittest scripts.tests.test_vnext_documentation.VnextDocumentationTests.test_active_document_set_stays_small_and_unambiguous *> build/logs/docs-budget.log
# Inspect $LASTEXITCODE before the next command.
python -m unittest scripts.tests.test_vnext_documentation.VnextDocumentationTests.test_active_local_links_resolve *> build/logs/docs-links.log
```

Build the owning C++ target in an existing tree; run one executable/filter.
Add required dependencies only; never aggregate `_tests_run`; update CURRENT evidence.

For shared NPC playtests, bootstrap with the optional final stock navigation settings:

```text
tes3mp_native_bootstrap CONFIG_DIR NEW_OUTPUT_DIR START NPC_A NPC_B 1 X:Y:Z NAV_SETTINGS
```

This emits V77 with `actors auto`; startup validates actor/cell/resource bounds.
Registrations must bind the new manifest.

Append `player-physics` after `NAV_SETTINGS` for V79 (capabilities 6/36/38); register
two fresh profiles. `chargen` retains V78 migration. Do not upgrade existing saves.
Winning choices and creation/completion use the configured safe point.
Optional trailing `starting_items COUNT`: quoted-record/quantity/slot triples
(`-1` unequipped, at most 64). Grants commit once at completion; omission stays empty.
Policy changes require fresh campaigns; scripted rewards remain M5.
Append `shared_script RECORD globals COUNT GLOBAL...` after `player-physics`
for fresh V80 bootstrap (0–16); admission validates winning content/ownership before
publication. Omission retains V79; existing saves never upgrade.
V77 requires established characters; V78 requires capabilities 6/36.
Native desktop captures emit rendered race/head/hair/sex as `player_appearance_sample`.
Compare distinct profiles through join/reentry/reconnect/restart; creation has no body.
Wire/native fixtures remain synthetic.

`build_windows.bat -Target server`, `client` and `desktop-evidence` initialize MSVC
and log to `build/logs`; inspect build cost first. Builds do not prove gameplay.
Wrappers `protocol`, `server-logic`, `adapter-tests`, `contracts`, `standalone` run
groups; `baseline`, `checks`, `full`, unfiltered unittest discovery and default
standalone presets are not narrow defaults. Linux/macOS presets remain in
root CMakePresets.json.

Record bounded synthetic/loadout/live evidence.
Captures prove exercised paths; mod data is external input.

## Documentation contract

Keep exactly five active Markdown files under `docs/vnext`:
README (vision), CURRENT (state/evidence/next action, ≤850 words), PLAN (sole roadmap,
≤1,500), DEVELOPMENT (workflow), DECISIONS (constraints/proposals).
Combined ceiling: 5,000 words; dependency proofs exempt. Replace stale handoffs;
preserve links/evidence. No duplicate trackers/plans/diaries/reports.
