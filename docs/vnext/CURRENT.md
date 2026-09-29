# Current state and next action

**M4 in [PLAN.md](PLAN.md) is active.** [Host](../../apps/tes3mp-server/native/inventory_host.hpp):
NPC/biped/doors: 60 Hz; commits: 30 Hz; three placement-keyed
neighbors step once in one collision scene. Additional neighbors freeze. **V63.**

Player/NPC casts, conditions, stats, Disintegration, concealment and visibility
have vanilla/TR checks. VFX and sneak stance remain incomplete.
`build/logs/{concealment-campaign-09,visibility-campaign-03}.log`.

V56 effects drive motion/breath and drowning; AI lacks jump requests:
`build/logs/{movement-npc-service-17,movement-deep-05}.log`.

Twelve AI effects reach timed, item and passive sources; stock Fight/Flee and
Charm consume them: `build/logs/ai-rules-final.log`. V58-V62 compose social
context, crime, NPC placement stats, two-neighbor melee/effects/lives and
respawn in one transaction. Two-player attacks, rejection and restart pass the
synthetic real-loadout campaign: `build/logs/neighbor-combat-test-verify2.log`.
V63 routes player Target spells, Touch effects and physical projectiles to any
bound placement with its own life generation. Collision reports the first bound
body; area effects include neighboring NPCs. Rejected launch, impact, death
attribution and restart pass a synthetic real-loadout campaign for two and three
neighbors: `build/logs/{neighbor-projectiles-test-08,neighbor-expanded-test-02}.log`.
With three neighbors the largest scene image was 1,152 bytes and campaign
image 32,138 bytes. V62 melee and older selected-NPC spell/bow checks:
`build/logs/{neighbor-combat-compat-62,npc-target-spell-compat-02,bow-flight-compat-03}.log`.
The three-neighbor bound remains provisional. Faction join/rank/expulsion
commits for an explicit player: `build/logs/ai-faction-script-test-07.log`.
Flee gates run on LOS and reach: `build/logs/{ai-flee-test-19,ai-action-test-05}.log`.
Mixed passives, non-player Command and scripts remain open.

**Next: validate player projectile contacts against server-owned hulls, then
execute NPC bows, crossbows and thrown weapons with durable ammunition recovery.**

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
