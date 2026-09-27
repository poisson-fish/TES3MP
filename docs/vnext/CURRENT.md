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

Player/NPC casts share durable wind-up/release/recovery and fractional presentation.
Release revalidates before atomic payment. Incapacitation, source/target loss and
disconnect interrupt independently; inactive/saturated areas pause. Custom casting
has resource checks only.

Inherited desktop captures: `build/m4-player-cast-vanilla-live-02`,
`build/m4-player-cast-tr-item-live-01`.

Silence/Sound use OpenMW success/target priorities at release. Silence defeats
Always spells; Sound stacks before fatigue scaling and spares Always spells.
Failed spells pay once; items remain usable. NPC targeting uses authoritative cast activity.

New headless evidence: `build/logs/interference-*`. Player/NPC wind-up arrivals, expiry, concurrent
casts, exact payments, failed writes, reconnect and disk-image restart.
No interference desktop capture.

**Next: elemental shields.**

98 effects remain:

- Attribute/resources (8): DamageAttribute, DamageSkill, RestoreAttribute,
  RestoreSkill, FortifyHealth, FortifyMagicka, FortifyFatigue, FortifyMaximumMagicka.
- Defense/equipment (5): FireShield, LightningShield, FrostShield,
  DisintegrateWeapon, DisintegrateArmor.
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

45 implemented + 98 remaining = 143 IDs. Scripts, powers/abilities/diseases,
multi-NPC/summons/player lives remain unproven. Ranged/overlapping combat precedes
movement cutover; plain ranged sources/body proxies remain. TR Lua awaits M5.
