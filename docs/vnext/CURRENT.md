# Current state and next action

**M4 in [PLAN.md](PLAN.md) is active.** [Host](../../apps/tes3mp-server/native/inventory_host.hpp):
one selected NPC or walking bipedal creature/doors at 60 Hz, commits at 30 Hz;
neighbors freeze. **V52, T3C7/capability 25; fresh campaigns required.**

Custom NPCs share layered melee/body resources, including Argonian swim.
Fixed-windup clips use stock random strength. Hashes changed; fresh campaigns.
Pauses/callback suppression remain.

Eight synthetic-stat/placement/spell desktop runs use authored Morrowind/TR models.
Directories: `build/m4-custom-<body>-<mode>-live-<suffix>`, with `result.json` and
`presentation-validation.json`; both clients' images inspected:

| Body | Record | fatigue suffix | physical suffix |
|---|---|---|---|
| dancer | yakov | 03 | 02 |
| khajiit-f | TR_m1_Carajhi | 01 | 03 |
| khajiit-m | TR_m1_Batharra | 03 | 01 |
| tsaesci | TR_m7_Qorinue-Najan | 01 | 01 |

Verified melee/falls/get-up, reconnect/restart: 100 ms +/-25 ms, 10% loss,
periodic extra 125 ms. 32 traces: render p95 <9.5 ms; body ~1x, melee
0.985-0.998x stock. 64 shared NPC outcomes, 21 health hits; no duplicates.

`build/logs/m4-custom-*`: native retry/restart/retarget/pause, 4x3x3 melee
poses plus hit/falls, creature regression, evidence validators and builds.
Corpse poses untested. Creatures require walking bipeds,
no spells/equipped weapon.

**Next: extend authoritative casting release/recovery and interruption through the
shared timeline; verify resources and both desktops under impairment,
reconnect/restart.**

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
Powers/abilities/diseases remain outside general
casting; preserve stock restrictions. Multi-NPC/summon/player-life prerequisites remain.
Ranged completion and overlapping combat precede movement/collision cutover.
Plain ranged sources/body proxies remain;
TR Lua awaits M5.
