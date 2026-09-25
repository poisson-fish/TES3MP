# Current state and next action

**M4 in [PLAN.md](PLAN.md) is active.** V32 shares spell and `WhenUsed`
preparation. Ordinary spells now use OpenMW's skill, attribute, fatigue and
Sound/Silence success chance on staged actor stats. Failed casts pay magicka,
consume the roll and publish failure without effects or a projectile. Items
pay charge at launch. Up to eight durable projectiles retain source/target and
pending command identity.
Server contact selects Target area
effects across both players and NPC, excluding the caster from splash.
Stats, timed Resist Magicka, death and outcomes commit together. Offline
players and inactive NPCs pause expiry. V32 requires a fresh campaign.

[Host](../../apps/tes3mp-server/native/inventory_host.hpp): one traveler,
128 doors, two 60 Hz substeps; saturation pauses simulation. Deadlines survive restart.

Earlier evidence: NPC lifecycle (`build/logs/m4-npc-life-final-test.log`),
unarmed fatigue (`build/logs/m4-unarmed-effect-test-03.log`), two-desktop Self
casts (`build/m4-instant-spell-live-04/result.json`), `WhenUsed`
(`build/logs/m4-enchant-use-test-06.log`), timed resistance
(`build/logs/m4-timed-spell-test-08.log`) and Target area
(`build/logs/m4-area-test-03.log`).

Synthetic V31 player contact, NPC splash, paid item miss, rollback, retry,
recovery and expiry passed `build/logs/m4-player-target-test-final.log`.

Ordinary casts passed
`build/logs/m4-ordinary-cast-test-06.log` and
`build/logs/m4-ordinary-target-test-01.log`, including success/failure rolls,
cost, rollback, gating and restart.

Synthetic concurrent flights passed
`build/logs/m4-projectile-collection-test-final.log`: two same-tick contacts,
rejected write/retry, malformed bound and restart. The V31 player target
regression passed `build/logs/m4-projectile-collection-legacy-02.log`.

Background actors freeze. Projectiles may target NPC/player; player contact
uses a server-position proxy sphere. Bolt visuals are absent. Next: remaining
sources, knockout, unarmed health damage, armor, block and melee.
Player movement stays last after collision/smoothness checks.
