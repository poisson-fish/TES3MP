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

V38 persists caster identity/life; players remain life 1, effects survive caster
respawn. `npc-caster-identity` passes restart/death/rejection
(`build/logs/m4-caster-identity-test-08.log`) and legacy recovery
(`build/logs/m4-caster-identity-legacy.log`).

`tes3mp_native_ai_magic_tests records`: 284 spells/100 WhenUsed enchantments,
including 106/30 TR sources (`build/logs/m4-ai-magic-records.log`); synthetic contexts.

V38 executes concurrent NPC/player casts through shared launch/contact/effects.
NPC flights exclude their caster hull and cancel on death. Recovery validates
source/life. T3C2 requires capability 20.
`tes3mp_native_loadout_tests npc-actor-casts`: observer projections, concurrent
contact, timed effects, item charge/capacity, session replacement, restart,
death/respawn and rejected persistence/recovery (`build/logs/m4-actor-casts-final.log`).
Equipped WhenUsed remains restricted; this check uses a carried item.
Wire/handshake: `build/logs/m4-actor-cast-{protocol,handshake}.log`.

V39 schedules unarmed known spells every eight committed ticks against the nearest
active living player, with one NPC flight. Launch is immediate; line of sight is
unchecked. Restart/rejection/disconnect coverage:
`npc-auto-spells` (`build/logs/m4-auto-spells-test-03.log`).

V40 compares equipped melee weapons against spells using shared OpenMW ratings:
damage, condition, hit chance, speed, resistance and supported strike effects.
Weapon ties beat spells; detached item ties beat weapons. Unsupported weapon/strike
plans retain the existing combat path; carried/ranged weapon selection and equipped
WhenUsed scheduling remain pending. Fresh descriptor-bound campaign; V38 image layout.
`tes3mp_native_ai_magic_tests weapons` passes
(`build/logs/m4-ai-magic-weapons.log`). `npc-weapon-selection` proves both weapon/spell
winners, armed NPC/player concurrent casting, two observer projections, disconnect,
rejoin, restart and identical rejected-write retries
(`build/logs/m4-weapon-selection-test.log`). Synthetic content/GMSTs, real KF;
no live-client or TR encounter acceptance.

Next: finish carried/ranged weapon competition and equipped WhenUsed, then server
line-of-sight targeting before animation timing/interruption and durable preparation.
Timed effect arguments, encounter proof and player movement follow in [PLAN.md](PLAN.md).
