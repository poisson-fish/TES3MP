# Current state and next action

**M4 in [PLAN.md](PLAN.md) is active.** [Host](../../apps/tes3mp-server/native/inventory_host.hpp):
NPC/biped/doors: 60 Hz; commits: 30 Hz; first engaged neighbor moves;
other neighbors freeze. **V61.**

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
V60 moves the first witness in the shared collision world; pursuit follows
engagement. Collision, rejection and restart:
`build/logs/shared-scene-neighbor-11.log`. V61 binds NPC stats/hit resources to
placement keys. Neighbor fatigue/recovery advances durably; both NPC stat/body
views project. Fresh V61 campaigns required. Foreign-key recovery rejects;
two-player rejection/restart:
`build/logs/placement-effects-11.log`; V60 compatibility:
`build/logs/neighbor-compat-24.log`. Neighbor attacks, effects and life are
pending; other witnesses freeze. Faction join/rank/expulsion
commits for an explicit player: `build/logs/ai-faction-script-test-07.log`.
Flee gates runs on LOS and reach: `build/logs/{ai-flee-test-19,ai-action-test-05}.log`;
desktop comparison: `build/stock-flee-comparison/result.json`.
Mixed passives, non-player Command and general scripts remain open.

**Next: wire the placement-keyed neighbor to stock AI attacks, effects and
life transitions in the composed actor tick; extend beyond the first neighbor.**

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
