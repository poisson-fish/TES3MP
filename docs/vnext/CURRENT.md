# Current state and next action

**M4 in [PLAN.md](PLAN.md) is active.** V30 shares spell and `WhenUsed`
effect preparation. Spells pay magicka; items pay charge at launch. One
projectile retains source identity. Server contact provides the impact point;
Target area effects up to 64 feet select the two players and NPC, excluding
the caster from splash. One tick commits stats, timed Resist Magicka, death
and one outcome per target. Offline players and inactive NPCs pause expiry.
V30 requires a fresh campaign.

[Host](../../apps/tes3mp-server/native/inventory_host.hpp): one interior or
nine exteriors, one traveler, 128 doors, two 60 Hz substeps per 30 Hz tick.
Saturation pauses both. Travel/respawn deadlines survive restart; offline
players freeze.

Verified 2026-09-24: `build/logs/m4-npc-life-final-test.log` covers death,
loot, respawn and restart. Unarmed fatigue passed
`build/logs/m4-unarmed-effect-test-03.log` and
`build/logs/m4-unarmed-live-07/result.json`.

Verified 2026-09-24: `build/m4-instant-spell-live-04/result.json` captures two
desktops casting Self under latency/loss. Synthetic V28 spell and `WhenUsed`
hit/miss, rollback, retries, restart and reconnect passed
`build/logs/m4-enchant-use-test-06.log`. V29 `npc-timed-spell` passed
`build/logs/m4-timed-spell-test-08.log`: rejected rollback, restart/expiry,
offline freeze, Target resistance and post-restart damage.

Verified 2026-09-25: synthetic V30 `npc-area-spell` passed
`build/logs/m4-area-test-03.log`: spell and `WhenUsed` impact, near/far actor
selection, caster exclusion, one outcome per target, rejected-write rollback
and restart. V29 `npc-timed-spell` passed
`build/logs/m4-area-v29-regression-01.log` after the shared-path change.

Background actors freeze. One projectile aims at the NPC; client bolt
presentation is absent. Area support covers Target effects and three actors;
timed effects cover Resist Magicka. Wider sources, targets, concurrent
projectiles, AI, armor/block, unarmed knockout/health damage and player hulls
remain unwired. Next: generalize casts and projectiles, then finish melee
before broader two-desktop proof. Player movement remains last, after
collision/smoothness checks.
