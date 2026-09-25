# Current state and next action

**M4 in [PLAN.md](PLAN.md) is active.** V26 adds one native spell slice to
V25's placed NPC life cycle. An authenticated, base-known OpenMW spell with one
fixed, zero-duration Self Restore Health effect and the Always Succeeds flag
spends OpenMW spell cost and restores health in the composed server tick. The
health, magicka, actor state and reliable event commit together. V26 requires a
fresh campaign. Other spell effects, target types, spell acquisition and
projectiles remain unwired.

[Host](../../apps/tes3mp-server/native/inventory_host.hpp): one dry interior
or nine adjacent exteriors, one traveler, 0–128 doors, two 60 Hz substeps per
30 Hz tick. Saturation pauses both steps. Travel and NPC respawn deadlines
persist without players and across restart; offline players freeze.

Verified 2026-09-24: V25 `npc-life-cycle` in
`build/logs/m4-npc-life-final-test.log` covers death, loot, respawn and restart.
The first combat effect, OpenMW hand-to-hand fatigue damage, passed
`build/logs/m4-unarmed-effect-test-03.log` and the two-desktop capture at
`build/logs/m4-unarmed-live-07/result.json`.

Verified 2026-09-24: V26 `npc-instant-spell` in
`build/logs/m4-instant-spell-test-07.log` covers invalid source/target,
rejected durability, two projections, reducer routing, duplicate receipt and
restart/reconnect. The two-desktop capture
`build/m4-instant-spell-live-04/result.json` uses a synthetic room on a real
loadout and shows Alice healed from 27.8 to
35 and spent one magicka; both clients received one event, and Bob's reconnect
retained the result under 100 ms one-way latency, jitter and 10% loss.
Production server/client builds:
`build/logs/m4-instant-spell-server-build-02.log`,
`build/logs/m4-instant-spell-client-build-03.log`.

Background actors freeze. General AI, scripts, water, sliding neighborhoods,
armor, block, resistances, sustained magic effects, broad spells/projectiles,
enchantments, unarmed knockout/health damage, werewolf scaling and player
hulls remain unwired. Player movement is inherited. Next: one target spell
projectile with server-owned flight/contact, then enchantments. Cut player
movement over last, after shared collision and smoothness are verified.
