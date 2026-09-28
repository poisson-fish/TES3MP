# Current state and next action

**M4 in [PLAN.md](PLAN.md) is active.** [Host](../../apps/tes3mp-server/native/inventory_host.hpp):
NPC/biped/doors: 60 Hz; commits: 30 Hz; neighbors freeze.
**V58, T3D1/capability 29.**

Player/NPC casts retain timing, payment and interruption. Condition, stat,
resource and Disintegration effects have synthetic checks; custom casts have
vanilla/TR desktop captures. VFX remains incomplete.

Invisibility/Chameleon use stock hit and awareness; casts/attacks break
Invisibility. T3D1 projects seven visibility effects and Charm. Checks:
`build/logs/{concealment-campaign-09,visibility-campaign-03,visibility-protocol-03}.log`.
Desktop visibility and expiry:
`build/m4-visibility-desktop-live-08/result.json`. Detached AI lacks sneak stance.

V56 admits eight movement effects; T3D1 projects player magnitudes. NPC
motion/breath derive from committed state; drowning carries life attribution. Checks:
`build/logs/{movement-npc-service-17,movement-deep-05}.log`.
Movement desktop captures:
`build/m4-movement-desktop-live-06/result.json`,
`build/m4-movement-deep-desktop-live-03/result.json`. NPC AI has no jump request.

Twelve AI/disposition IDs enter timed, item and passive sources for both players
and the selected actor. Shared stock Fight/Flee rules use distance, disposition
and health. Flee follows a connected pathgrid or runs blindly for one second;
path runs persist until completion or distant LOS loss. Calm, Frenzy,
Demoralize, Rally, TurnUndead and Command affect AI. T3D1 projects Charm
into stock dialogue; NPC Charm affects server aggression. Checks:
`build/logs/{ai-rules-final,ai-protocol-test,ai-player-charm-02,ai-passive-creature-02,ai-flee-test-11}.log`.
Committed player disease adds stock's disposition term; offline passive AI
abilities no longer expire. Stock dialogue 45→80→45 with Charm 35 and expiry:
`build/m4-charm-activation-live-01/result.json`.
Ordinary activation now focuses the replicated NPC and executes stock talk.
Remote equipment accepts constants.
V58 persists player faction, crime, bounty, draw, werewolf and source selection.
Trusted updates for both players join the actor tick, including a simultaneous
cast. Bad content, duplicate players and rejected writes are atomic; restart
validates content and inventory. Stock aggression and Flee consume these values.
Production gameplay producers remain unconnected. Flee gates a
new run on LOS and target reach using loaded actor hulls.
Synthetic hull-bound near/far and committed Flee/restart checks:
`build/logs/ai-flee-test-19.log`. Stock desktop Flee with LOS: 68/128 triggers,
218/128 does not;
`build/stock-flee-comparison/result.json` (temporary logging removed).
V58 modifier/restart and V57 creature checks:
`build/logs/{ai-player-live-test-08,ai-player-creature-03}.log`; stock OpenMW build:
`build/logs/ai-player-openmw-build-02.log`.
Mixed passive spells, non-player Command casters and multiple active combatants
remain open.

**Next: connect authoritative gameplay producers to the V58 per-tick ingress.**
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
