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

V38 persists caster kind/identity/life in player projectiles, timed/constant
instances and NPC deaths. Player life is 1 across reconnect (player respawn is
not implemented); NPC effects on other actors retain their launch life after
respawn. Recovery rejects invalid kinds, identities, future lives and launch/life
mismatches before installation. Fresh campaign required; older layouts remain.
Lethal native magic now passes an explicit death-time context into the shared
OpenMW stat helper; the composed image owns the death tick.
`tes3mp_native_loadout_tests npc-caster-identity` proves player projectile contact,
session replacement/restart, NPC strike/death/respawn, old-life retention and
rejected durability/recovery (`build/logs/m4-caster-identity-test-08.log`).
Synthetic actors/records, real KF; no new live-client proof. Legacy strike/restart:
`build/logs/m4-caster-identity-legacy.log`.

Detached AI selection/preparation shares OpenMW ratings, resistance, restoration,
range settings, eligibility and rechargeable-item preference with stock callers.
Bounded sources preserve ordered effects, identity and proposed charge without
mutation or RNG consumption. `tes3mp_native_ai_magic_tests` filters `selection`,
`rejection`, `items`, `launch` pass individually
(`build/logs/m4-ai-magic-{selection,rejection,items,launch}.log`).
The `records` filter selects 284 spells/100 WhenUsed enchantments, including
106/30 TR sources and 85 mixed records (`build/logs/m4-ai-magic-records.log`).
Real records use synthetic caster/equipment/activity; live TR outcomes remain
unproved. Automatic selection/launch remains pending.

Next: actor-cast projection and NPC projectile execution (V38 rejects NPC flights),
then authoritative AI scheduling with competition, timing, interruption and target
revalidation. Preserve concurrent player casts. Follow PLAN.md for remaining
effects, retirement, real-mod durability/network proof and melee.
