# Current state and next action

**M4 in [PLAN.md](PLAN.md) is active.** The [native host](../../apps/tes3mp-server/native/inventory_host.hpp)
simulates one traveler, doors and frozen actors with two 60 Hz steps.
Player contact uses a proxy sphere.

V34 composes armor/block/wear, hit recovery, knockout, RNG and death.
Recovery uses animation keys and pauses offline; player hit resources remain
unbound. Both attack directions share OpenMW normal-weapon resistance and
knockout damage. Evidence: `build/logs/m4-melee-defense-scheduling-final.log`,
`build/logs/m4-melee-defense-recovery-gate-final.log`,
`build/logs/m4-armor-block-test-final.log`, `build/logs/m4-general-knockout-final.log`.

V35 composes spell/enchantment cost/charge, eight projectiles, areas and actor
effects: source/caster, rolled magnitude, resistance, duration, expiry and RNG.
Timed damage/restoration and statuses feed shared combat; inactive expiry pauses
and NPC effect deaths retain attribution. Spells, `WhenUsed` and `WhenStrikes`
install instances. Older image paths remain.

Synthetic ordered effects, charge, rollback, source rejection and restart pass
(`build/logs/m4-actor-effects-ordered-test-02.log`,
`build/logs/m4-actor-effect-strike-ordered.log`). Vanilla plus synthetic effects
ran two clients under loss and reconnect (`build/m4-actor-effects-live-03/result.json`);
a slow healing effect survived restart (`build/m4-actor-effects-restart-live-02/result.json`).
The TR stack prepares 130 added spells and 98 use/strike enchantments
(`build/logs/m4-actor-effects-mod-records-final.log`); live outcomes remain unproved.

V37 generalizes equipped constants to Fortify Attribute, Fortify Skill and
Resist Magicka/Normal Weapons/Fire/Frost/Shock/Poison across equipment slots.
Up to eight effects per item and 512 actor instances carry arguments, ordinals,
source/caster, rolled magnitudes and indefinite expiry. Unchanged equipped
instances retain rolls, including zero results; replacement and respawn derive
new sources atomically. Detached attribute/skill overlays use shared OpenMW
mutations and are removed before saving combat stats. Other constant behaviors
still reject. V37 requires a fresh campaign; V36 fixed Luck-shirt and V35 actor
effect images retain their original descriptor/recovery paths.

`tes3mp_native_loadout_tests npc-general-constants` proves synthetic mixed
Strength/Intelligence/Luck/Axe/Resist Fire records on shirts and rings, duplicate effects,
zero/variable rolls, rejected replacement and unsupported mixed records,
forged argument/ordinal/source/magnitude rejection, exact restart continuation,
stable RNG, derived resources and Strength-modified damage
(`build/logs/m4-general-constants-test-final.log`). The individual target builds
(`build/logs/m4-general-constants-build-final.log`). V36 regression:
`build/logs/m4-general-constants-legacy.log`. V35 actor effects and stock
constant equipment also pass (`build/logs/m4-general-constants-v35.log`,
`build/logs/m4-general-constants-stock.log`). Live mod outcomes, all-slot presentation and AI casting remain unproved.

Next: unify AI spell/item casts, broaden constants, and prove real mod outcomes
on two clients under loss, reconnect and restart. Then finish knockdown/melee.
