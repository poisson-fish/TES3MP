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

New player physical capture: `build/m4-player-physical-live-08/result.json`.
Stock male player, NPC sword hits; synthetic stats/placement. Both clients'
self/remote down/get-up/upright images inspected under the same impairment,
subject reconnect and restart. Damage retention, clocks and hit deduplication pass.
Remote player creation/equipment replacement samples retained combat; local
POV/skeleton rebuilds retain knockout frames. Camera setup waits for server positions.
Focused checks: `build/logs/m4-player-physical-validator.log`, `build/logs/m4-physical-validator.log`,
`build/logs/m4-player-physical-pose-test.log` (four resource layers),
`build/logs/m4-player-physical-client-build.log`.

**M4 incomplete. Next: diagnose concurrent-hit reconnect failure**, then remaining
male NPC/creature/custom-body coverage and attribute/resource effects.
`build/m4-player-physical-live-03/server.stderr.log` records `CandidateStateInvalid`
(preparation=5, tick 373) while the observer remains under attack during subject
reconnect; unresolved. NPC wind-up also holds an out-of-reach selected target.
Accepted player capture stages the observer in reach for selection, then moves
both players away during recovery. It does not prove overlapping combat.

The following 100 built-in effects remain outside general native casting:

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

Existing 43 plus these 100 cover 143 IDs, not arbitrary scripted/mod support.
Allowlisting is insufficient: PLAN's applicable integration and live acceptance
remain required. Powers/abilities/diseases remain outside general casting; preserve
stock restrictions. Resolve multi-NPC/summon/player-life prerequisites as needed.
Ranged completion and overlapping combat precede movement/collision cutover.
Plain ranged sources/body proxies and inherited movement remain limitations;
TR Lua awaits M5.
