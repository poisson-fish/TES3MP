# Current state and next action

**M4 in [PLAN.md](PLAN.md) is active.** The [host](../../apps/tes3mp-server/native/inventory_host.hpp)
simulates one traveler and doors at 60 Hz; neighbors freeze. Player contact uses
a proxy sphere.

V34 composes armor/block/wear, knockout, RNG, death and offline-paused hit
recovery. Player hit resources remain unbound. Both directions share normal-weapon
resistance and knockout damage (`build/logs/m4-melee-defense-recovery-gate-final.log`).

V35 composes spell/enchantment cost/charge, eight projectiles, areas and timed
effects with source/caster, rolls, resistance, expiry and RNG. Inactive expiry
pauses; NPC effect deaths retain attribution. Spells, WhenUsed and WhenStrikes
install instances. Older images retain recovery.
Vanilla/synthetic loss/reconnect: `build/m4-actor-effects-live-03/result.json`;
healing restart: `build/m4-actor-effects-restart-live-02/result.json`.

V37 constants cover Fortify Attribute/Skill and Resist Magicka/Normal
Weapons/Fire/Frost/Shock/Poison across equipment slots. Eight effects/item,
512 instances; sources/rolls persist. Atomic replacement/respawn and stat overlays
preserve saved base stats.
Other constants reject. Fresh campaign required; V36 Luck-shirt/V35 recovery remains.
`tes3mp_native_loadout_tests npc-general-constants`:
`build/logs/m4-general-constants-test-final.log`. All-slot presentation remains unproved.

Shared caster preparation separates combat/inventory indices. Synthetic
`npc-actor-effect-strike` proves slot 2/owner 3, attribution and atomic restart
(`build/logs/m4-caster-context-test-05.log`); player regression:
`build/logs/m4-caster-player-test-01.log`.

Detached AI selection/preparation shares OpenMW ratings, resistance, restoration,
range settings, eligibility and rechargeable-item preference with stock callers.
Bounded sources preserve ordered effects, identity and proposed charge without
mutation or RNG consumption. `tes3mp_native_ai_magic_tests` filters `selection`,
`rejection`, `items`, `launch` pass individually
(`build/logs/m4-ai-magic-{selection,rejection,items,launch}.log`). Spell launch
shares player cost/failure/RNG behavior and defers Target effects.
Build: `build/logs/m4-ai-magic-build-final.log`.
The `records` filter selects 284 spells/100 WhenUsed enchantments, including
106/30 TR sources and 85 mixed records (`build/logs/m4-ai-magic-records.log`).
Real records use synthetic caster/equipment/activity; live TR outcomes remain
unproved. Selection is not scheduled by the authoritative tick; weapon/potion
competition, timing and automatic AI launch remain pending.

Next: wire prepared AI sources into shared tick/launch after durable caster
kind/life and actor-cast projection; preserve concurrent player use. Then timed
arguments/constants, retirement with legacy recovery, and two-client real-mod
proof under loss, reconnect/restart and failed persistence; then finish melee.
