# Current state and next action

**Active: M5 [PLAN.md](PLAN.md), native dialogue transactions. M4 closed, 2026-10-06.**
[Host](../../apps/tes3mp-server/native/inventory_host.hpp):
60 Hz NPC/door/player physics substeps; 30 Hz commits.

**V77/V78 migration:** bounded shared exploration and character creation retain
split/regroup/restart, streaming, stock/TR bodies and starter evidence:
`build/exploration-desktop-{06,restart-01}/result.json`,
`build/character-desktop-fixture-02/{complete-03,restart-02}/result.json`.
TR remains appearance-only. Resume inactivity/replay bounds:
`build/logs/resume-inactivity.log`.

**Death/respawn:** ordinary inventory retained; owner accepted revival/safe-point
respawn, 2026-10-04. Same-cell and corpse recovery:
`build/logs/respawn-same-cell-{client-build,locomotion}.log`,
`build/logs/peer-loot-{reconnect-retry,restart}.log`.
Seyda Neen six-actor/nine-interior trace: `build/logs/seyda-six-actors.log`;
scripts/scripted inventory await M5.

**M4 accepted, 2026-10-06:** body/door/NPC:
`build/m4-user-acceptance-body-door-npc-v78-20261005-230100/operator-acceptance.json`.
Contact/recoil: `build/logs/melee-recoil-{baseline,fixed}.log`.

**V79 accepted, 2026-10-06:** capability 38; stock player physics, shared hulls,
durable resources/frames and bounded replay. Impaired heading/jump/stop/contact:
`build/player-physics-desktop-impaired-16/result.json`; corrections max
19.3/23.7, mean 1.3/1.5 units (budgets 35/8), screenshots inspected.
Exact recovery: `build/player-physics-desktop-restart-04/result.json`.
Owner sign-off/128-input backpressure/combat/death/loot/respawn:
`build/m4-user-acceptance-player-v79-20261006-live-01/operator-acceptance.json`.
Clock: `build/logs/player-physics-clock.log`. Fresh campaigns require
`player-physics`; V78 retains migration; movement skills await M5.

**M5 shared execution:** [runtime](../../apps/tes3mp-server/native/world_script.hpp)
Stock compiler/opcodes; atomic globals/locals/messages and GSCR/GLOB recovery.
Synthetic `build/logs/m5-world-script-{shared,rollback,recovery,admission,content}.log`:
Fargoth declarations pass; Vodunius/chargen/skipped APIs reject.

**V80 scheduling/delivery:** one script/0–16 globals; calendar ownership rejects.
Durable tick ordinals; capability 39 sends ≤8 literal messages/≤256 bytes through
stock UI. Recovery/baselines never replay output.
Synthetic atomic failure/admission/offline/restart/legacy:
`build/logs/m5-ambient-{host,adapter,delivery,bootstrap,bootstrap-admission,bootstrap-zero}.log`,
`build/logs/m5-world-script-host-{shared,rollback}.log`,
`build/logs/m5-player-physics-regression.log`.
Paired 100ms ±25ms/10% loss/reorder, absences/restart/clean exits:
`build/m5-ambient-desktop-{02,restart-01,ui-01}/result.json`; screenshots inspected.
Regression: `build/logs/m5-context-shared-regression.log`.

**M5 context/V81–82:** explicit campaign/story/physical/character/source/life/invocation/event;
unstaged services reject, waits differ from absence. Isolated banks/progression/cursors
commit with gameplay; online once/tick, no catch-up, silent recovery.
Synthetic stock/isolation/rollback/legacy:
`build/logs/m5-context-{lookup,rollback,stock}.log`,
`build/logs/m5-personal-{state,host,shared}.log`,
`build/logs/m5-personal-scheduling-{state,host,legacy-state,legacy-host}.log`.

**M5 cross-script:** owned stock typed members/lifecycle; ≤64 locals/128-byte names;
physical targets reject. Synthetic isolation/rollback/recovery/stock:
`build/logs/m5-cross-script-{context,rollback,stock,lookup-regression}.log`.
V81/V82 retain single-script admission.

**V83 banks:** 1–4 scripts/story, ≤16 mirrored instances; stopped locals/flags/
ordinals/markers and globals commit with gameplay. Isolation/rollback/offline/recovery/legacy:
`build/logs/m5-script-banks-{state,host-05,legacy-state,legacy-host,trusted-host,shared}.log`.

