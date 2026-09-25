# Current state and next action

**M4 in [PLAN.md](PLAN.md) is active.** V26 accepts base-known Always Succeeds
spells with up to eight fixed, zero-duration Self Restore Health, Magicka or
Fatigue effects. An OpenMW-backed runtime validates and resolves casts; one
tick commits costs, stats and events. V26 requires a fresh campaign. Targets,
acquisition and projectiles remain unwired. M4 remains open.

[Host](../../apps/tes3mp-server/native/inventory_host.hpp): one interior or
nine exteriors, one traveler, 128 doors, two 60 Hz substeps per 30 Hz tick.
Saturation pauses both. Travel/respawn deadlines survive restart; offline
players freeze.

Verified 2026-09-24: V25 `npc-life-cycle` in
`build/logs/m4-npc-life-final-test.log` covers death, loot, respawn and restart.
OpenMW hand-to-hand fatigue damage passed
`build/logs/m4-unarmed-effect-test-03.log` and the two-desktop capture at
`build/logs/m4-unarmed-live-07/result.json`.

Verified 2026-09-24: `build/logs/m4-instant-spell-test-07.log` covers rejection,
two clients, reducer routing, duplicates and restart. Two-desktop capture
`build/m4-instant-spell-live-04/result.json` used synthetic placements and a
real loadout: Alice healed 27.8 to 35 for one magicka; both saw one event;
Bob retained it after reconnect under 100 ms latency, jitter and 10% loss.

Verified 2026-09-24: `build/logs/m4-cast-path-test-03.log` passes the original
cast regression plus mixed Restore Health/Magicka commit and restart. Mixed
effects have headless evidence only.

Background actors freeze. General AI, scripts, water, sliding neighborhoods,
armor, block, resistances, sustained magic effects, broad spells/projectiles,
enchantments, unarmed knockout/health damage, werewolf scaling and player
hulls remain unwired. Player movement is inherited. Next: one target spell
projectile through the shared cast/effect path with server-owned flight/contact,
effect resolution and restart/reconnect, then enchantments. Cut player
movement over last, after shared collision and smoothness are verified.
