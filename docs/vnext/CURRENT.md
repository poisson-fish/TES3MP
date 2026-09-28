# Current state and next action

**M4 in [PLAN.md](PLAN.md) is active.** [Host](../../apps/tes3mp-server/native/inventory_host.hpp):
NPC/biped/doors: 60 Hz; commits: 30 Hz; neighbors freeze.
**V55, T3C9/capability 27; fresh campaigns.**

Inherited body captures:
`build/m4-custom-<body>-<mode>-live-<suffix>`:

| Body | Record | fatigue suffix | physical suffix |
|---|---|---|---|
| dancer | yakov | 03 | 02 |
| khajiit-f | TR_m1_Carajhi | 01 | 03 |
| khajiit-m | TR_m1_Batharra | 03 | 01 |
| tsaesci | TR_m7_Qorinue-Najan | 01 | 01 |

Fixtures: `build/logs/m4-custom-*`. Corpse poses and armed/casting creatures untested.

Player/NPC casts retain timing, payment and interruption; custom casts check
resources only. Captures: `build/m4-player-cast-vanilla-live-02`,
`build/m4-player-cast-tr-item-live-01`.

Silence defeats Always; Sound stacks and spares Always. Failed spells pay once.
Evidence: `build/logs/interference-*`.

Elemental shields and stat/resource effects: synthetic evidence:
`build/logs/effect-family-{rules,shields-04,stats-03,fortify-02}.log`.

Cures/Dispel preserve sources. V54 persists diseases, curses, contact and
resistance/weakness. Atomic/restart evidence:
`build/logs/persistent-conditions-20260927-06.log`.

V55 persists Corprus/Vampirism/SunDamage with native time/weather/exposure. Checks:
`build/logs/{special-conditions-09,environment-sun-03,special-admission-rules-02}.log`;
V54 regression: `build/logs/persistent-regression-02.log`. No desktop capture.

Native wait restores fatigue; rest restores health/magicka. StuntedMagicka
blocks magicka recovery until its skip-advanced deadline. Rejection/restart:
`build/logs/rest-recovery-08.log`; composition contract:
`build/logs/rest-contract.log`. No desktop capture.
Other effects retain deadlines across skips. VFX pending.

DisintegrateWeapon/Armor use OpenMW's fractional condition rule and stock armor
priority; breakage unequips. Synthetic player/NPC instant/timed, rejection and
restart: `build/logs/disintegration-13.log`. Constant ticking and desktop unproven.

Invisibility/Chameleon use stock hit and shared awareness. Casts/attack release
break temporary Invisibility and suppress an equipped constant source until
replacement; rejected actions and restart preserve that state. Light, NightEye,
DetectAnimal, DetectEnchantment and DetectKey enter timed and constant sources.
T3C9 projects seven aggregate visibility magnitudes to client actor effects;
OpenMW animation lighting/transparency, player night vision and reference
detection consume them. Source, stacking, expiry, rejection, restart and snapshot:
`build/logs/{constant-concealment-01,concealment-campaign-09,visibility-campaign-03,visibility-protocol-03}.log`.
Visuals/Detect markers need desktop capture; detached AI lacks sneak stance.

**Next: capture visibility on desktops, then bind movement's engine path.**

62 effects remain:
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

81 implemented + 62 remaining = 143 IDs; bounded gameplay, incomplete visuals/sources.
Scripts, passive sources, multi-NPC/summons/player lives remain unproven.
Ranged/overlapping combat precedes movement cutover; plain ranged sources/body
proxies remain. TR Lua awaits M5.
