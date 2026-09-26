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

V38 persists caster identity/life: players remain life 1; effects survive caster
respawn; older recovery remains.
`npc-caster-identity`: restart, death/respawn, rejected persistence
(`build/logs/m4-caster-identity-test-08.log`); legacy recovery:
`build/logs/m4-caster-identity-legacy.log`.

Detached AI preparation shares OpenMW ratings, resistance, restoration, eligibility
and rechargeable-item preference without mutation/RNG. `tes3mp_native_ai_magic_tests`
filters `selection`, `rejection`, `items`, `launch` pass individually
(`build/logs/m4-ai-magic-{selection,rejection,items,launch}.log`). `records` selects
284 spells/100 WhenUsed enchantments, including 106/30 TR sources and 85 mixed
records (`build/logs/m4-ai-magic-records.log`); contexts remain synthetic.

V38 executes concurrent NPC/player casts through shared launch/contact/effects.
NPC flights exclude their caster hull, cancel on death and validate source/life
on recovery. T3C2 carries caster kind/life; admission requires capability 20.
`tes3mp_native_loadout_tests npc-actor-casts`: observer projections, concurrent
contact, timed effects, item charge/capacity, session replacement, restart,
death/respawn and rejected persistence/recovery (`build/logs/m4-actor-casts-final.log`).
Regression: `build/logs/m4-auto-spells-actor-regression.log`. Equipped WhenUsed
remains restricted; the check uses a carried item. Wire/handshake:
`build/logs/m4-actor-cast-{protocol,handshake}.log`. Synthetic records, real KF.

V39 schedules unarmed NPC known spells against the nearest active living player,
without line-of-sight checks; visibility is a required follow-up slice.
OpenMW ratings, source eligibility, racial exclusion, active-source suppression
and one NPC flight feed shared launch. Eight-tick admission uses committed tick;
launch remains immediate. Fresh descriptor-bound campaign; V38 image layout.
`npc-auto-spells` verifies concurrent player/NPC casts, two observer projections,
target switching on disconnect, offline/rejoin, restart before admission and in
flight, and identical rejected-write retries (`build/logs/m4-auto-spells-test-03.log`).
Synthetic actors/records and real KF; no live-client or TR encounter acceptance.

Next: weapon competition and equipped WhenUsed, then animation timing/interruption
and durable preparation.
Timed effect arguments and two-client encounter proof follow in [PLAN.md](PLAN.md).
Player movement remains later in M4.
