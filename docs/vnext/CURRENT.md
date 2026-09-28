# Current state and next action

**M4 in [PLAN.md](PLAN.md) is active.** [Host](../../apps/tes3mp-server/native/inventory_host.hpp):
NPC/biped/doors: 60 Hz; commits: 30 Hz; neighbors freeze.
**V57, T3D1/capability 29.**

Inherited captures cover dancer, khajiit, and tsaesci TR bodies;
corpse poses and armed creatures remain untested.

Player/NPC casts retain timing, payment and interruption. Silence/Sound,
shields, stats, cures, Dispel, diseases/curses, Corprus/Vampirism, SunDamage,
rest and Disintegration have synthetic checks under `build/logs/`; custom casts
have vanilla/TR desktop captures. VFX and desktop effects remain incomplete.

Invisibility/Chameleon use stock hit and awareness; casts/attacks break temporary
Invisibility. T3D1 projects seven visibility effects and Charm. Lifecycle checks:
`build/logs/{concealment-campaign-09,visibility-campaign-03,visibility-protocol-03}.log`.
Two desktops confirmed visibility, detection, expiry and reconnect:
`build/m4-visibility-desktop-live-08/result.json`. Detached AI lacks sneak stance.

V56 admits eight movement effects into spell/constant sources; T3D1 projects
player magnitudes. NPC motion/breath derive from committed state; drowning carries
life attribution. Solver, source and deep-water checks:
`build/logs/{movement-npc-service-17,movement-deep-05}.log`.
Two desktops converged across dry-room and deep-water expiry/restart captures:
`build/m4-movement-desktop-live-06/result.json`,
`build/m4-movement-deep-desktop-live-03/result.json`. NPC AI has no jump request.

The twelve AI/disposition IDs enter bounded timed spell/WhenUsed sources.
V57 also admits AI effects from equipped constants and authored, AI-only Self
abilities for the selected NPC or unarmed biped creature and both players.
Stock and server share target checks and Fight/Flee arithmetic. The selected
NPC or undead creature applies distance and disposition to aggression, health
and Flee to attack choice, and runs away through a durable 30-tick navigation
destination. Calm, Frenzy, Demoralize, Rally and TurnUndead affect those choices;
level-gated Command follows a player caster. T3D1 projects committed Charm into
OpenMW MagicEffects, which stock derived dialogue disposition reads. NPC Charm
also affects the server's aggression disposition. Synthetic rules, protocol,
NPC/creature source, expiry, rejection and restart checks:
`build/logs/{ai-rules-final,ai-protocol-test,ai-player-charm-02,ai-passive-creature-02}.log`.
Desktop build: `build/logs/ai-desktop-build.log`.
No dialogue capture. Aggression disposition omits faction, crime,
disease, drawn-weapon and werewolf modifiers. Flee navigation uses an away
destination rather than stock pathgrid/random blind run. Mixed passive spells,
non-player Command casters and multiple active combatants remain open.

**Next: close aggression/Flee fidelity; validate dialogue live.**
Player movement cutover stays after combat and collision requirements.

62 effects remain to complete across applicable actors and sources:
- Movement (8): WaterBreathing, SwiftSwim, WaterWalking, Burden, Feather,
  Jump, Levitate, SlowFall.
- AI/disposition (12): Charm, CalmHumanoid, CalmCreature, FrenzyHumanoid,
  FrenzyCreature, DemoralizeHumanoid, DemoralizeCreature, RallyHumanoid,
  RallyCreature, CommandHumanoid, CommandCreature, TurnUndead.
- Objects (4): Lock, Open, Telekinesis, Soultrap.
- Travel (4): Mark, Recall, DivineIntervention, AlmsiviIntervention.
- Equipment (12): BoundDagger, BoundLongsword, BoundMace, BoundBattleAxe,
  BoundSpear, BoundLongbow, BoundCuirass, BoundHelm, BoundBoots, BoundShield,
  BoundGloves, ExtraSpell (preserve actual stock behavior).
- Summons (22): SummonScamp, SummonClannfear, SummonDaedroth, SummonDremora,
  SummonAncestralGhost, SummonSkeletalMinion, SummonBonewalker,
  SummonGreaterBonewalker, SummonBonelord, SummonWingedTwilight, SummonHunger,
  SummonGoldenSaint, SummonFlameAtronach, SummonFrostAtronach, SummonStormAtronach,
  SummonCenturionSphere, SummonFabricant, SummonWolf, SummonBear, SummonBonewolf,
  SummonCreature04, SummonCreature05.

81 implemented + 62 incomplete = 143 IDs; visuals/sources remain bounded.
Scripts, mixed passive sources, multi-NPC/summons/player lives remain unproven.
Ranged/overlapping combat precedes movement cutover; plain ranged sources/body
proxies remain. TR Lua awaits M5.
