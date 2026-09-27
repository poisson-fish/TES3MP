# Current state and next action

**M4 in [PLAN.md](PLAN.md) is active.** [Host](../../apps/tes3mp-server/native/inventory_host.hpp):
one NPC/walking biped and doors at 60 Hz; commits at 30 Hz; neighbors freeze.
**V53, T3C8/capability 26; fresh campaigns.**

Inherited body evidence:
`build/m4-custom-<body>-<mode>-live-<suffix>`:

| Body | Record | fatigue suffix | physical suffix |
|---|---|---|---|
| dancer | yakov | 03 | 02 |
| khajiit-f | TR_m1_Carajhi | 01 | 03 |
| khajiit-m | TR_m1_Batharra | 03 | 01 |
| tsaesci | TR_m7_Qorinue-Najan | 01 | 01 |

Synthetic fixtures; authored models. Logs: `build/logs/m4-custom-*`.
Corpse poses untested; creatures require walking bipeds, no spells/equipped weapon.

Player/NPC casts share durable wind-up/release/recovery and fractional presentation
without callbacks. Bound resources, source/target life and concurrent casts persist. Revalidation
precedes atomic magicka/charge/CastOnce payment. Incapacitation, source/target loss
and disconnect interrupt; inactive/saturated areas pause. Recovery outlives targets.
Custom casting has resource checks only.

New: `build/m4-player-cast-vanilla-live-02`, `build/m4-player-cast-tr-item-live-01`:
`result.json`, `presentation-validation.json`, TR `payment-validation.json`.
Synthetic actors/placements, unchanged Fireball/TR spell/WhenUsed; both desktops
inspected. Eight 60-FPS traces: player casts ~1x stock.
100 ms +/-25 ms, 10% loss, extra 125 ms delays, reconnect/restart during concurrent
wind-up. Unpaid interruption; converged unique payment totals; no duplicate outcomes.
`build/logs/player-cast-*`: focused checks/builds.

**Next: implement Silence/Sound interference through this casting lifecycle.**

100 effects remain:

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

43 implemented + 100 remaining = 143 IDs. Scripts, powers/abilities/diseases,
multi-NPC/summons/player lives remain unproven. Ranged/overlapping combat precedes
movement cutover; plain ranged sources/body proxies remain. TR Lua awaits M5.
