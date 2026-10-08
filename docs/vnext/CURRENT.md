# Current state and next action

**Active: M5 [PLAN.md](PLAN.md), native dialogue transactions. M4 closed, 2026-10-06.**
[Host](../../apps/tes3mp-server/native/inventory_host.hpp):
60 Hz NPC/door/player physics substeps; 30 Hz commits.

**V77 exploration:** radius 0–7; 256 cells, 37 actors, 8,192 references/region,
256 navigation tiles/actor. Shared collision/streaming retain state and frozen hulls.
Synthetic paired split/regroup/restart under 100ms ±25ms/10% loss/reorder:
`build/exploration-desktop-{06,restart-01}/result.json`. Scripts/leveled/corpses reject;
full AI packages remain M5.

**V78 characters:** stock/TR creation, starters, inventory/appearance installation,
reentry/reconnect/restart verified synthetically; starter session accepted 2026-10-04.
`build/character-desktop-fixture-02/{complete-03,restart-02}/result.json`.
Creation cannot simulate before completion. TR appearance-only; physical OS input unverified.
Resume retains inactivity/expiry/generation/replay bounds (`build/logs/resume-inactivity.log`).

**Death/respawn:** capability 37; life cleanup preserves ordinary inventory.
`build/logs/respawn-{protocol,adapter,native}.log`, `player-lives-regression.log`,
`chargen-respawn-launch-build.log`. Same-cell correction passed
(`build/logs/respawn-same-cell-{client-build,locomotion}.log`). Operator accepted
revival/Seyda Neen safe-point respawn, 2026-10-04; unarmed confirmed.

**Exterior trace:** `build/logs/seyda-six-actors.log`: six actors/nine interiors.
Single-cell preview disables travel. Excluded scripts/scripted inventory require M5;
restore only proven content.

**M4 combat/body acceptance:** melee accepted 2026-10-05; Argonian recoil,
fatigue/HUD and crab-contact damage passed (`build/logs/melee-recoil-{baseline,fixed}.log`).
Peer/creature corpse transfers, reconnect/restart preserve exact identities/counts
(`build/logs/peer-loot-{reconnect-retry,restart}.log`). Shared corpse accepted 2026-10-05;
body/door/NPC accepted 2026-10-06:
`build/m4-user-acceptance-body-door-npc-v78-20261005-230100/operator-acceptance.json`.
Split/regroup/restart preserve bodies/inventories/resources/NPC identities;
stale/resampled inventory rejects contradictions atomically. Doors/travel/quit
survive capture exhaustion; relay reclaims idle endpoints.

**V79 player cutover:** capability 38; latest-wins controls/acknowledgements, fixed
stock physics/shared player/NPC/door hulls, durable frames/resources and bounded replay.
Retry/hold/expiry/epoch/rollback/restart/ground regression:
`build/logs/player-{physics-native,physics-scene,physics-world,controls-protocol,controls-queue,controls-server,physics-adapter,physics-angles}.log`.
Two-desktop heading/jump/stop/contact/reconnect:
`build/player-physics-desktop-impaired-16/result.json`; screenshots inspected.
Direct bootstrap, then 100ms ±25ms/10% loss/reorder: corrections max 19.3/23.7,
mean 1.3/1.5 units (budgets 35/8).
Exact stopped positions/reconnect: `build/player-physics-desktop-restart-04/result.json`.
Clock recovery: `build/logs/player-physics-clock.log`.
Fresh campaigns require `player-physics`; V78 retains migration; movement skills await M5.

**V79 movement accepted, 2026-10-06:**
`build/m4-user-acceptance-player-v79-20261006-live-01/operator-acceptance.json`.
128-input saturation waits/retries; backpressure/adapter passed.
Clients quit (0); combat/death/loot/respawn retained. Abort resolved; reopen on recurrence.

**M5 shared execution:** [runtime](../../apps/tes3mp-server/native/world_script.hpp)
stock compiler/core opcodes, owned globals, staged locals/messages,
committed/deferred/rejected outcomes, bound GSCR/GLOB recovery.
Synthetic `build/logs/m5-world-script-{shared,rollback,recovery,admission,content}.log`:
Fargoth declarations pass; Vodunius/chargen/skipped APIs reject.

