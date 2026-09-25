# Current state and next action

**M4 in [PLAN.md](PLAN.md) is active.** V34 gates shield blocks on
OpenMW's weapon-type carried-left visibility (including two-handed weapons) and a durable
hit-recovery counter taken from bound hit animation keys. Recovery pauses for
inactive actors. Synthetic animation, composed defense, rejection, malformed
restore, restart and V33 knockout regression passed
`build/logs/m4-melee-defense-scheduling-final.log`,
`build/logs/m4-melee-defense-recovery-gate-final.log` and
`build/logs/m4-melee-defense-knockout-regression-final.log`. V34 requires a fresh
campaign. All defenders currently use the selected NPC's bound hit resource;
distinct player animation resources and live-client timing need proof.

V33 saves player/NPC knockout. Actors recover fatigue at OpenMW's rate;
knocked actors cannot attack and take unarmed health damage. Two-client
outcomes passed `build/logs/m4-knockout-test-final-03.log`.

Composed hits apply OpenMW armor rating, minimum damage, weighted contact,
wear, shield facing/eligibility, block rolls and fatigue costs to player/NPC
defenders. Equipment, stats, RNG and outcome commit together. Synthetic
rejection, damage/block and restart passed
`build/logs/m4-armor-block-test-final.log`. Knockdown rolls and other melee
rules remain.

V32 spells/`WhenUsed` pay at launch; eight durable projectiles resolve
contact, Target areas, timed resistance and death. Inactive actors pause
expiry. Two clients showed Self casts (`build/m4-instant-spell-live-04/result.json`);
concurrent flights passed `build/logs/m4-projectile-collection-test-final.log`.

Script-free equipped `WhenStrikes` compose damage, wear, charge,
Self/Touch/Target effects, RNG and death; misses retain charge.
Checks: `build/logs/m4-strike-test-final.log`,
`build/logs/m4-armor-block-strike-regression.log`.

[Host](../../apps/tes3mp-server/native/inventory_host.hpp): one traveler,
128 doors, two 60 Hz substeps; background actors freeze. Player contact
uses a proxy sphere. Bolt visuals and knockout animation timing remain.
Next: OpenMW knockdown roll/get-up in the composed hit; then remaining
weapon resistance/critical rules, scripted items and other cast sources.
Player movement follows collision and smoothness checks.
