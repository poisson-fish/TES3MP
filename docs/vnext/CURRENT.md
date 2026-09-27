# Current state and next action

**M4 in [PLAN.md](PLAN.md) is active.** [Host](../../apps/tes3mp-server/native/inventory_host.hpp):
one NPC/doors at 60 Hz, commits at 30 Hz; neighbors freeze. T3C6/capability 24.

V51: Reflect, Spell Absorption, Paralyze/Resist Paralysis, Dispel,
Drain Health/Magicka/Fatigue, Absorb Health/Magicka/Fatigue/Attribute/Skill.
Shared OpenMW mechanics/AI; constant defenses; life-bound absorb;
Dispel preserves items/constants. Fresh campaigns required.
Paralyze blocks actions/movement. Touch attribution/on-strike death deduplication persist.

Inherited focused evidence (`build/logs/`): `m4-expanded-effects-test.log`,
`m4-expanded-ai-test.log`, `m4-expanded-protocol-test.log`, `m4-expanded-handshake-test.log`;
`m4-interrupted-test.log`; `m4-death-history-test.log` (2,049 deaths, 16 MiB budget);
`m4-knockout-presentation-test.log`, `m4-knockout-presentation-getup.log`,
`m4-knockout-presentation-loop.log`; `m4-stat-drains-unit.log`, `m4-stat-drains-test.log`.

Inherited inspected fatigue captures: `build/m4-knockout-live-05/result.json`,
`build/m4-npc-knockout-live-03/result.json`. NPC physical captures:
`build/m4-physical-female-live-04/result.json`, `build/m4-physical-khajiit-live-03/result.json`,
`build/m4-physical-argonian-live-03/result.json`. Synthetic vanilla bodies/stats/placements;
two clients, 10% loss/100 ms delay/jitter, reconnect/restart. Native NPC body
replacements immediately sample retained combat.

Inherited player physical capture: `build/m4-player-physical-live-08/result.json`.
Stock male player/NPC sword hits, synthetic stats/placement; self/remote get-up
inspected under impairment/reconnect/restart. Remote creation/equipment and local POV/skeleton
rebuilds retain combat poses; camera setup waits for server positions.
Inherited checks: `build/logs/m4-player-physical-validator.log`, `build/logs/m4-physical-validator.log`,
`build/logs/m4-player-physical-pose-test.log` (four layers), `build/logs/m4-player-physical-client-build.log`.

Tick-373 reconnect fixed: native campaigns exclude retained pose velocity from
legacy integration across disconnect/rejoin/restart. Inherited reproduction:
`build/logs/m4-reconnect-before.log` (`CandidateStateInvalid`, preparation=5).
`m4-reconnect-pose.log` covers four presentation layers.

NPC wind-up lock fixed: out-of-reach targets cancel unreleased swings at full
wind-up; normal selection resumes. Released swings retain targets and recheck
reach at the hit key. No save/protocol change.
`build/logs/m4-windup-before.log` reproduces the 128-tick lock;
`m4-windup-test.log` passes nearby-player reselection, both players retreating,
return/reconnect, inactive pause, released misses, atomic rejection, exact retry/restart
and two-observer events.
`m4-windup-reconnect.log`, `m4-windup-getup.log` and `m4-windup-weapons.log`
preserve tick-373 recovery, player fatigue/physical get-up, weapon replacement
and uncertain-write failure. Server build: `m4-windup-server-build.log`.
New evidence is synthetic; rendered acceptance remains inherited.

**M4 incomplete. Next: capture stock male NPC combat on both desktops**, exercising
retreat/retargeting during player get-up under loss/delay/jitter and reconnect/restart.
Then finish creature/custom-body coverage and the eight attribute/resource effects below.
Inherited player capture moves both players away during recovery; overlapping combat remains unproven.

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
Allowlisting is insufficient: PLAN's applicable integration and live acceptance
remain required. Powers/abilities/diseases remain outside general casting; preserve
stock restrictions. Resolve multi-NPC/summon/player-life prerequisites as needed.
Ranged completion and overlapping combat precede movement/collision cutover.
Plain ranged sources/body proxies and inherited movement remain limitations;
TR Lua awaits M5.
