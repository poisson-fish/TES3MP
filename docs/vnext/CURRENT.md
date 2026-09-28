# Current state and next action

**M4 in [PLAN.md](PLAN.md) is active.** [Host](../../apps/tes3mp-server/native/inventory_host.hpp):
NPC/biped/doors: 60 Hz; commits: 30 Hz; neighbors freeze.
**V57, T3D1/capability 29.**

Inherited captures cover dancer, khajiit, and tsaesci TR bodies.

Player/NPC casts retain timing, payment and interruption. Silence/Sound,
shields, stats, cures, Dispel, diseases/curses, Corprus/Vampirism, SunDamage,
rest and Disintegration have synthetic checks; custom casts have vanilla/TR
desktop captures. VFX remains incomplete.

Invisibility/Chameleon use stock hit and awareness; casts/attacks break
Invisibility. T3D1 projects seven visibility effects and Charm. Checks:
`build/logs/{concealment-campaign-09,visibility-campaign-03,visibility-protocol-03}.log`.
Two desktops confirmed visibility and expiry:
`build/m4-visibility-desktop-live-08/result.json`. Detached AI lacks sneak stance.

V56 admits eight movement effects; T3D1 projects player magnitudes. NPC
motion/breath derive from committed state; drowning carries life attribution. Checks:
`build/logs/{movement-npc-service-17,movement-deep-05}.log`.
Dry-room and deep-water desktop captures:
`build/m4-movement-desktop-live-06/result.json`,
`build/m4-movement-deep-desktop-live-03/result.json`. NPC AI has no jump request.

Twelve AI/disposition IDs enter timed spells, WhenUsed, equipped constants and
authored AI-only abilities for the selected NPC/creature and both players.
Server and stock share target checks and Fight/Flee arithmetic. The selected
actor applies distance, disposition, health and Flee to combat choice. Flee
selects a connected pathgrid point or runs blindly for one second; pathgrid
runs persist until completion or distant LOS loss. Calm, Frenzy, Demoralize, Rally,
TurnUndead and level-gated Command affect AI. T3D1 projects committed Charm
into stock dialogue; NPC Charm affects server aggression. Checks:
`build/logs/{ai-rules-final,ai-protocol-test,ai-player-charm-02,ai-passive-creature-02,ai-flee-test-11}.log`.
Committed player disease adds stock's disposition term; offline passive AI
abilities no longer expire. A two-desktop capture shows stock dialogue
45→80→45 with Charm 35 and expiry:
`build/m4-charm-activation-live-01/result.json` and three PNGs.
Ordinary activation now focuses the replicated NPC and executes stock talk.
Replica corpse inventory and scripts remain outside this path. Remote equipment
accepts constants.
Aggression still lacks canonical faction, crime/bounty, drawn-weapon and
werewolf modifiers. Flee lacks stock attack-distance trigger and exact
actor-bound distance checks.
Mixed passive spells, non-player Command casters and multiple active combatants
remain open.

**Next: bind canonical player faction, crime/bounty, draw and werewolf state
to aggression, then finish Flee's stock attack-distance and actor-bound gates.**
Player movement cutover follows combat and collision.

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
