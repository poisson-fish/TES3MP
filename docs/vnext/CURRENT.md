# Current state and next action

**Active: M5 [PLAN.md](PLAN.md), native script banks. M4 closed, 2026-10-06.**
[Host](../../apps/tes3mp-server/native/inventory_host.hpp):
60 Hz NPC/door/player physics substeps; 30 Hz commits.

**V77 exploration accepted:** radius 0–7 anchors share 256 cells; caps: 37 actors,
8,192 references/region, 256 navigation tiles/actor. Connected regions share collision;
streamed geometry retains state/frozen hulls/contact witnesses.
Synthetic `build/logs/exploration-{bootstrap-radius2,discovery-03,regions-03,host-07}.log`:
27-cell export, exhaustion/retry, 12-cell navigation/contact,
geometry rejection/release, overlapping demand/split/regroup/rollback/recovery.
`build/logs/exploration-{cell-actors-02,placed-actors-01,boundary}.log`:
collision/combat/flight regression, unsupported actor rejection, engine independence.
Scripts/leveled/corpses reject; full AI packages remain M5.

**Synthetic exploration:**
`build/exploration-desktop-{06,restart-01}/result.json`: two desktops cross
3→6→3 via AutoMove, split/regroup/unload/reentry/reconnect;
restart preserves item identities/counts and adjacent-cell damage.
Desktop crossings: 100ms delay, ±25ms jitter, 10% loss/reorder. `build/exploration-desktop-presentation-02/result.json`:
actors/peer recovered; screenshots inspected.

**V74–V76:** 143 selectors/141 lifecycles; capability 34/35 presentation.
`build/logs/{target-recipients-08,source-limits-02,spell-presentation-transaction-04,spell-passive-self-18,spell-passive-stats-08,world-collision-09}.log`.
Stock AI excludes powers; WhenUsed needs equipment; passives Self,
travel players, Soultrap creatures. Two summons are stock placeholders.
Animated ground rejects. Bind intervention destinations.

**V78:** fresh players cannot simulate; campaign-configured starters. Completion/recovery
persists stock bodies/stats/spells/resources/safe points/appearances; capabilities 6/36.
Catalog pairs: 8,192/race, 65,536 total.
`build/logs/character-{catalog-bounds,appearances-stock,appearances-tr}.log`: ten races/both sexes, stock/TR.
Starter grant/validation/recovery: `build/logs/character-starters-native.log`.
Desktop starters: `build/m4-user-acceptance-starters-20261004-210941/starter-installation.json`;
session accepted 2026-10-04.
Delayed lanes resync; empty inventories clear.
Gear/angles preserve body/motion.
Synthetic `build/logs/{chargen-installer-15,chargen-installation,chargen-install-protocol,chargen-install-server-delivery,chargen-install-server-reverse,chargen-install-restart,appearance-protocol,appearance-adapter,appearance-installer,chargen-clock,appearance-clock-adapter,public-outfit-adapter,public-outfit-body-contract,remote-locomotion-coordinates,chargen-control}.log`.

**Synthetic character acceptance:**
`build/character-desktop-fixture-02/{complete-03,restart-02}/result.json`,
`build/character-acceptance-02/{drop,pickup-restart,reentry,movement}/result.json`,
`build/character-tr-acceptance/{complete,restart-facing}/result.json`:
stock/TR creation/recovery, empty inventories, outfits/drop/pickup,
interior reentry/facing/reconnect; screenshots inspected. Appearance-only TR mods.
`build/chargen-ui-{pick,generate-03,create}/result.json`: MyGUI pick/questions/custom,
Back/refresh/birthsign/completion; custom names. Refresh retains the modal;
transitions/resume respect closed dialogs; physical OS input unverified.
Resume bounds inactivity, retaining expiry/generation/replay rejection
(`build/logs/resume-inactivity.log`).

