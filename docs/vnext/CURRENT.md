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

Player/NPC casts retain timing, payment, interruption and inactivity pauses;
custom casting checks resources only. Captures: `build/m4-player-cast-vanilla-live-02`,
`build/m4-player-cast-tr-item-live-01`.

Silence defeats Always; Sound stacks and spares Always. Failed spells pay once.
Evidence: `build/logs/interference-*`.

Elemental shields, permanent stat damage/restoration and reversible resource
fortification: synthetic evidence in
`build/logs/effect-family-{rules,shields-04,stats-03,fortify-02}.log`.

Cures and Dispel preserve source membership. V54 persists diseases, curses,
contact and resistance/weakness. Synthetic atomic/restart evidence:
`build/logs/persistent-conditions-20260927-06.log`.

V55 persists Corprus worsening, cures, contact and Vampirism. SunDamage uses
native hour, weather and exposure. Synthetic atomic/restart/sun checks:
`build/logs/{special-conditions-09,environment-sun-03,special-admission-rules-02}.log`;
V54 regression: `build/logs/persistent-regression-02.log`. No desktop capture.

Native wait/rest stages actor recovery with the world skip. Wait restores
fatigue; sleep also restores health and magicka using shared OpenMW rules.
StuntedMagicka suppresses sleep magicka recovery through its active duration;
its deadline advances with the skip. Synthetic rejection/restart:
`build/logs/rest-recovery-08.log`; composition contract:
`build/logs/rest-contract.log`. No desktop capture.
Other timed effect deadlines still follow simulation ticks across skips.
Passive sources and VFX remain pending.

**Next: DisintegrateWeapon/DisintegrateArmor through the native equipment candidate.**

71 effects remain:

- Defense/equipment (2): DisintegrateWeapon, DisintegrateArmor.
- Concealment/detection (7): Invisibility, Chameleon, Light, NightEye,
  DetectAnimal, DetectEnchantment, DetectKey.
- Movement (8): WaterBreathing, SwiftSwim, WaterWalking, Burden, Feather,
  Jump, Levitate, SlowFall.
- AI/disposition (12): Charm, CalmHumanoid, CalmCreature, FrenzyHumanoid,
  FrenzyCreature, DemoralizeHumanoid, DemoralizeCreature, RallyHumanoid,
  RallyCreature, CommandHumanoid, CommandCreature, TurnUndead.
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

72 implemented + 71 remaining = 143 IDs; bounded gameplay, incomplete visuals/sources.
Scripts, passive sources, multi-NPC/summons/player lives remain unproven.
Ranged/overlapping combat precedes movement cutover; plain ranged sources/body
proxies remain. TR Lua awaits M5.
