# Current state and next action

**M4 in [PLAN.md](PLAN.md) is active.** V25 persists one initially living
placed NPC's life generation, attributed deaths and respawn deadline. Death
opens shared corpse loot. Respawn atomically restores actor state and baseline
inventory with fresh item identities; stale-life requests and rejected writes
leave the prior state intact. V25 needs a fresh campaign and `respawn TICKS`.

[Host](../../apps/tes3mp-server/native/inventory_host.hpp):
**native-inventory-25**, one dry interior or nine adjacent exteriors, one
traveler, 0–128 doors, two 60 Hz substeps per 30 Hz tick. Saturation pauses
both steps. Travel and life deadlines persist without players and across
restart; offline players freeze.

Verified 2026-09-24: `npc-life-cycle` in
`build/logs/m4-npc-life-final-test.log` covers death, malformed history,
loot, rejected respawn, fresh identities, stale-life rejection and restart.
Builds: `build/logs/m4-npc-life-final-server-build.log`,
`build/logs/m4-immediate-resume-desktop-build.log`.

The live Raflod kill, immediate Bob reconnect and GUI corpse loot passed in
`build/logs/m4-npc-life-live-15/result.json`. Moving-NPC reconnect:
`build/logs/m4-immediate-resume-live-01/result.json`.

The first bounded combat effect is ordinary hand-to-hand fatigue damage
against a standing target. OpenMW's melee calculation and stat mutation run
inside the native server tick; its fatigue result, damage type and actor state
share the attack's durability and replication. `npc-life-cycle` in
`build/logs/m4-unarmed-effect-test-03.log` covers rejection, both session
projections and restart/reconnect. The live two-desktop capture
`build/logs/m4-unarmed-live-07/result.json` records one 50-point hit: both
clients saw Raflod's fatigue fall from 219 to 169 while health stayed 100;
Bob's reconnect retained 169 under 100 ms one-way latency, jitter and 10% loss.

Background actors freeze. General AI, scripts, water, sliding neighborhoods,
armor, block, resistances, magic effects, unarmed knockout/health damage,
werewolf/nondefault strength scaling and player hulls remain unwired. Player
movement is inherited. Next: spells/projectiles, then enchantments. Cut player
movement over last, after shared collision and smoothness are verified. M3
content campaigns: `build/m3-tr-varyon-doors`, `build/m3-tr-noran-dry`.