**Death/respawn:** capability 37; life cleanup preserves ordinary inventory.
`build/logs/respawn-{protocol,adapter,native}.log`, `player-lives-regression.log`,
`chargen-respawn-launch-build.log`. Same-cell correction passed
(`build/logs/respawn-same-cell-{client-build,locomotion}.log`). Operator accepted
revival/Seyda Neen safe-point respawn, 2026-10-04; unarmed confirmed.

**Exterior trace:** `build/logs/seyda-six-actors.log`: six actors/nine interiors.
Single-cell preview disables travel. Excluded scripts/scripted inventory require M5;
restore only proven content.

**Melee accepted, 2026-10-05:** Argonian recoil fix; fatigue/HUD match; death label.
`build/logs/melee-recoil-{baseline,fixed}.log`.
Replica stock bounds/visibility; server rolls. Shieldless creatures need no inventory.
`build/logs/melee-contact-geometry.log`, `build/melee-contact-validation-01/result.json`:
crab contact/dagger damage.

**Shared corpse/recovery accepted, 2026-10-05:** peer visibility/opening/transfer/reconnect/restart
without duplication.
`build/m4-user-acceptance-peer-loot-20261005-221147/operator-acceptance.json`;
exact player stacks/bodies: `build/logs/peer-loot-{reconnect-retry,restart}.log`.
Neighbor-creature/numeric activation/transfer passed
(`build/logs/shared-corpse-creature-native-02.log`, `build/shared-player-corpse-validation-01/result.json`).
Creature respawn: plain stores/absent destinations.

**Body/door/NPC accepted, 2026-10-06:** `build/m4-user-acceptance-body-door-npc-v78-20261005-230100/{door-pass,restart-pass,door-repro,operator-acceptance}.json`:
split/regroup/reconnect/restart preserved bodies/inventories/resources/NPC identities.
Automated doors passed; recorder exhaustion fixed:
`build/m4-user-acceptance-body-door-npc-v78-20261005-230100/capture-limit-result.json`
doors/travel/quit survive exhaustion.
Stale/resampled inventory preserves state; contradictions reject atomically
(`build/logs/door-stale-baseline-fixed.log`). Relay reclaims idle endpoints
(`build/logs/door-relay-expiry.log`).

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
**V81 personal state:** 1–8 stories/two characters own stock globals/locals/invocations/causal cursors.
≤16 scoped source-free calls join gameplay commits; replay is silent, rejection atomic.
Synthetic `build/logs/m5-personal-{state,host,shared}.log`: isolation, rollback/retry/stale/bounds,
content/ownership-bound exact/uncertain recovery, upgrade/downgrade rejection; V80 preserved.

**V82 personal scheduling:** established online characters/stories execute once/host tick;
offline/skipped ticks never catch up. Durable markers; silent installation/output. V81 retains trusted calls.
Synthetic
`build/logs/m5-personal-scheduling-{state,host,legacy-state,legacy-host}.log`:
isolation, late-failure rollback/retry/stale, exact/uncertain physics recovery,
future-marker and V81 upgrade/downgrade rejection.

**M5 cross-script seam:** staged owned banks route stock typed members,
StartScript/StopScript/ScriptRunning; ≤64 locals/128-byte names. Targets reject.
Synthetic `build/logs/m5-cross-script-{context,rollback,stock,lookup-regression}.log`:
character/story isolation, identity/bounds/waits, late-failure/durability rollback/retry,
stock GSCR locals/running recovery and stock callers. V81/V82 retain single-script admission.

**V83 native banks:** 1–4 scripts/story, ≤16 mirrored instances. Admission order;
StartScript enables later members, StopScript retains locals. Character/story globals,
typed/self locals, running flags and member ordinals commit with gameplay/markers.
Synthetic `build/logs/m5-script-banks-{state,host-05,legacy-state,legacy-host,trusted-host,shared}.log`:
late rejection/defer/rollback/retry/stale, offline/skipped ticks, stopped-state and
exact/uncertain recovery; V81/V82 preserved; incompatible policies reject.

**Next:** dialogue
([M5 contract](DECISIONS.md#m5-implementation-contract)). Personal output delivery,
physical-reference operations and NPC restoration remain closed.
