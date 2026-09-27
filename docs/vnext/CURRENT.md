# Current state and next action

**M4 in [PLAN.md](PLAN.md) is active.** [Host](../../apps/tes3mp-server/native/inventory_host.hpp):
one NPC/doors at 60 Hz, commits at 30 Hz; neighbors freeze. T3C6 requires capability 24.

V51 implements a subset of step 4: Reflect, Spell Absorption, Paralyze,
Resist Paralysis, Dispel, Drain Health/Magicka/Fatigue and Absorb Health/Magicka/
Fatigue/Attribute/Skill. Shared OpenMW helpers own mechanics/AI. Constant defenses
work; Dispel preserves items/constants. Absorb benefits bind caster life.
V51 requires fresh campaigns and binds reflected-absorb settings. Paralyze blocks
actions/movement; snapshots carry paralysis/negative magicka. Touch attribution
and on-strike death deduplication persist.

Inherited V51 evidence: synthetic vanilla spell/items, retry, expiry, restart and
NPC lifecycle (`build/logs/m4-expanded-effects-test.log`); shared helpers/AI
(`build/logs/m4-expanded-ai-test.log`); protocol/handshake
(`build/logs/m4-expanded-protocol-test.log`, `build/logs/m4-expanded-handshake-test.log`).

Other inherited checks:
- Interrupted-cast cancellation: `build/logs/m4-interrupted-test.log`.
- Death history through 2,049, attribution, loot deduplication and respawn:
  `build/logs/m4-death-history-test.log`. The image remains bounded to 16 MiB;
  quest rewards remain M5.
- Knockout/get-up: `build/logs/m4-knockout-presentation-test.log`,
  `build/logs/m4-knockout-presentation-getup.log`, `build/logs/m4-knockout-presentation-loop.log`.
- Drain Attribute/Skill: `build/logs/m4-stat-drains-unit.log`, `build/logs/m4-stat-drains-test.log`.

Inherited fatigue captures: players `build/m4-knockout-live-05/result.json`,
NPC `build/m4-npc-knockout-live-03/result.json`. Synthetic vanilla actors/spells;
both clients' inspected exhaustion/get-up/upright screenshots passed 10% loss/100 ms
delay/jitter, reconnect/restart. Fixture/validators passed.
Humanoid/female/beast/Argonian exhaustion/physical-knockdown sampling passed
(`build/logs/m4-knockout-body-presentation.log`): resource clocks only.

New: NPC physical sword-hit knockdown/get-up passed two-client captures at 10%
loss/100 ms delay/jitter, reconnect and restart: `build/m4-physical-female-live-04/result.json`,
`build/m4-physical-khajiit-live-03/result.json`, `build/m4-physical-argonian-live-03/result.json`.
Inspected both clients' down/get-up/upright screenshots. Synthetic vanilla
placements/stats; stock sword/rolls/clips. Damage retention, advancing clocks and
hit deduplication passed; missed reconnect windows are recorded, not credited.
New native bodies immediately apply retained combat before another packet arrives.
Validator/builds passed (`build/logs/m4-physical-validator.log`,
`build/logs/m4-physical-client-build.log`, `build/logs/m4-physical-fixture-build.log`).
Player physical-hit recovery and male/creature/custom-body live coverage remain.

Inherited desktop evidence, 10% loss/100 ms delay/jitter:
`build/m4-cast-vanilla-live-10/result.json`, `build/m4-cast-tr-live-02/result.json`,
`build/m4-cast-vanilla-item-live-03/result.json`, `build/m4-cast-tr-item-live-04/result.json`,
`build/m4-swing-vanilla-live-07/result.json`.
Synthetic placements; these captures do not verify new effects.

**M4 incomplete. Next: step 4's player physical-hit knockdown and remaining body presentation,
then attribute/resource effects.**
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
