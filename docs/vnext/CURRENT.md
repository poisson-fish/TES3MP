# Current state and next action

**M4 in [PLAN.md](PLAN.md) is active.** [Host](../../apps/tes3mp-server/native/inventory_host.hpp):
NPC/biped/doors: 60 Hz; commits: 30 Hz; neighbors freeze.
**V55, T3C8/capability 26; fresh campaigns.**

Inherited body captures:
`build/m4-custom-<body>-<mode>-live-<suffix>`:

| Body | Record | fatigue suffix | physical suffix |
|---|---|---|---|
| dancer | yakov | 03 | 02 |
| khajiit-f | TR_m1_Carajhi | 01 | 03 |
| khajiit-m | TR_m1_Batharra | 03 | 01 |
| tsaesci | TR_m7_Qorinue-Najan | 01 | 01 |

Synthetic model fixtures: `build/logs/m4-custom-*`. Corpse poses and armed/spellcasting
creatures remain untested.

Player/NPC casts retain timing, payment, interruption and inactivity pauses.
Custom casting checks resources only. Inherited captures: `build/m4-player-cast-vanilla-live-02`,
`build/m4-player-cast-tr-item-live-01`.

Silence defeats Always; Sound stacks before fatigue and spares Always. Failed
spells pay once; items remain usable. Inherited: `build/logs/interference-*`.

Elemental shields, permanent Damage/Restore Attribute/Skill and reversible resource
fortification: inherited synthetic lifecycle evidence in
`build/logs/effect-family-{rules,shields-04,stats-03,fortify-02}.log`.

CurePoison/CureParalyzation preserve source membership. V54 persists common/blight
diseases and curses, contact, resistance/weakness and whole-source cures. Dispel
preserves sources; selective cure blocks reacquisition. Synthetic atomic/restart
evidence: `build/logs/persistent-conditions-20260927-06.log`.

V55 persists Corprus's daily worsening and rolled effects; CureCorprusDisease
reverses harmful worsening but retains membership. Corprus contact uses its own
resistance/weakness; Vampirism persists. SunDamage uses native hour, regional
weather and exterior/quasi-exterior exposure. Synthetic atomic/restart/sun checks:
`build/logs/{special-conditions-09,environment-sun-03,special-admission-rules-02}.log`;
V54 regression: `build/logs/persistent-regression-02.log`. Server build:
`build/logs/special-final-build.log`. No desktop capture.

**Next: native rest recovery, then StuntedMagicka.** Rest currently owns legacy
combat state, excluded by native campaigns. Passive sources and VFX remain pending.

72 effects remain:

- Defense/equipment (2): DisintegrateWeapon, DisintegrateArmor.
- Concealment/detection (7): Invisibility, Chameleon, Light, NightEye,
  DetectAnimal, DetectEnchantment, DetectKey.
- Movement (8): WaterBreathing, SwiftSwim, WaterWalking, Burden, Feather,
  Jump, Levitate, SlowFall.
- AI/disposition (12): Charm, CalmHumanoid, CalmCreature, FrenzyHumanoid,
  FrenzyCreature, DemoralizeHumanoid, DemoralizeCreature, RallyHumanoid,
  RallyCreature, CommandHumanoid, CommandCreature, TurnUndead.
- Conditions (1): StuntedMagicka.
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

71 implemented + 72 remaining = 143 IDs; bounded gameplay, incomplete visuals/sources.
Scripts, passive sources, multi-NPC/summons/player lives remain unproven.
Ranged/overlapping combat precedes movement cutover; plain ranged sources/body
proxies remain. TR Lua awaits M5.
