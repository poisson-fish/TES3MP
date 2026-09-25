# Current state and next action

**M4 in [PLAN.md](PLAN.md) is active.** V28 prepares spell and `WhenUsed`
enchantment effects through one range plan. Self spells spend magicka and
apply fixed restorations. Target spells spend magicka at launch; enchanted
items spend instance charge in the same durable launch tick. One pending
projectile retains source kind, instance or spell ID, and effect record ID;
server actor/world contact resolves the effect through OpenMW resistance and
stats. V28 requires a fresh campaign; M4 remains open.

[Host](../../apps/tes3mp-server/native/inventory_host.hpp): one interior or
nine exteriors, one traveler, 128 doors, two 60 Hz substeps per 30 Hz tick.
Saturation pauses both. Travel/respawn deadlines survive restart; offline
players freeze.

Verified 2026-09-24: `build/logs/m4-npc-life-final-test.log` covers death,
loot, respawn and restart. Unarmed fatigue passed
`build/logs/m4-unarmed-effect-test-03.log` and desktop capture
`build/logs/m4-unarmed-live-07/result.json`.

Verified 2026-09-24: `build/m4-instant-spell-live-04/result.json` captures two
desktops casting Self under latency/loss; `build/logs/m4-self-spell-compat-01.log`
passes the V26 Self regression. `npc-target-spell` in
`build/logs/m4-target-projectile-test-09.log` passes the V27 spell fixture.
`tes3mp_native_loadout_tests` built in `build/logs/m4-enchant-build-08.log`;
`npc-target-spell` in `build/logs/m4-enchant-use-test-06.log` passes synthetic
V28 spell and `WhenUsed` hit/miss, charge rollback, stale/duplicate requests,
invalid saved source, flight restart and two-client reconnect.

Background actors freeze. The projectile aims at the NPC at launch; only one
can fly, with no client bolt presentation. Active resistance effects are not
retained in combat stats; headless tests prove full Resist Magicka. Wider
magic, AI, armor/block, unarmed knockout/health damage and player hulls remain
unwired. Movement is inherited. Next: duration/area effects, retained active
resistances and remaining melee rules, then broader two-desktop proof. Cut
player movement over last after collision/smoothness checks.
