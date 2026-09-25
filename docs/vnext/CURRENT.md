# Current state and next action

**M4 in [PLAN.md](PLAN.md) is active.** V31 shares spell and `WhenUsed`
preparation. Spells pay magicka; items pay charge at launch. One durable
projectile retains source/target identity. Server contact selects Target area
effects across both players and NPC, excluding the caster from splash.
Stats, timed Resist Magicka, death and outcomes commit together. Offline
players and inactive NPCs pause expiry. V31 requires a fresh campaign.

[Host](../../apps/tes3mp-server/native/inventory_host.hpp): one interior or
nine exteriors, one traveler, 128 doors, two 60 Hz substeps per 30 Hz tick.
Saturation pauses both; travel/respawn deadlines survive restart.

Verified: NPC death/loot/respawn/restart (`build/logs/m4-npc-life-final-test.log`),
unarmed fatigue (`build/logs/m4-unarmed-effect-test-03.log`,
`build/logs/m4-unarmed-live-07/result.json`), and two-desktop Self casts under
latency/loss (`build/m4-instant-spell-live-04/result.json`). Synthetic V28
spell/`WhenUsed` hit, miss and recovery passed
`build/logs/m4-enchant-use-test-06.log`; V29 timed resistance and offline
freeze passed `build/logs/m4-timed-spell-test-08.log`. Synthetic V30 area
selection, rollback and restart passed `build/logs/m4-area-test-03.log`.

Verified 2026-09-25: synthetic V31 `player-target-spell` passed
`build/logs/m4-player-target-test-final.log`: player contact and NPC splash, paid `WhenUsed`
miss, timed resistance, rollback, pending retry rejection, malformed recovery,
restart and expiry. V30 area regression passed
`build/logs/m4-area-v31-regression-01.log`.

Background actors freeze. One projectile may target the NPC or a player;
player contact uses server positions and a proxy sphere. Client bolt
presentation is absent. Next: broaden sources and allow concurrent projectiles,
then finish knockout, unarmed health damage, armor, block and melee before
broader two-desktop proof. Player movement stays last after collision/smoothness checks.
