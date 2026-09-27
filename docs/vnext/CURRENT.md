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
`build/m4-physical-argonian-live-03/result.json`. Synthetic vanilla bodies/stats/placements;
two clients, 10% loss/100 ms delay/jitter, reconnect/restart.

Inherited player physical capture: `build/m4-player-physical-live-08/result.json`.
Remote creation/equipment and local POV/skeleton rebuilds retain combat poses.
Inherited four-layer check: `build/logs/m4-player-physical-pose-test.log`.

Tick-373 reconnect fixed: native campaigns exclude retained pose velocity from
legacy integration across disconnect/rejoin/restart. Inherited checks:
`build/logs/m4-reconnect-before.log`, `m4-reconnect-pose.log`.

NPC wind-up lock fixed: out-of-reach targets cancel unreleased swings at full
wind-up; normal selection resumes. Released swings retain targets and recheck
reach at the hit key. Inherited transaction/failure evidence:
`build/logs/m4-windup-test.log`, `m4-windup-reconnect.log`, `m4-windup-getup.log`,
`m4-windup-weapons.log`. Server build: `m4-windup-server-build.log`.

New inspected capture: `build/m4-retarget-getup-live-06/result.json` (18 screenshots,
binary hashes). Stock male bodies/iron longsword; synthetic stats/placements.
Both desktops show swings and retargeting during player get-up, 128-tick retreat,
return/reconnect/restart and final convergence without duplicated observed outcomes.
100 ms delay, +/-25 ms jitter, 10% loss; 147 drops. Reconnect frame 35; restore 33.
Bob restarts first; offline Alice pauses. General overlapping melee/ranged/magic remains unproven.

Fixed native-placement melee-event routing and stock weapon sections. Cosmetic chops
restart and release completed poses; replica callbacks are suppressed.
Checks: `build/logs/m4-retarget-action-test.log` (four
stock body layers), `m4-retarget-validator.log`, `m4-retarget-baseline-test.log`;
client build: `m4-retarget-client-build.log`. Harness fixes: fresh movement,
startup-prefix exclusion, shutdown packet drain; failed attempts remain in `build/`.

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
Allowlisting is insufficient: PLAN's applicable integration and live acceptance
remain required. Powers/abilities/diseases remain outside general casting; preserve
stock restrictions. Resolve multi-NPC/summon/player-life prerequisites as needed.
Ranged completion and overlapping combat precede movement/collision cutover.
Plain ranged sources/body proxies and inherited movement remain limitations;
TR Lua awaits M5.
