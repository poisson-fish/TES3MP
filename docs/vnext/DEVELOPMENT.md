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
V81–83: `personal_scripts COUNT story ID scripts COUNT script "RECORD" running 0|1 ...
globals COUNT GLOBAL...`; 1–8 mirrored stories/character, 1–4 ordered members/story,
≤16 mirrored scripts. V81 is trusted-call-only; V82–83 schedule online characters
once/tick without catch-up. Banks recover with gameplay.
V84 adds `dialogues COUNT "RECORD"...` per story (1–16 DIAL, 64 INFO, 16 conditions/INFO).
V85: fresh campaigns/capability 40; ≤14 mirrored scripts reserve two dialogue calls/tick,
one/character. First topic seeds menus; living unscripted speakers validate reach/sight/life.
Stock filters read staged attributes/skills/resources/health%; disposition thresholds 0–100.
Authenticated stock substitutions use staged social state/globals/cell names; action bindings reject.
Journal freezes text/calendar at invocation: 64 entries/story, 4,096 bytes/entry,
16,384 total text bytes, 32,768-byte frames. Notices follow durability; installation
is silent. Source-only ModDisposition persists ≤64 placement relationships/character/story,
validated by record/life. Stat writes, V85 scene changes and broader restoration reject.
Prior images never silently migrate; V81–84 output stays closed. Bootstrap emits V80.
V86–88: fresh descriptor/images, capability 41; one dry interior, one living
unscripted NPC, static geometry. Source-only Disable/GetDisabled; ≤12 mirrored
scripts reserve four discovery/retry calls. Terminal scopes suspend banks/new
interactions, ground mutations and summons; movement/resources continue.
V87 returns grounded online living bodies at a quiescent hull boundary:
NPC/peer gaps ≥256/128 units, enlarged by hull radii plus 32. Dormant outcomes
freeze; other banks resume. V88 runs fixed stock source Enable there against
the original life, ≤16 instructions/one call in the 16-call tick budget.
V89: fresh images/capability 42; source-only Position/PositionCell/GetPos/GetAngle,
same interior, finite coordinates ≤1,000,000, displacement ≤4,096, rotation ≤360°.
Inherited discovery/retry budgets; validate final scoped hull before durability,
geometry on recovery. One inert pose, occupied scope; no new simulation/life/loot,
cross-cell movement or mixed Disable/Position. Wire permits suppression
or one zero-velocity pose; ≤37 optional capabilities; silent recovery.
V90 returns grounded online living bodies beyond quiescent original/destination
NPC (≥256) and peer (≥128) hulls, enlarged by radii plus 32; living original life
required. Occupancy commits while outcomes/receipts remain dormant.
Filters: `relocation-return`, `story-relocation-return`; capture: `--relocate-source --return-path`.
V91–93: fresh images/capability 41; same single-NPC interior. One foreign-death
dependency/story preserves pending context/script/instruction/reference/life/deadline,
banks and paused clock. GetDisabled discovers waits; GetSecondsPassed advances
30 Hz only after bank success. Sixteen-call wake budget; newer living lives resume.
V92 stock OnDeath: ≤256 source-life events, character/story cursor, pre-quest history,
whole-bank rollback, commit-only consumption. Helpers receive no credit; host admission
uses canonical player killers. Snapshots test summon/environment attribution;
live enrollment/summons stay closed. V93 reserves the oldest wait's next life
(ties: character/story), retains it on wake/offline/restart and releases on credit.
Combat admission/impacts and NPC targeting protect one canonical opportunity without copies.
Recovery validates cursor/reservation origins against death history.
Filters: `source-waits`, `life-events`, `opportunities`; host: `story-` prefix.
Source-only stock GetHealth/GetHealthGetRatio use staged stats; foreign deaths defer
the bank. Journal wait/reservation status recovers silently. Health writes, targeted StartScript and calendar arithmetic stay closed.
V94: fresh images/capability 42; one nonrepeating source-only AiTravel or AiEscort player
(duration 0) in the same dry interior. V89 coordinate/displacement bounds apply;
≤2 active private frames, ≤65,536 bytes/frame, two 60 Hz substeps/30 Hz commit.
Stock walk speed freezes at quiescent admission; temporary effects, paralysis, levitation or overencumbrance reject.
Continuation/pose/banks commit together; offline owners suspend without catch-up.
Arrival retains the private destination; no automatic quest credit or loot access.
Banks remain frozen; combat, package replacement, cross-cell AI and mixed scene writes
stay closed. Filters: `activities`, `story-activities`, `story-departure`.
V77 requires established characters; V78 requires capabilities 6/36.
Desktop captures emit race/head/hair/sex as `player_appearance_sample`;
compare join/reentry/reconnect/restart. Creation has no body; wire/native fixtures are synthetic.

`build_windows.bat -Target server`, `client` and `desktop-evidence` initialize MSVC
and log to `build/logs`; inspect build cost first. Builds do not prove gameplay.
Group wrappers and `baseline`/`checks`/`full`, unfiltered discovery and default
standalone presets are broad gates. Linux/macOS presets: root CMakePresets.json.

## Documentation contract

Five active files: README, CURRENT (≤850 words), PLAN (sole roadmap, ≤1,500),
DEVELOPMENT, DECISIONS; combined ≤5,000. Dependency proofs are exempt.
Replace handoffs; keep links/evidence. No duplicate trackers/plans/diaries/reports.
