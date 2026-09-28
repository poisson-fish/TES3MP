# Current state and next action

**M4 in [PLAN.md](PLAN.md) is active.** [Host](../../apps/tes3mp-server/native/inventory_host.hpp):
NPC/biped/doors: 60 Hz; commits: 30 Hz; neighbors freeze.
**V56, T3D0/capability 28; fresh campaigns.**

Inherited body captures:
`build/m4-custom-<body>-<mode>-live-<suffix>`:

| Body | Record | fatigue suffix | physical suffix |
|---|---|---|---|
| dancer | yakov | 03 | 02 |
| khajiit-f | TR_m1_Carajhi | 01 | 03 |
| khajiit-m | TR_m1_Batharra | 03 | 01 |
| tsaesci | TR_m7_Qorinue-Najan | 01 | 01 |

Corpse poses and armed/casting creatures untested.

Player/NPC casts retain timing/payment/interruption; custom casts check resources.
Captures: `build/m4-player-cast-vanilla-live-02`,
`build/m4-player-cast-tr-item-live-01`.

Silence defeats Always; Sound stacks and spares Always. Failed spells pay once.
Evidence: `build/logs/interference-*`.

Elemental shields and stat/resource effects:
`build/logs/effect-family-{rules,shields-04,stats-03,fortify-02}.log`.

Cures/Dispel preserve sources. V54 persists diseases, curses, contact and
resistance/weakness. Atomic/restart evidence:
`build/logs/persistent-conditions-20260927-06.log`.

V55 persists Corprus/Vampirism/SunDamage with native time/weather/exposure. Checks:
`build/logs/{special-conditions-09,environment-sun-03,special-admission-rules-02}.log`;
V54 regression: `build/logs/persistent-regression-02.log`. No desktop capture.

Wait restores fatigue; rest restores health/magicka. StuntedMagicka blocks
magicka recovery until its skip-advanced deadline. Rejection/restart:
`build/logs/rest-recovery-08.log`; composition contract:
`build/logs/rest-contract.log`. No desktop capture.
Other deadlines survive skips; VFX pending.

DisintegrateWeapon/Armor use OpenMW's fractional condition rule and stock armor
priority; breakage unequips. Synthetic player/NPC instant/timed, rejection and
restart: `build/logs/disintegration-13.log`. Constant ticking and desktop unproven.

Invisibility/Chameleon use stock hit and shared awareness. Casts/attack release
break temporary Invisibility and suppress an equipped constant source until
replacement; rejected actions and restart preserve that state. Light, NightEye,
DetectAnimal, DetectEnchantment and DetectKey enter timed and constant sources.
T3D0 projects seven aggregate visibility magnitudes to client actor effects;
OpenMW animation lighting/transparency, player night vision and reference
detection consume them. Stacking, expiry, rejection, restart and snapshot:
`build/logs/{constant-concealment-01,concealment-campaign-09,visibility-campaign-03,visibility-protocol-03}.log`.
Two impaired desktop clients confirmed Light and NightEye rendering, DetectAnimal,
DetectEnchantment and DetectKey HUD widgets, Invisibility action break, expiry and
reconnect: `build/m4-visibility-desktop-live-08/result.json`. The world screenshots
omit the HUD; marker widget counts are recorded by the automation. Detached AI lacks
sneak stance.

V56 admits eight movement effects into spell/constant source lifetimes, including
WaterBreathing's index zero; T3D0 projects player magnitudes. NPC frames derive
stock movement and breath from sources, stats, inventory and cell water.
Drowning damages NPCs; lethal events use the NPC's life for attribution. Old
V56 images restore full breath.
Stock solver water/jump/slow-fall checks: `build/logs/movement-npc-rules-09.log`;
NPC source, Burden travel, rejection and restart:
`build/logs/movement-npc-service-17.log`; wet-cell SwiftSwim speed,
WaterWalking lift/restart and Burden slowdown:
`build/logs/movement-npc-wet-11.log`; wire:
`build/logs/movement-wire-test-03.log`. Two impaired desktops received all
eight player/NPC effects and identical positions on 671 common NPC ticks:
`build/m4-movement-desktop-live-06/result.json`. Its room was dry. Generated
deep-water content proves drowning, WaterBreathing, expiry and exact host
reconstruction: `build/logs/movement-deep-05.log`. Two impaired desktops also
confirmed protection, expiry, convergent damage and process restart:
`build/m4-movement-deep-desktop-live-03/result.json`. NPC AI issues no jump
requests yet.

**Next: implement the AI/disposition effect family through stock handlers and
committed sources.** Player movement cutover stays after combat and collision
requirements.

62 effects remain to complete across applicable actors and sources:
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

81 implemented + 62 incomplete = 143 IDs; visuals/sources remain bounded.
Scripts, passive sources, multi-NPC/summons/player lives remain unproven.
Ranged/overlapping combat precedes movement cutover; plain ranged sources/body
proxies remain. TR Lua awaits M5.
