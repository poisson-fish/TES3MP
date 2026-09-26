# Current state and next action

**M4 in [PLAN.md](PLAN.md) is active.** The [native host](../../apps/tes3mp-server/native/inventory_host.hpp)
simulates one traveler, doors and frozen actors with two 60 Hz steps.
Player contact uses a proxy sphere.

V34 composes armor/block/wear, hit recovery, knockout, RNG and death.
Animation-key recovery pauses offline; player hit resources remain unbound.
Both directions share normal-weapon resistance and knockout damage
(`build/logs/m4-melee-defense-recovery-gate-final.log`).

V35 composes spell/enchantment cost/charge, eight projectiles, areas and actor
effects: source/caster, rolled magnitude, resistance, duration, expiry and RNG.
Timed damage/restoration and statuses feed shared combat; inactive expiry pauses
and NPC effect deaths retain attribution. Spells, `WhenUsed` and `WhenStrikes`
install instances. Older image paths remain.

Vanilla/synthetic two-client loss/reconnect evidence:
`build/m4-actor-effects-live-03/result.json`; healing restart:
`build/m4-actor-effects-restart-live-02/result.json`.
TR prepares 130 added spells and 98 use/strike enchantments
(`build/logs/m4-actor-effects-mod-records-final.log`); live outcomes remain unproved.

V37 generalizes equipped constants to Fortify Attribute, Fortify Skill and
Resist Magicka/Normal Weapons/Fire/Frost/Shock/Poison across equipment slots.
Bounds: eight effects/item, 512 instances. Arguments, ordinals, sources and rolls
persist; replacement/respawn derives new sources atomically. OpenMW stat overlays
never accumulate in saved base stats. Other constants reject. V37 requires a fresh
campaign; V36 fixed Luck-shirt and V35 effect images retain recovery.

`tes3mp_native_loadout_tests npc-general-constants` proves synthetic mixed
attributes/skills/resistances, duplicate/zero/variable rolls, atomic rejection,
forged-source rejection, exact restart/RNG, derived resources and damage
(`build/logs/m4-general-constants-test-final.log`). Legacy/stock evidence:
`build/logs/m4-general-constants-legacy.log`, `build/logs/m4-general-constants-stock.log`.
All-slot presentation and AI casting remain unproved.

Shared caster context separates combat/inventory indices and identity. Player
items and player/AI strikes share preparation/charge calculation.
Synthetic `npc-actor-effect-strike` proves NPC combat slot 2/inventory owner 3,
attribution, charge, rejection, exact restart and ward-modified melee
(`build/logs/m4-caster-context-test-05.log`). Build: `build/logs/m4-caster-context-build-04.log`.
Player spell/item regression passes `npc-actor-effects`
(`build/logs/m4-caster-player-test-01.log`).

Next: AI spell/WhenUsed selection/launch, durable caster kind/life and projection.
Then timed arguments/constants, durability and retirement with legacy recovery.
Prove real mod outcomes with two clients under loss, reconnect/restart, concurrent
casts and failed persistence; then finish melee.