**V80 scheduling/delivery:** fresh bootstrap binds one script/0–16 globals;
calendar ownership rejects. World-tick invocations join gameplay durability;
ordinals survive skipped ticks. Installation/projection/reentry never execute
scripts. Background ticks continue.
Capability 39 delivers ≤8 literal messages of ≤256 bytes through atomic reliable admission
and stock UI; session/invocation checks suppress duplicates. Baselines/recovery never replay output.
Synthetic `build/logs/m5-ambient-{host,adapter,delivery,bootstrap,bootstrap-admission,bootstrap-zero}.log`:
atomic failure/retry/stale, offline/reentry/exact/uncertain restart,
malformed/budget/ownership rejection, V79 recovery/upgrade/downgrade rejection.
Inherited seam/movement recovery: `build/logs/m5-world-script-host-{shared,rollback}.log`,
`build/logs/m5-player-physics-regression.log`.
`build/m5-ambient-desktop-{02,restart-01,ui-01}/result.json`: paired occurrences,
eight-second absences, no missed-output replay, durable continuation, clean client exits;
100ms ±25ms/10% loss/reorder. Native-window message screenshots inspected.
Shared regression: `build/logs/m5-context-shared-regression.log`.

**M5 scoped context:** InterpreterContext scopes references by
campaign/story/physical/character/source/life/invocation/event identities. Unstaged services
reject; waits differ from absence. Personal text: staged stats.
Synthetic `build/logs/m5-context-{lookup,rollback,stock}.log`: isolation, stale/bounds
rejection, stock callers, rollback/retry and named-locals recovery.
**V81/V82 personal state/scheduling:** isolated characters/stories own globals/locals,
invocations/cursors; online established characters execute once/tick without offline catch-up.
Gameplay-coherent markers, silent replay and exact/uncertain recovery; legacy policies retained.
Synthetic `build/logs/m5-personal-{state,host,shared}.log`,
`build/logs/m5-personal-scheduling-{state,host,legacy-state,legacy-host}.log`.

**M5 cross-script seam:** staged owned banks route stock typed members,
StartScript/StopScript/ScriptRunning; ≤64 locals/128-byte names. Targets reject.
Synthetic `build/logs/m5-cross-script-{context,rollback,stock,lookup-regression}.log`:
character/story isolation, identity/bounds/waits, late-failure/durability rollback/retry,
stock GSCR locals/running recovery and stock callers. V81/V82 retain single-script admission.

**V83 native banks:** 1–4 scripts/story, ≤16 mirrored instances. Ordered locals/flags/
ordinals/markers and character/story globals commit with gameplay; StartScript/StopScript
preserve ownership and stopped locals.
Synthetic `build/logs/m5-script-banks-{state,host-05,legacy-state,legacy-host,trusted-host,shared}.log`:
late rejection/defer/rollback/retry/stale, offline/skipped ticks, stopped-state and
exact/uncertain recovery; V81/V82 preserved; incompatible policies reject.

**V84 scoped dialogue:** stock filters/results, journals/topics and bank receipts join gameplay.
Trusted-source isolation/rollback/recovery/stock/host remain synthetic:
`build/logs/m5-dialogue-{isolation,rollback,recovery,stock,host-05}.log`.

**V85 authenticated dialogue:** capability 40 binds character/story, living unscripted speaker,
reach/sight/life, revision and offered choice. ≤14 mirrored scripts reserve two intents/tick;
one/character. Banks/progression/choices/receipts commit with gameplay. Private literal
responses/messages and journal indices/finished flags present after durability; client results,
greetings, services and restock writes suppress. First topic seeds story knowledge.
Reconnect restores choices/progression silently. Synthetic
`build/logs/m5-dialogue-intents-{state,host,client,adapter,ingress,delivery}.log`:
two-client/story isolation, duplicates/conflicts, defer/rollback/retry/stale,
exact/uncertain restart and stock client presentation without Journal/World/Lua services.
Desktop acceptance, text substitutions, disposition thresholds, journal text/timestamps,
physical results and broader NPC restoration remain closed.

**Next:** further staged dialogue filter/result services
([M5 contract](DECISIONS.md#m5-implementation-contract)); physical results/NPC restoration remain later slices.
