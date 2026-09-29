# Current state and next action

**M4 in [PLAN.md](PLAN.md) is active.** [Host](../../apps/tes3mp-server/native/inventory_host.hpp):
NPC/biped/doors: 60 Hz; commits: 30 Hz; neighbors freeze.
**V58.**

Player/NPC casts retain timing, payment and interruption. Condition/stat/resource/
Disintegration and vanilla/TR checks exist; VFX remains incomplete.

Invisibility/Chameleon use stock hit/awareness; actions break Invisibility.
T3D1 projects visibility and Charm. Checks:
`build/logs/{concealment-campaign-09,visibility-campaign-03,visibility-protocol-03}.log`.
Desktop expiry:
`build/m4-visibility-desktop-live-08/result.json`. Detached AI lacks sneak stance.

V56 admits eight movement effects; T3D1 projects player magnitudes. NPC
motion/breath derive from commits; drowning carries life attribution. Checks:
`build/logs/{movement-npc-service-17,movement-deep-05}.log`.
Captures:
`build/m4-movement-desktop-live-06/result.json`,
`build/m4-movement-deep-desktop-live-03/result.json`. NPC AI has no jump request.

Twelve AI/disposition IDs enter timed, item and passive sources. Stock
Fight/Flee consume distance, disposition and health. Flee paths persist until
completion or distant LOS loss. Calm, Frenzy, Demoralize, Rally, TurnUndead,
Command and Charm affect AI/dialogue. Checks:
`build/logs/{ai-rules-final,ai-protocol-test,ai-player-charm-02,ai-passive-creature-02,ai-flee-test-11}.log`.
Disease affects disposition; offline passives persist. Stock dialogue
45→80→45 with Charm expiry:
`build/m4-charm-activation-live-01/result.json`.
Activation focuses the NPC for stock talk; remote equipment accepts constants.
V58 persists two-player faction, crime, bounty, draw, werewolf and source
selection. Casts, attacks, equipment and trusted social actions compose in
the actor tick; invalid/rejected writes are atomic and restart is checked.
Stock aggression/Flee consume the result. Synthetic social checks:
`build/logs/ai-social-test-04.log`.
An authenticated contact against the selected NPC now produces a reported
assault when that victim has stock Alarm 100 and is not engaged, unconscious,
werewolf or vampire. Stock `iCrimeAttack` bounty and faction expulsion join
the hit transaction. Synthetic two-player contact, rejected-write, restart
and subsequent AI checks: `build/logs/ai-crime-test-11.log`. Other witnesses,
offenses and durable crime engagement remain open. A bounded server runner
executes stock faction join/rank/expulsion scripts for an explicit player;
unsupported opcodes reject. Results join the actor tick. Synthetic two-player,
rejected-write, restart and AI check: `build/logs/ai-faction-script-test-07.log`.
General scripts remain M5 work. Werewolf stat save/apply/restore shares OpenMW
code; server transformation, equipment, effects and witnesses remain open.
Flee gates new runs on LOS and actor-hull reach. Synthetic near/far and
Flee/restart checks:
`build/logs/{ai-flee-test-19,ai-action-test-05}.log`. Stock Flee desktop: 68/128 triggers,
218/128 does not;
`build/stock-flee-comparison/result.json` (temporary logging removed).
Mixed passives, non-player Command and multiple combatants remain open.

**Next: wire server werewolf transformation through shared mechanics, equipment,
temporary effects and witnesses; then extend crime witnesses and engagement.**

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
