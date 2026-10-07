# Current state and next action

**Active: M5 [PLAN.md](PLAN.md), durable personal host transactions. M4 closed, 2026-10-06.**
[Host](../../apps/tes3mp-server/native/inventory_host.hpp):
60 Hz NPC/door/player physics substeps; 30 Hz commits.

**V77 exploration accepted:** radius 0–7 anchors share 256 cells; caps: 37 actors,
8,192 references/region, 256 navigation tiles/actor. Connected regions share collision;
streamed geometry retains state/frozen hulls/contact witnesses. One writer.
Synthetic `build/logs/exploration-{bootstrap-radius2,discovery-03,regions-03,host-07}.log`:
27-cell export, remote-anchor exhaustion/retry, 12-cell navigation/contact,
geometry rejection/release, overlapping demand, split/regroup, rollback/retry/recovery.
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

**Synthetic acceptance:**
`build/character-desktop-fixture-02/{complete-03,restart-02}/result.json`: Breton female/
Argonian male completion/recovery, empty inventories.
`build/character-acceptance-02/{drop,pickup-restart,reentry,movement}/result.json`: shirt equip,
longsword drop/replication, three interiors/reentry, durable ground pickup/restart,
four-heading clips/reconnect; screenshots inspected.
`build/character-tr-acceptance/{complete,restart-facing}/result.json`: both Breton sexes/TR heads/hair,
empty inventory/recovery; appearance-only mods.
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

**Inherited desktops:** `build/v76-collision-acceptance-10/result.json` (100ms delay,
±25ms jitter, 10% loss), `build/spell-v74-acceptance-23/result.json`,
`build/combat-acceptance-16/result.json`.
[Stock comparisons](../../scripts/native_spell_presentation_capture.py) prove dispatch only.

**Exterior trace:** `build/logs/seyda-six-actors.log`: six actors/nine interiors.
Single-cell preview disables travel. Excluded scripts/scripted inventory require M5;
restore only proven content.

**Melee accepted, 2026-10-05:** Argonian recoil fix; reproduced stall, nine stock-input swings
(`build/logs/melee-recoil-{baseline,fixed}.log`). Fatigue/HUD match (182.25–185).
Death label added.
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
Automated doors passed; recorder exhaustion closed clients. Fixed:
`build/m4-user-acceptance-body-door-npc-v78-20261005-230100/capture-limit-result.json`
proves doors/travel/quit after exhaustion.
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

**M4 closed, 2026-10-06:** accepted two-client/NPC melee/ranged/magic, vanilla/TR gear,
impairment, interruption/death/loot/respawn and recovery/convergence.

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

**M5 scoped context:** InterpreterContext scopes both reference paths by
campaign/story/physical/character/source/life/invocation/event identities. Unstaged services
reject; waits differ from absence. Personal text uses staged stats.
Synthetic `build/logs/m5-context-{lookup,rollback,stock}.log`: isolation, stale/bounds
rejection, stock callers, rollback/retry and named-locals recovery.
**V81 personal state:** 1–8 stories/two characters own stock globals/named locals,
invocations and ordered causal cursors. Source-free core calls use scoped context;
≤16 trusted invocations join actor/inventory/player-physics commits. Replays are silent;
invalid calls reject the whole candidate. Content/ownership-bound recovery rejects
upgrades/downgrades. Synthetic `build/logs/m5-personal-{state,host,shared}.log`:
isolation, rollback/retry/stale/bounds, exact/uncertain recovery; V80 preserved.

**Next:** personal scheduling, then cross-script calls/dialogue under the
[M5 contract](DECISIONS.md#m5-implementation-contract). Personal output delivery,
physical-reference operations and NPC restoration remain closed.
