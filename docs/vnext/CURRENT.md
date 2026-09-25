# Current state and next action

**M4 in [PLAN.md](PLAN.md) is active.** V26 adds an authenticated, base-known
OpenMW spell with one fixed, zero-duration Self Restore Health effect and Always
Succeeds. Its cost, healing, actor state and event commit in one server tick.
V26 requires a fresh campaign. Other effects, targets, spell acquisition and
projectiles remain unwired. Armed melee, unarmed fatigue and this spell are
bounded proofs; M4 remains open until [PLAN.md](PLAN.md)'s general combat exit.

[Host](../../apps/tes3mp-server/native/inventory_host.hpp): one dry interior
or nine exteriors, one traveler, up to 128 doors, two 60 Hz substeps per
30 Hz tick. Saturation pauses both. Travel/respawn deadlines persist without
players and across restart; offline players freeze.

Verified 2026-09-24: V25 `npc-life-cycle` in
`build/logs/m4-npc-life-final-test.log` covers death, loot, respawn and restart.
OpenMW hand-to-hand fatigue damage passed
`build/logs/m4-unarmed-effect-test-03.log` and the two-desktop capture at
`build/logs/m4-unarmed-live-07/result.json`.

Verified 2026-09-24: `npc-instant-spell` in
`build/logs/m4-instant-spell-test-07.log` covers rejected source/target/write,
two clients, reducer routing, duplicates and restart. Two-desktop capture
`build/m4-instant-spell-live-04/result.json` used synthetic placements with a
real loadout: Alice healed 27.8 to 35 for one magicka; both saw one event and
Bob retained it after reconnect under 100 ms latency, jitter and 10% loss.
Server/client builds:
`build/logs/m4-instant-spell-server-build-02.log`,
`build/logs/m4-instant-spell-client-build-03.log`.

Background actors freeze. General AI, scripts, water, sliding neighborhoods,
armor, block, resistances, sustained magic effects, broad spells/projectiles,
enchantments, unarmed knockout/health damage, werewolf scaling and player
hulls remain unwired. Player movement is inherited. Next: one target spell
projectile with server-owned flight/contact, then enchantments. Cut player
movement over last, after shared collision and smoothness are verified.
