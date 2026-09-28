# Current state and next action

**M4 in [PLAN.md](PLAN.md) is active.** [Host](../../apps/tes3mp-server/native/inventory_host.hpp):
NPC/biped/doors: 60 Hz; commits: 30 Hz; neighbors freeze.
**V56, T3D0/capability 28; fresh campaigns.**

Inherited body captures: `build/m4-custom-<body>-<mode>-live-<suffix>`
for dancer/yakov, khajiit-f/TR_m1_Carajhi,
khajiit-m/TR_m1_Batharra and tsaesci/TR_m7_Qorinue-Najan.
Corpse poses and armed/casting creatures untested.

Player/NPC casts retain timing, payment and interruption; custom casts check
resources: `build/m4-player-cast-{vanilla-live-02,tr-item-live-01}`.
Silence defeats Always; Sound stacks; failed spells pay once:
`build/logs/interference-*`. Shields/stats:
`build/logs/effect-family-{rules,shields-04,stats-03,fortify-02}.log`.
Cures/Dispel preserve sources; V54 diseases/curses persist through contact,
resistance and restart: `build/logs/persistent-conditions-20260927-06.log`.
V55 Corprus/Vampirism/SunDamage use native time/weather:
`build/logs/{special-conditions-09,environment-sun-03,special-admission-rules-02}.log`;
V54 regression: `build/logs/persistent-regression-02.log`.
Wait restores fatigue; rest restores health/magicka; StuntedMagicka blocks
recovery through skips: `build/logs/{rest-recovery-08,rest-contract}.log`.
Other deadlines survive; VFX pending. Disintegration uses stock fractional
condition/armor priority and unequips broken gear:
`build/logs/disintegration-13.log`. Constant ticking and desktop unproven.

Invisibility/Chameleon use stock hit and awareness; casts/attack release break
temporary Invisibility and suppress equipped sources until replacement. T3D0
projects seven visibility effects from timed/constant sources to OpenMW rendering,
night vision and detection. Lifecycle checks:
`build/logs/{constant-concealment-01,concealment-campaign-09,visibility-campaign-03,visibility-protocol-03}.log`.
Two impaired desktops confirmed rendering, three Detect widgets, action break,
expiry and reconnect: `build/m4-visibility-desktop-live-08/result.json`.
HUD counts came from automation; detached AI lacks sneak stance.

V56 admits eight movement effects, including WaterBreathing index zero, into
spell/constant sources; T3D0 projects player magnitudes. NPCs derive stock motion
and breath from committed state; drowning carries life attribution. Old V56
images restore full breath. Solver/source/wet/wire/deep-water checks:
`build/logs/{movement-npc-rules-09,movement-npc-service-17,movement-npc-wet-11,movement-wire-test-03,movement-deep-05}.log`.
Two impaired desktops converged across 671 dry-room NPC ticks and a separate
deep-water expiry/restart capture:
`build/m4-movement-desktop-live-06/result.json`,
`build/m4-movement-deep-desktop-live-03/result.json`. NPC AI has no jump request.

The twelve AI/disposition IDs now enter bounded timed spell/WhenUsed sources.
Stock and server handlers share target-type checks and Fight/Flee deltas. The
selected NPC and an undead biped creature consume Calm/Frenzy/Demoralize/Rally
and TurnUndead in their combat choice; valid level-gated Command sources stage
Follow navigation toward a player caster, then resume authored travel at expiry.
Charm retains magnitude for the NPC, but dialogue does not consume it yet.
Synthetic source/type rules: `build/logs/ai-rules-02.log`; NPC and undead creature
stacking, expiry, rejected writes, Follow, restart:
`build/logs/{ai-disposition-npc-10,ai-disposition-creature-06}.log`.
Movement regression: `build/logs/ai-movement-regression-01.log`.
No desktop capture. Stock distance/disposition aggression and low-health Flee
rating, flee path, Charm dialogue, passive/constant AI sources, non-player
Command casters and multiple active combatants remain open.

**Next: finish stock Fight/Flee and Charm consumers across actors and sources.**
Player movement cutover stays after combat and collision requirements.

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
