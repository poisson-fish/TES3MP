# Current state and next action

**M4 in [PLAN.md](PLAN.md) is active.** V33 saves fatigue knockout for
players and the NPC. Active actors recover at OpenMW's combat
fatigue rate; knocked actors cannot attack, and unarmed hits then damage
health. The synthetic fixture verifies rejection, restart, malformed
restore, recovery and two-client outcomes
(`build/logs/m4-knockout-test-final-03.log`). Fresh campaign required.

V32 shares spell and `WhenUsed` preparation. Casts spend magicka and items
pay charge at launch. Eight durable projectiles resolve server contact,
Target areas, timed resistance and death atomically. Inactive
actors pause expiry. Two desktop clients showed Self casts
(`build/m4-instant-spell-live-04/result.json`); concurrent flights passed
`build/logs/m4-projectile-collection-test-final.log`.

NPC and player hits trigger script-free equipped `WhenStrikes` weapons.
Physical damage, wear, charge, supported Self/Touch/Target effects, RNG,
death and hit outcome share one commit. Misses retain charge. The fixture passed
`build/logs/m4-strike-test-final.log` and
`build/logs/m4-strike-projectile-regression.log`.

[Host](../../apps/tes3mp-server/native/inventory_host.hpp): one traveler,
128 doors, two 60 Hz substeps. Background
actors freeze. Player contact uses a server-position proxy sphere. Bolt
visuals and knockout animation timing are absent. Next: armor, block and
remaining melee rules. Scripted items and other cast sources remain.
Player movement follows collision and smoothness checks.
