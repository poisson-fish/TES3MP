# Current state and next action

**M4 in [PLAN.md](PLAN.md) is active.** The [host](../../apps/tes3mp-server/native/inventory_host.hpp)
simulates one traveler and doors at 60 Hz; neighbors freeze. Player contact uses
a proxy sphere.

V34 owns melee defense/recovery, knockout and RNG. Player hit resources remain
unbound (`build/logs/m4-melee-defense-recovery-gate-final.log`).
V35 owns spell/enchantment payment, eight projectiles, areas and attributed timed
effects; inactive expiry pauses. Loss/reconnect:
`build/m4-actor-effects-live-03/result.json`; healing restart:
`build/m4-actor-effects-restart-live-02/result.json`.

V37 constants support Fortify Attribute/Skill and Resist Magicka/Normal Weapons/
Fire/Frost/Shock/Poison across slots, eight effects/item and 512 instances.
Persisted sources/rolls and atomic replacement/respawn preserve base stats.
Other constants reject; all-slot presentation remains unproved.
`npc-general-constants`: `build/logs/m4-general-constants-test-final.log`.

V38 persists caster identity/life. Players remain life 1; effects survive caster
respawn. V37/V38 require fresh campaigns; older recovery remains.
`npc-caster-identity` covers restart, death/respawn and rejected persistence:
`build/logs/m4-caster-identity-test-08.log`. Synthetic records, real KF;
legacy strike/restart: `build/logs/m4-caster-identity-legacy.log`.

Detached AI preparation shares OpenMW ratings, resistance, restoration, range,
eligibility and rechargeable-item preference. Bounded sources preserve ordered
effects, identity and proposed charge without mutation or RNG consumption. `tes3mp_native_ai_magic_tests` filters `selection`,
`rejection`, `items`, `launch` pass individually
(`build/logs/m4-ai-magic-{selection,rejection,items,launch}.log`).
The `records` filter selects 284 spells/100 WhenUsed enchantments, including
106/30 TR sources and 85 mixed records (`build/logs/m4-ai-magic-records.log`).
Real records use synthetic contexts; live TR outcomes and automatic scheduling remain unproved.

V38 now executes concurrent trusted NPC/player casts through shared launch,
contact and effects. NPC projectiles exclude their caster hull, retain typed
attribution, cancel on caster death and validate source/life/launch on recovery.
T3C2 events carry caster kind/life; combat servers require capability 20.
`tes3mp_native_loadout_tests npc-actor-casts` proves two observer projections,
concurrent contact, timed damage/expiry, item charge, eight-flight capacity,
session replacement, restart, death/respawn and rejected persistence/recovery
(`build/logs/m4-actor-casts-final.log`). Synthetic actors/records with real KF;
no new live-client or automatic AI casting proof. Equipped WhenUsed sources still
face the existing equipment service restriction; this check uses a carried item.
Wire/handshake checks: `build/logs/m4-actor-cast-{protocol,handshake}.log`.

Next: AI scheduling with competition, timing, interruption and target revalidation,
feeding actor-cast input while preserving concurrent player casts. Remaining work:
[PLAN.md](PLAN.md).
