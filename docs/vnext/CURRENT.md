# Current state and next action

**M4 in [PLAN.md](PLAN.md) is active.** The [native host](../../apps/tes3mp-server/native/inventory_host.hpp)
simulates one traveler with two 60 Hz steps, doors and frozen background
actors. Player contact uses a proxy sphere. Movement cutover awaits shared
collision and smoothness evidence.

V34 composes armor, shield eligibility/facing, block, wear, hit recovery,
knockout, equipment, RNG and death for player/NPC hits. Recovery follows
bound animation keys and pauses offline. V34 needs a fresh campaign. The
NPC hit resource times all defenders; player resources need proof. Checks:
`build/logs/m4-melee-defense-scheduling-final.log`,
`build/logs/m4-melee-defense-recovery-gate-final.log`,
`build/logs/m4-armor-block-test-final.log` and
`build/logs/m4-knockout-test-final-03.log`.

The shared OpenMW melee primitives now classify normal weapons with the
bound gameplay setting, calculate resistance/weakness and multiply damage
against knocked targets. Both native attack directions use them. Checks:
`build/logs/m4-normal-resistance-melee.log`,
`build/logs/m4-normal-resistance-armor.log` and
`build/logs/m4-general-knockout-final.log`. A native resisted hit still
needs a durable source of resistance/weakness effects.

The shared spell/enchantment plan rolls bounded variable magnitudes using
durable RNG. Health, Magicka and Fatigue damage flows through Self, Touch
and Target effect resolution with resistance; player casts currently accept
Self or Target, and strikes supply Touch. Magic fatigue damage honors the
bound capping setting. One Target projectile composed all three resource
deltas after restart and produced equal session projections; missing RNG
rejects before mutation (`build/logs/m4-general-engine-stats-target.log`).
Spells/`WhenUsed` pay at launch; eight durable projectiles resolve contact,
areas, timed Resist Magicka and death. Inactive actors pause expiry.
Script-free `WhenStrikes` composes charge and Self/Touch/Target effects;
misses retain charge. Prior checks:
`build/logs/m4-projectile-collection-test-final.log`,
`build/logs/m4-strike-test-final.log`. Self casts were shown on two clients
(`build/m4-instant-spell-live-04/result.json`).

Next: generalize durable actor effects, then exercise resisted melee and
timed elemental damage across sources and targets. Add OpenMW knockdown
roll/get-up, remaining melee rules, scripted items and cast sources.
