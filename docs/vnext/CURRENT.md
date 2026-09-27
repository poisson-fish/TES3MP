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

Silence defeats Always; Sound stacks before fatigue and spares Always. Failed
spells pay once; items remain usable. Inherited evidence: `build/logs/interference-*`.

Elemental shields share stock resistance/retaliation. Damage/Restore Attribute/Skill
preserve permanent changes; Fortify Health/Magicka/Fatigue applies once and reverses
on removal. FortifyMaximumMagicka rescales current magicka. Shared family checks:
`build/logs/effect-family-{rules,shields-04,stats-03,fortify-02}.log` cover stacking, expiry, atomic retry, disconnect and
disk restart. Synthetic headless evidence; no new desktop capture.

**Next: conditions/cures through shared source removal and disease lifecycle.**
Follow PLAN's family strategy. General cast/hit/loop VFX integration remains required;
existing casting animations do not establish effect-specific visuals.

87 effects remain:

- Defense/equipment (2): DisintegrateWeapon, DisintegrateArmor.
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

56 implemented + 87 remaining = 143 IDs; bounded gameplay coverage, not visual/source completion.
Scripts, powers/abilities/diseases,
multi-NPC/summons/player lives remain unproven. Ranged/overlapping combat precedes
movement cutover; plain ranged sources/body proxies remain. TR Lua awaits M5.
