# Current state and next action

**M4 in [PLAN.md](PLAN.md) is active.** [Host](../../apps/tes3mp-server/native/inventory_host.hpp):
NPC/biped/doors step at 60 Hz and commit at 30 Hz; three neighbors share one
collision scene. Additional neighbors freeze. **V63.**

Player/NPC casts, conditions, stats, Disintegration, concealment and visibility
have vanilla/TR checks; VFX and sneak stance remain incomplete.

V56 effects drive motion/breath and drowning; AI lacks jump requests.

Twelve AI effects reach timed, item and passive sources; stock Fight/Flee and
Charm consume them. V58-V62 compose social context, crime, NPC placement
stats, two-neighbor melee/effects/lives and respawn in one transaction.
V63 routes player spells and physical projectiles by placement/life. Area
effects include neighbors; rejected launch, impact, death and restart pass
synthetic real-loadout campaigns for two and three neighbors.
Player physical impacts now use the first server-owned NPC hull crossed, even
when it differs from the requested aim target; world contact ends flight without
damage. Direct hull/corridor and obstructed-shot outcomes pass in two/three-NPC
campaigns: `build/logs/{projectile-hulls-test-04,projectile-hulls-expanded-02}.log`.
The three-neighbor bound remains provisional. Faction changes commit for an
explicit player; Flee gates run on LOS and reach.
Mixed passives, non-player Command and scripts remain open.

**Next: execute NPC bows, crossbows and thrown weapons with durable ammunition
recovery.** NPC selection can rate ranged weapons, but its attack path still
releases only melee. Physical flight receipts currently bind player casters and
NPC targets; successful hits do not yet add recoverable ammunition to NPC loot.

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