**V84 dialogue:** stock filters/results/progression/bank receipts join gameplay.
Trusted-source isolation/rollback/recovery remain synthetic:
`build/logs/m5-dialogue-{isolation,rollback,recovery,stock,host-05}.log`.

**V85 authenticated dialogue:** capability 40 binds character/story, living unscripted
speaker, reach/sight/life, revision and offered choice. ≤14 mirrored scripts;
two intents/tick, one/character. Banks/progression/choices/receipts join gameplay;
private output follows durability. Client results/greetings/services/restock suppress.
First topic seeds knowledge; baselines/choices restore silently.
Synthetic isolation/duplicates/defer/rollback/recovery/presentation:
`build/logs/m5-dialogue-intents-{state,host,client,adapter,ingress,delivery}.log`.
Semantic paired desktop progression/absence/restart/no-replay:
`build/m5-dialogue-desktop-{06,restart-02}/result.json`;
100ms ±25ms/10% loss/reorder, screenshots inspected, clean exits.

**Staged dialogue stats:** stock 8 modified attributes/27 skills, current
health/magicka/fatigue and health percentage read authenticated character snapshots.
Result stat writes/unstaged filters closed. Isolation/waits/rollback/retry/truncation/recovery: `build/logs/m5-dialogue-player-stats.log`;
early-baseline installation: `build/logs/m5-dialogue-installation.log`.

**Stats slice accepted, Codex/owner, 2026-10-07:** [fixture](../../scripts/prepare_native_dialogue_fixture.py)
Strength <40/≥40, Destruction <20/≥20;
`build/m5-dialogue-ui-01/agent-signoff.json` screenshots inspected.
Scripted setup/custom mouse bindings.
Owner confirmed silent completed restart: north 20/30, south 20/20;
`build/logs/m5-dialogue-operator-restart-01.log`.
Pending-choice restart remains synthetic:
`build/m5-dialogue-stats-{desktop-02,restart-01}/result.json` (no screenshots).
Regression: `build/logs/m5-dialogue-player-stats-ui-signoff.log`.

**Dialogue substitutions:** bounded stock %/^ responses read authenticated character/speaker,
profile custom class, staged ranks/bounty/owned globals and stock cell names before results.
Private output/rollback/recovery:
`build/logs/m5-dialogue-substitutions{,-host,-client}.log`;
stats regression: `build/logs/m5-dialogue-substitutions-stats-regression.log`.
Paired impaired/restart semantic proof:
`build/m5-dialogue-substitutions-{desktop-02,restart-01}/result.json`;
foreground captures excluded; visual acceptance remains open.
V85 rejects older policies; action bindings closed.

**Disposition:** authenticated 0–100 thresholds share stock character/speaker calculations.
Private branches/waits/rollback/retry/silent recovery:
`build/logs/m5-dialogue-disposition{,-host}.log`. Paired impaired stock branches/pending-choice
restart: `build/m5-dialogue-disposition-{desktop-02,restart-01}/result.json`
(synthetic; no screenshots).

**Story-owned NPC relationships:** V85 source-only stock ModDisposition commits
≤64 placement/record/life-bound bases/character/story with banks/progression/
receipts/gameplay. Filters restore private bases; public combat/lives/inventory unchanged.
Synthetic isolation/late/overflow rollback/bounds/silent recovery:
`build/logs/m5-npc-state.log`; paired atomic durability/exact/uncertain restart:
`build/logs/m5-npc-state-host.log`. Scene edits/broader NPC restoration stay closed.

**Journal text/timestamps:** V85 commits bounded stock expansion from authenticated
character/speaker, result-time globals and authoritative calendar with private
progression. Stock journal views isolate stories; notifications follow durability;
recovery neither expands nor restamps/replays. Prior policies/frames reject.
Synthetic isolation/waits/rollback/bounds/recovery/client:
`build/logs/m5-journal{,-stock,-intents,-substitutions,-client,-legacy,-host,-adapter,-installation}.log`.
Desktop visual acceptance remains open; requires fresh V85 campaigns.

**Next:** coherent scene-changing dialogue scopes/NPC restoration
([M5 contract](DECISIONS.md#m5-implementation-contract)); scoped recovery and
unchanged Seyda Neen/TR proofs remain. M5 stays open.
