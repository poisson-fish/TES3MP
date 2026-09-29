# Current state and next action

**M4 in [PLAN.md](PLAN.md) is active.** [Host](../../apps/tes3mp-server/native/inventory_host.hpp):
NPC/biped/doors: 60 Hz; commits: 30 Hz; two placement-keyed
neighbors step once in one collision scene. Additional neighbors freeze. **V62.**

Player/NPC casts, conditions, stats, Disintegration, concealment and visibility
have vanilla/TR checks. VFX and sneak stance remain incomplete.
`build/logs/{concealment-campaign-09,visibility-campaign-03}.log`.

V56 effects drive motion/breath and drowning; AI lacks jump requests:
`build/logs/{movement-npc-service-17,movement-deep-05}.log`.

Twelve AI effects reach timed, item and passive sources; stock
Fight/Flee and Charm consume them:
`build/logs/{ai-rules-final,ai-player-charm-02,ai-flee-test-11}.log`;
V58 persists social context. V59 composes werewolf stats/equipment,
crime and Fight engagement. Rejection/restart/AI:
`build/logs/social-lifecycle-regression-23.log`; V58 compatibility:
`build/logs/ai-disposition-compat-21.log`.
V60 moves an engaged witness. V61 binds NPC stats and hit resources to
placements; fatigue, bodies and foreign-key rejection are checked:
`build/logs/placement-effects-11.log`. V62 gives both bound neighbors their own
stock melee clip, combat stats, passive/timed effect owner and caster identity,
death history, respawn baseline and life generation. Player melee can target a
neighbor. Hits, death and respawn join the composed actor/inventory image;
rejected writes leave it unchanged. Two players attacking separate NPCs, two
neighbors attacking, passive effects, death, respawn and restart pass the
synthetic real-loadout campaign: `build/logs/neighbor-combat-test-verify2.log`;
V61 and selected-NPC life compatibility:
`build/logs/{placement-actors-compat-14,npc-life-cycle-compat-v62}.log`.
Player spells and ranged hits still target the selected NPC; the
two-neighbor bound is provisional. Faction join/rank/expulsion
commits for an explicit player: `build/logs/ai-faction-script-test-07.log`.
Flee gates run on LOS and reach: `build/logs/{ai-flee-test-19,ai-action-test-05}.log`.
Mixed passives, non-player Command and scripts remain open.

**Next: route player spells and physical projectiles to any bound NPC placement,
then extend the bounded neighbor set as scene and transaction budgets permit.**

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
