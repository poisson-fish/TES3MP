# Current state and next action

**M4 in [PLAN.md](PLAN.md) is active.** [Host](../../apps/tes3mp-server/native/inventory_host.hpp):
one NPC/doors at 60 Hz, commits at 30 Hz; neighbors freeze. T3C6/capability 24.

V51: Reflect, Spell Absorption, Paralyze/Resist Paralysis, Dispel,
Drain Health/Magicka/Fatigue, Absorb Health/Magicka/Fatigue/Attribute/Skill.
Shared OpenMW mechanics/AI; constant defenses; life-bound absorb;
Dispel preserves items/constants. Fresh campaigns required.
Paralyze blocks actions/movement. Touch attribution/on-strike death deduplication persist.

Inherited fatigue captures: `build/m4-knockout-live-05/result.json`,
`build/m4-npc-knockout-live-03/result.json`. NPC physical captures:
`build/m4-physical-female-live-04/result.json`, `build/m4-physical-khajiit-live-03/result.json`,
`build/m4-physical-argonian-live-03/result.json` (synthetic, impaired, reconnect/restart).

Inherited player capture: `build/m4-player-physical-live-08/result.json`;
rebuild pose retention: `build/logs/m4-player-physical-pose-test.log`.

Native reconnect excludes retained velocity from legacy integration.
Inherited check: `build/logs/m4-reconnect-pose.log`.

NPC wind-up cancels unreachable targets; released swings recheck reach at impact.
Inherited transaction/failure checks:
`build/logs/m4-windup-test.log`, `m4-windup-reconnect.log`, `m4-windup-getup.log`,
`m4-windup-weapons.log`.

Inspected: `build/m4-retarget-getup-live-06/result.json` (18 screenshots, binary hashes).
Stock male/longsword; synthetic stats/placements. Both desktops: get-up/retargeting,
128-tick retreat, reconnect/restart, convergence; 100 ms delay, +/-25 ms jitter,
10% loss. Bob restarts first; offline Alice pauses. General overlapping combat remains unproven.

Fixed native-placement melee-event routing and stock weapon sections. Cosmetic chops
restart and release completed poses; replica callbacks are suppressed.
Checks: `build/logs/m4-retarget-action-test.log` (four
stock body layers), `m4-retarget-validator.log`, `m4-retarget-baseline-test.log`;
client build: `m4-retarget-client-build.log`.

User reports choppy/slow combat animations versus walking; speed remains unmeasured.
Committed attack/fall poses hold received samples; captures cap rendering at 30 FPS.
PLAN's bounded smoothness slice follows NPC swing replication, before body/effect expansion.

**M4 incomplete. Next: replicate the committed NPC swing recipe/clock and
target-facing to both desktops**, replacing the cosmetic event-only chop fallback.
Verify under impairment/retreat/get-up/reconnect/restart before creature/custom-body
coverage and remaining effects.

100 built-in effects remain outside general native casting:

- Attribute/resources (8): DamageAttribute, DamageSkill, RestoreAttribute,
  RestoreSkill, FortifyHealth, FortifyMagicka, FortifyFatigue, FortifyMaximumMagicka.
- Defense/interference (7): FireShield, LightningShield, FrostShield, Silence,
  Sound, DisintegrateWeapon, DisintegrateArmor.
- Concealment/detection (7): Invisibility, Chameleon, Light, NightEye,
  DetectAnimal, DetectEnchantment, DetectKey.
- Movement (8): WaterBreathing, SwiftSwim, WaterWalking, Burden, Feather,
  Jump, Levitate, SlowFall.
- AI/disposition (12): Charm, CalmHumanoid, CalmCreature, FrenzyHumanoid,
  FrenzyCreature, DemoralizeHumanoid, DemoralizeCreature, RallyHumanoid,
  RallyCreature, CommandHumanoid, CommandCreature, TurnUndead.
- Conditions (16): WeaknessToCommonDisease, WeaknessToBlightDisease,
  WeaknessToCorprusDisease, ResistCommonDisease, ResistBlightDisease,
  ResistCorprusDisease, CureCommonDisease, CureBlightDisease, CureCorprusDisease,
  CurePoison, CureParalyzation, RemoveCurse, Corprus, Vampirism, SunDamage,
  StuntedMagicka.
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

43 implemented + 100 remaining = 143 IDs; arbitrary scripted/mod support unproven.
PLAN acceptance remains mandatory. Powers/abilities/diseases remain outside general
casting; preserve stock restrictions. Multi-NPC/summon/player-life prerequisites remain.
Ranged completion and overlapping combat precede movement/collision cutover.
Plain ranged sources/body proxies remain;
TR Lua awaits M5.
