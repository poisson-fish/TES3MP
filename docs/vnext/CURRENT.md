# Current state and next action

**M4 in [PLAN.md](PLAN.md) is active.** V27 prepares spell/enchantment effects
through one plan. Self spells spend magicka and apply fixed restorations. A
Target Damage Health spell spends magicka at launch, persists one projectile,
then resolves server actor/world contact through OpenMW resistance and stats.
Launch, flight and outcome use composed durable ticks. V27 requires a fresh
campaign; M4 remains open.

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
`build/logs/m4-target-projectile-test-09.log` passes synthetic hit/miss,
rejected writes, duplicate use, flight restart, two-client reconnect and
invalid saved source rejection.

Background actors freeze. The projectile aims at the NPC at launch; only one
can fly, with no client bolt presentation. Active resistance effects are not
retained in combat stats; headless tests prove full Resist Magicka. Enchantment
use, wider magic, AI, armor/block, unarmed knockout/health damage and player
hulls remain unwired. Movement is inherited. Next: enchantment charge/use on
the shared path, then duration/area and remaining melee rules. Cut player
movement over last after collision/smoothness checks.
