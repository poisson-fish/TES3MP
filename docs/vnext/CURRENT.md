# Current state and next action

**M4 in [PLAN.md](PLAN.md) is active.** The
[host](../../apps/tes3mp-server/native/inventory_host.hpp) simulates one native NPC
and doors at 60 Hz, committing at 30 Hz. Neighbors freeze; players use contact
proxies until movement cutover.

V42 owns autonomous casting from selection through durable recovery. It compares
carried/equipped melee, bows/crossbows with compatible ammo, thrown weapons,
known spells and equipped unscripted WhenUsed. Server visibility filters targets.
Bound KF keys drive preparation, wind-up, release and recovery. Source, target,
life, interruption and visibility checks precede payment; inactive areas pause.
Two distinct players can cast in the same transaction as the NPC.

Selection/preparation/wind-up/release/flight/recovery persist with RNG, resources,
effects and identities. Recovery validates sources/targets before installation.
Shared effects support timed Fortify Attribute/Skill arguments, original effect
ordinals, mixed Self/Touch/Target and areas. Base stats do not accumulate modifiers.
T3C3 snapshots require capability 21 and drive native NPC casting animation.
Old campaign layouts remain distinct; V42 requires a fresh descriptor-bound campaign.

Verified narrow evidence:

- `tes3mp_native_ai_magic_tests weapons`: carried weapon classes/ammo, condition,
  range, resistance and ties (`build/logs/m4-ai-selection-weapons.log`).
- `npc-full-selection`: equipped WhenUsed, spell competition and observer outcomes
  (`build/logs/m4-full-selection-test-03.log`).
- `npc-cast-lifecycle`: every stage/flight restart, rejected-write identity,
  uncertain-write closure and recovery,
  two player casts, disconnect/occlusion interruption, inactive restart, malformed
  recovery, typed effects/expiry and mixed areas (`build/logs/m4-cast-lifecycle-final.log`).
- Legacy `npc-actor-casts` recovery: `build/logs/m4-cast-legacy-final.log`.
- Combat wire/handshake: `build/logs/m4-cast-protocol-test-02.log` and
  `build/logs/m4-cast-handshake-test.log`.
- Four two-desktop encounters passed concurrent casting, target switching,
  reconnect and process restart during wind-up, with 10% loss/100 ms delay/jitter:
  `build/m4-cast-vanilla-live-10/result.json` (`Fireball_large`) and
  `build/m4-cast-tr-live-02/result.json` (`T_Ayl_Des_DWelkynd_FIR`), plus equipped
  `cruel flamebolt ring` in `build/m4-cast-vanilla-item-live-03/result.json` and
  `T_De_Ep_Ring_Chill` in `build/m4-cast-tr-item-live-04/result.json` (both wind-up images).
  Gameplay records are unchanged; actor stats/room placements are synthetic.
  TR Lua packages are disabled; native Lua services remain M5 work.

V34 retains melee defense/recovery and knockout; distinct player hit resources
remain unbound. V37 supports multi-slot constant fortification/resistance;
other constant effects reject. V38 retains caster life through respawn and effects.
These are bounded migration domains, not complete M4 combat compatibility.

The autonomous casting block has durable encounter evidence. Remaining M4 work
includes weapon execution, knockout and the other unsupported combat/effect paths;
weapon competition here does not implement ranged weapon firing. Next: finish the
remaining melee/knockout work; player movement remains last.
