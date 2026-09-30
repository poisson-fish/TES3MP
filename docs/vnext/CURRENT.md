# Current state and next action

**M4 in [PLAN.md](PLAN.md) is active.** [Host](../../apps/tes3mp-server/native/inventory_host.hpp):
NPC/biped/doors step at 60 Hz and commit at 30 Hz; three neighbors share one
collision scene. Additional neighbors freeze. V63 bindings remain active.

Player/NPC casts, conditions, stats, Disintegration, concealment and visibility
have vanilla/TR checks; VFX and sneak stance remain incomplete.

V56 effects drive motion/breath and drowning; AI lacks jump requests.

Twelve AI effects reach timed, item and passive sources; stock Fight/Flee and
Charm consume them. V58-V63 compose social context, placement combat, neighbor
lives and player spells/projectiles. Physical shots contact the first server-owned
hull; world contact ends flight. Two/three-neighbor regression checks pass:
`build/logs/{neighbor-projectiles-npc-ranged-test-30,neighbor-expanded-npc-ranged-test-01}.log`.

NPC bows, crossbows and stock throwing stars now use server-owned selection,
authored shoot release, flight and player contact. Flights persist NPC placement
and life; death clears old-life flights and respawn releases with the next life.
Stock-chance plain ammunition recovery joins damage, RNG and NPC inventory in
one durable impact. Rejection, retry, two overlapping impacts, lethal recovery,
malformed life, death and restart pass narrow real Morrowind loadouts:
`build/logs/npc-ranged-{bow,crossbow,thrown}-test-30.log`.
The three-neighbor bound remains provisional. Mixed passives, non-player
Command, scripts and generic enchanted/scripted ranged sources remain open.
An impact sharing a world-transfer tick rejects atomically.

**Next: wire desktop ranged input and projectile presentation to committed
server releases/contacts; verify two-client flight under latency and reconnect.**

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

81 implemented + 62 incomplete = 143 IDs. Broader multi-NPC combat, summons
and player lives remain unproven. Ranged/body work precedes movement cutover.
TR Lua awaits M5.
