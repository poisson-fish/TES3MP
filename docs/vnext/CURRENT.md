# Current state and next action

**M4 in [PLAN.md](PLAN.md) is active.** The [native host](../../apps/tes3mp-server/native/inventory_host.hpp)
simulates one traveler, doors and frozen actors with two 60 Hz steps.
Player contact uses a proxy sphere.

V34 composes armor, shield, block, wear, hit recovery, knockout, RNG and death.
Recovery uses bound animation keys and pauses offline. Player hit resources
still need binding; V34 needs a fresh campaign. Checks:
`build/logs/m4-melee-defense-scheduling-final.log`,
`build/logs/m4-melee-defense-recovery-gate-final.log`,
`build/logs/m4-armor-block-test-final.log` and
`build/logs/m4-knockout-test-final-03.log`.

Both attack directions use shared OpenMW normal-weapon resistance and
knockout damage. Checks:
`build/logs/m4-normal-resistance-melee.log`,
`build/logs/m4-normal-resistance-armor.log` and
`build/logs/m4-general-knockout-final.log`.

Spells and enchantments carry Self, Touch and Target effects, costs,
charge, eight projectiles, areas and durable RNG. V35 adds
actor effect instances for players and NPC: effect/source/caster identity,
rolled magnitude, OpenMW resistance, duration and expiry share the
composed image. Timed elemental/resource damage and restoration accrue while
active; status effects feed shared magic and melee calculations. Inactive
actors pause expiry; NPC effect deaths are attributed. V35 needs a fresh
campaign. `WhenStrikes`, spells and `WhenUsed` install instances;
old image paths remain.

Synthetic checks cover ordered effects, resisted weapon hits, timed Fire Damage
and restoration, `WhenUsed` charge, strike source, rollback, source rejection, expiry and restart
(`build/logs/m4-actor-effects-ordered-test-02.log`,
`build/logs/m4-actor-effect-strike-ordered.log`). A vanilla loadout plus synthetic
effect records ran two clients under loss; both saw one timed cast,
healing and Bob's reconnect (`build/m4-actor-effects-live-03/result.json`).
A slow active effect survived server restart and continued healing on two
returning clients (`build/m4-actor-effects-restart-live-02/result.json`).
The TR stack prepares 130 added `T_` spells and 98 `T_` use/strike
enchantments (`build/logs/m4-actor-effects-mod-records-final.log`).
Live mod-record outcomes remain unproved.

Next: wire constant effects and AI casts into V35.
Prove varied real mod records and two-client convergence.
Then finish knockdown and melee rules.
