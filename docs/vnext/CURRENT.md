# Current state and next action

**M4 in [PLAN.md](PLAN.md) is active.** [Host](../../apps/tes3mp-server/native/inventory_host.hpp):
NPC/biped/doors: 60 Hz; commits: 30 Hz; neighbors freeze.
**V59.**

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
V59 applies shared OpenMW werewolf stat save/apply/restore, unequips items,
generates/removes the robe, purges temporary effects, and applies witnessed
identity and bounty consequences in one actor transaction. Transformation
blocks equipment and casts. A visible NPC in the bound interior can witness
an authenticated assault beyond its victim; stock Alarm reports the bounty,
and Fight escalation records durable per-player engagement. The selected
NPC consumes that engagement in stock AI. Synthetic two-player transformation,
assault, rejected-write, restart and AI checks:
`build/logs/social-lifecycle-test-22.log`. V58 AI compatibility:
`build/logs/ai-disposition-compat-21.log`. Other witnesses are placement-bound
until multi-NPC simulation; their own live AI is not yet driven. Other offenses
remain open. A bounded server runner
executes stock faction join/rank/expulsion scripts for an explicit player;
unsupported opcodes reject. Results join the actor tick. Synthetic two-player,
rejected-write, restart and AI check: `build/logs/ai-faction-script-test-07.log`.
General scripts remain M5 work.
Flee gates new runs on LOS and reach:
`build/logs/{ai-flee-test-19,ai-action-test-05}.log`;
desktop comparison: `build/stock-flee-comparison/result.json`.
Mixed passives, non-player Command and multiple combatants remain open.

**Next: simulate the first neighboring NPC with durable engagement and live
stock AI, then generalize to bounded neighboring combatants.**

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
Scripts, multi-NPC/summons/player lives remain unproven. Ranged/body work
precedes movement cutover. TR Lua awaits M5.
