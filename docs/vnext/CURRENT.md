# Current state and next action

**M4 in [PLAN.md](PLAN.md) is active.** V29 prepares spell and `WhenUsed`
enchantment effects through one range plan. Spells pay magicka; items pay
charge with launch. One pending projectile retains source identity; server
contact resolves OpenMW resistance and stats. V29 retains timed Resist Magicka
on the caster or selected NPC. Expiry pauses offline players and inactive NPCs.
V29 requires a fresh campaign; M4 remains open.

[Host](../../apps/tes3mp-server/native/inventory_host.hpp): one interior or
nine exteriors, one traveler, 128 doors, two 60 Hz substeps per 30 Hz tick.
Saturation pauses both. Travel/respawn deadlines survive restart; offline
players freeze.

Verified 2026-09-24: `build/logs/m4-npc-life-final-test.log` covers death,
loot, respawn and restart. Unarmed fatigue passed
`build/logs/m4-unarmed-effect-test-03.log` and
`build/logs/m4-unarmed-live-07/result.json`.

Verified 2026-09-24: `build/m4-instant-spell-live-04/result.json` captures two
desktops casting Self under latency/loss; `build/logs/m4-self-spell-compat-01.log`
passes the V26 Self regression. `npc-target-spell` in
`build/logs/m4-target-projectile-test-09.log` passes the V27 spell fixture.
`npc-target-spell` in `build/logs/m4-enchant-use-test-06.log` covers synthetic
V28 spell and `WhenUsed` hit/miss, rollback, retries, restart and reconnect.
V29's synthetic `npc-timed-spell` passed `build/logs/m4-timed-spell-test-08.log`:
rejected cast rollback, self effect restart/expiry, offline freeze, Target
resistance contact, and post-restart resisted damage. V28's `npc-target-spell`
passed `build/logs/m4-v28-regression-timed-01.log`.

Background actors freeze. The projectile aims at the NPC at launch; only one
can fly, with no client bolt presentation. Timed effects currently cover only
Resist Magicka with zero area; other active effects remain unwired. Wider
sources, targets, concurrent projectiles, AI, armor/block, unarmed knockout/
health damage and player hulls remain unwired. Movement is inherited. Next:
add area effects on the shared cast path, then generalize sources/targets and
projectiles and finish melee before broader two-desktop proof. Cut player
movement over last after collision/smoothness checks.
