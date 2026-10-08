# Development and fresh-session workflow

## Working loop

Follow [AGENTS.md](../../AGENTS.md): code/tests/`git status`, one writer,
focused checks/fixes, reviewed diffs/current evidence. Commit when authorized;
report executed checks only.

Evidence: ignored `build/`. Never track assets/credentials/saves/local content paths.

## Dependency and migration rules

Keep `components/tes3mp` independently buildable behind owned interfaces; forbid
OpenMW/rendering/OpenXR/OS/public transport-library types. Preserve dependency checks.

`apps/tes3mp-server` uses OpenMW through owned commands/IDs/results/snapshots/persistence
and explicit CMake dependencies. Broad probes cannot prove production headless boundaries.

Separate engine context/presentation; share single-player logic. Null UI/listeners
must preserve gameplay. Plain items cannot prove scripts/constants.

After replacement/failure proof, remove obsolete code/build entries/schemas/configuration/hooks/assertions;
redirect tests. Never share migration writers or polish retired gameplay.

Update touched proof/JSON inputs; repair baseline provenance/compare patches only for specific checks.

## Narrow verification

Broad gates require authorization. Inspect `build/logs` exits/short tails;
source-contract failures report mismatches only.

Documentation checks, separately; stop on failure:

```powershell
New-Item -ItemType Directory -Force -Path build/logs | Out-Null
python -m unittest scripts.tests.test_vnext_documentation.VnextDocumentationTests.test_active_document_set_stays_small_and_unambiguous *> build/logs/docs-budget.log
# Inspect $LASTEXITCODE before the next command.
python -m unittest scripts.tests.test_vnext_documentation.VnextDocumentationTests.test_active_local_links_resolve *> build/logs/docs-links.log
```

Build owning targets/dependencies in an existing tree; one filter,
never `_tests_run`.

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
V81/V82: `personal_scripts COUNT story ID script "RECORD" globals COUNT GLOBAL...`
(1–8 stories mirrored/character). V81 admits trusted source-free calls; V82 schedules
online established characters once/tick without catch-up. V83 uses
`story ID scripts COUNT script "RECORD" running 0|1 ... globals COUNT GLOBAL...`:
1–4 ordered members/story, ≤16 mirrored scripts; locals/flags/ordinals/markers recover with gameplay.
V84 adds `dialogues COUNT "RECORD"...` per story (1–16 DIAL, 64 INFO, 16 conditions/INFO).
V85 requires fresh campaigns/capability 40: ≤14 mirrored scripts reserve two dialogue calls/tick,
one/character; first topic seeds menus. Living unscripted speakers validate reach/sight/life.
Private responses use bounded stock substitutions with authenticated character/speaker,
staged ranks/bounty, owned globals and authoritative cell names before results;
action bindings reject. Literal messages, choices and progression follow durability;
reconnect is silent. Filters read staged modified attributes/skills, current resources
and health percentage. Thresholds 0–100 share stock disposition with authenticated
character/speaker snapshots and winning settings. Journal commands freeze stock expanded text and server calendar dates at invocation;
64 entries/story, 4,096 bytes/entry, 16,384 total text bytes, 32,768-byte private frames.
Journal update notices follow durability; stock journal installation is silent.
Source-only stock ModDisposition persists ≤64 placement relationships/character/story;
record/life validation restores the private base before filtering. Player stat writes,
scene changes and broader NPC restoration remain closed. Prior images never silently migrate;
V81–84 personal output stays closed. Bootstrap emits V80.
V77 requires established characters; V78 requires capabilities 6/36.
Desktop captures emit race/head/hair/sex as `player_appearance_sample`.
Compare profiles through join/reentry/reconnect/restart; creation has no body.
Wire/native fixtures remain synthetic.

`build_windows.bat -Target server`, `client` and `desktop-evidence` initialize MSVC
and log to `build/logs`; inspect build cost first. Builds do not prove gameplay.
Wrappers `protocol`, `server-logic`, `adapter-tests`, `contracts`, `standalone` run
groups; `baseline`, `checks`, `full`, unfiltered unittest discovery and default
standalone presets are not narrow defaults. Linux/macOS presets: root CMakePresets.json.

## Documentation contract

Five active files: README, CURRENT (≤850 words), PLAN (sole roadmap, ≤1,500),
DEVELOPMENT, DECISIONS; combined ≤5,000. Dependency proofs are exempt.
Replace handoffs; keep links/evidence. No duplicate trackers/plans/diaries/reports.
