# Current state and next action

**M4 in [PLAN.md](PLAN.md) is active.** [Host](../../apps/tes3mp-server/native/inventory_host.hpp):
one NPC/walking biped and doors at 60 Hz; commits at 30 Hz; neighbors freeze.
**V52, T3C7/capability 25; fresh campaigns.**

Inherited custom-body melee/falls/get-up evidence:
`build/m4-custom-<body>-<mode>-live-<suffix>`:

| Body | Record | fatigue suffix | physical suffix |
|---|---|---|---|
| dancer | yakov | 03 | 02 |
| khajiit-f | TR_m1_Carajhi | 01 | 03 |
| khajiit-m | TR_m1_Batharra | 03 | 01 |
| tsaesci | TR_m7_Qorinue-Najan | 01 | 01 |

Synthetic stats/placements/spells; authored models. Logs: `build/logs/m4-custom-*`.
Corpse poses untested; creatures require walking bipeds, no spells/equipped weapon.

NPC casting now samples frozen release/recovery sections through the shared timeline,
including interruption, pauses, starvation and life/session boundaries; no callbacks.
Save/wire unchanged. All three ranges match vanilla/dancer/custom Khajiit/Tsaesci
resources. Custom casting has resource checks only.

New captures: `build/m4-cast-timeline-vanilla-live-02` and
`build/m4-cast-timeline-tr-item-live-01`: `result.json`, `presentation-validation.json`.
Synthetic actors/placements; unchanged Fireball/TR spell/WhenUsed.
Both desktops inspected; eight traces: p95 <9.1 ms, casting ~1x stock.
Both capture groups use 100 ms +/-25 ms, 10% loss, extra 125 ms delays,
reconnect/restart. New captures prove pre-payment cancellation, retained wind-up,
converged resources and no duplicate outcomes. `build/logs/cast-*`: timeline,
resources, lifecycle/interruption, validators/builds. Player casting remains immediate.

**Next: persist player wind-up/release/recovery and interruption through this timeline,
preserving concurrent casts and atomic payment.**

100 effects remain outside general native casting:

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
