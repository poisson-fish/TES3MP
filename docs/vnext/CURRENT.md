# Current state and next action

**M4 in [PLAN.md](PLAN.md) is active.** V32 shares spell and `WhenUsed`
preparation. Ordinary casts use OpenMW success chance; failures spend magicka
without effects. Items pay charge at launch. Eight durable projectiles retain
source, target and command identity. Server contact applies Target areas to
players and NPCs, excluding caster splash. Stats, timed resistance, death and
outcomes commit together. Inactive actors pause expiry. V32 requires a fresh campaign.

[Host](../../apps/tes3mp-server/native/inventory_host.hpp): one traveler,
128 doors, two 60 Hz substeps; saturation pauses simulation. Deadlines survive restart.

Earlier evidence: NPC lifecycle (`build/logs/m4-npc-life-final-test.log`),
unarmed fatigue (`build/logs/m4-unarmed-effect-test-03.log`), two-desktop Self
casts (`build/m4-instant-spell-live-04/result.json`), `WhenUsed`
(`build/logs/m4-enchant-use-test-06.log`), timed resistance
(`build/logs/m4-timed-spell-test-08.log`) and Target area
(`build/logs/m4-area-test-03.log`).

V31 player contact, splash and recovery passed
`build/logs/m4-player-target-test-final.log`.

Ordinary cast rolls, rollback and restart passed
`build/logs/m4-ordinary-cast-test-06.log` and
`build/logs/m4-ordinary-target-test-01.log`.

Synthetic concurrent flights passed
`build/logs/m4-projectile-collection-test-final.log`: two same-tick contacts,
rejected write/retry, malformed bound and restart. The V31 player target
regression passed `build/logs/m4-projectile-collection-legacy-02.log`.

Synthetic cast-once enchanted item launch passed
`build/logs/m4-castonce-test-verify.log`: a rejected launch retained the item;
a committed launch consumed its single unscripted instance and retained the
projectile through restart and contact. Multi-count and scripted cast-once
items remain unsupported.

Background actors freeze. Projectiles may target NPC/player; player contact
uses a server-position proxy sphere. Bolt visuals are absent. Next: remaining
cast sources, then knockout, unarmed health damage, armor, block and melee.
Player movement stays last after collision/smoothness checks.
