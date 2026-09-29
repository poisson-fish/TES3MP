# Current state and next action

**M4 in [PLAN.md](PLAN.md) is active.** [Host](../../apps/tes3mp-server/native/inventory_host.hpp):
NPC/biped/doors: 60 Hz; commits: 30 Hz; first engaged neighbor moves;
other neighbors freeze. **V60.**

Player/NPC casts, conditions, stats and Disintegration have vanilla/TR checks;
VFX remains incomplete. Concealment uses stock awareness; actions break
Invisibility. T3D1 projects visibility and Charm. Checks:
`build/logs/{concealment-campaign-09,visibility-campaign-03}.log`;
desktop expiry: `build/m4-visibility-desktop-live-08/result.json`.
Detached AI lacks sneak stance.

V56 movement effects drive committed NPC motion/breath and drowning attribution;
NPC AI has no jump request. Checks:
`build/logs/{movement-npc-service-17,movement-deep-05}.log`.

Twelve AI/disposition effects reach timed, item and passive sources. Stock
Fight/Flee and Charm dialogue consume them; Flee paths persist. Checks:
`build/logs/{ai-rules-final,ai-player-charm-02,ai-flee-test-11}.log`;
desktop Charm: `build/m4-charm-activation-live-01/result.json`.
V58 persists two-player social context with atomic trusted tick updates:
`build/logs/ai-social-test-04.log`.
V59 commits stock werewolf stats/equipment, temporary-effect purge, witnessed
identity/bounty and durable per-player Fight engagement together. Its selected
NPC consumes engagement in AI. Synthetic rejection/restart/AI:
`build/logs/social-lifecycle-regression-23.log`; V58 compatibility:
`build/logs/ai-disposition-compat-21.log`.
V60 binds the first neighboring NPC witness from plugin placements to an owned
stock physics/navigation scene. Engagement pursues its player; body, motion and
path persist with the actor transaction. Synthetic two-player rejection/restart/
motion: `build/logs/neighbor-ai-test-09.log`. Its separate scene sees committed
counterpart positions; simultaneous collision, attacks, damage, lives and
effects still need bounded multi-actor composition. Other witnesses remain
placement-bound. Other offenses remain open. The bounded faction script runner
commits join/rank/expulsion for an explicit player; unsupported opcodes reject:
`build/logs/ai-faction-script-test-07.log`. General scripts remain M5 work.
Flee gates new runs on LOS and reach:
`build/logs/{ai-flee-test-19,ai-action-test-05}.log`;
desktop comparison: `build/stock-flee-comparison/result.json`.
Mixed passives, non-player Command and multiple combatants remain open.

**Next: generalize the composed actor state, shared collision scene, stock AI
attacks and durable combat/effect/life timelines to bounded neighboring NPCs.**

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
Scripts, multi-NPC combat/summons/player lives remain unproven. Ranged/body work
precedes movement cutover. TR Lua awaits M5.
