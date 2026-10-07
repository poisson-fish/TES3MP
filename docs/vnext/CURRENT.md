# Current state and next action

**Active: M5 [PLAN.md](PLAN.md), shared-script scheduling implemented. M4 closed, 2026-10-06.**
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

**V78:** fresh players cannot simulate; starters are campaign-configured. Durable completion/
recovery owns stock bodies/stats/spells/resources/safe points; capabilities 6/36.
Appearances persist. Catalog pairs bound: 8,192/race, 65,536 total.
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
`build/character-acceptance-02/{drop,pickup-restart,reentry,movement}/result.json`: chest-owned shirt equip,
longsword drop/replication, three discovered interiors/reentry, durable ground restart,
pickup/restart, four-heading clips/reconnect; screenshots inspected.
`build/character-tr-acceptance/{complete,restart-facing}/result.json`: both Breton sexes/TR heads/hair,
empty inventory/recovery; appearance-only mods.
`build/chargen-ui-{pick,generate-03,create}/result.json`: MyGUI pick/questions/custom,
Back/refresh/birthsign/completion; injected custom names. Refresh retains the modal,
transitions hide others, resume ignores closed dialogs; physical OS input unverified.
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

**Melee accepted, 2026-10-05:** operator passed Argonian combat after recoil fix.
Synthetic stock input: recoil stall reproduced; nine swings passed
(`build/logs/melee-recoil-{baseline,fixed}.log`). Fatigue/HUD match (182.25–185).
Death label added.
Replica stock bounds/visibility; server rolls. Shieldless creatures need no inventory.
`build/logs/melee-contact-geometry.log`, `build/melee-contact-validation-01/result.json`:
crab contact/dagger damage.

**Shared corpse/recovery accepted, 2026-10-05:** operator passed peer visibility,
opening/transfer and reconnect/restart without duplication.
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
128-input saturation now waits/retries; backpressure/adapter checks passed.
Both fixed clients quit normally (0); combat/death/loot/respawn acceptance retained.
Native abort resolved for now; reopen on recurrence.

**M4 closed, 2026-10-06:** owner accepted overlapping two-client/NPC
melee/ranged/magic, vanilla/TR gear, impairment, interruption/death/loot/respawn
and recovery/convergence. Sign-off retains focused evidence; older open artifacts
predate closure. Accepted bounds and M5/M7 work remain.

**M5 shared execution:** [runtime](../../apps/tes3mp-server/native/world_script.hpp)
uses stock compiler/core opcodes, explicit globals, staged locals/messages,
committed/deferred/rejected outcomes and bound GSCR/GLOB recovery.
Synthetic `build/logs/m5-world-script-{shared,rollback,recovery,admission,content}.log`:
retry/failure/bounds/source/content checks; Fargoth declaration locals pass,
Vodunius/chargen reject. Strict warnings reject skipped APIs.

**V80 host scheduling:** fresh opt-in binds one script/0–16 globals;
calendar ownership rejects. World-tick invocations join gameplay durability;
ordinals survive skipped ticks. Installation/projection/reentry never execute
scripts. Background ticks continue.
Synthetic `build/logs/m5-world-script-host.log`: two views, rollback/retry/stale,
offline/reentry, exact/uncertain restart, malformed images, ownership/source rejection,
V79 exact recovery and upgrade/downgrade rejection.
`build/logs/m5-world-script-host-{shared,rollback}.log`: seam regressions pass.
`build/logs/m5-player-physics-regression.log`: V79 movement/recovery passed.
Ambient delivery pending; NPC restoration/personal context/dialogue remain closed.

**Next:** deliver bounded committed ambient output to both clients; add
bootstrap admission and prove two-desktop reentry/restart under the
[M5 contract](DECISIONS.md#m5-implementation-contract). Preserve old images and M4 behavior.
