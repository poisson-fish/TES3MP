# Current state and next action

**M4 in [PLAN.md](PLAN.md) is active.** [Host](../../apps/tes3mp-server/native/inventory_host.hpp)
steps NPCs/doors at 60 Hz, commits at 30 Hz and binds three neighbors.
Casts, conditions, stats, visibility, movement and twelve AI effects have vanilla/TR
checks. Sneak, AI jumps, mixed passives and non-player Command remain.

Server bows/crossbows/throws persist flight, contact, damage and recovery,
including enchanted WhenStrikes/area and script locals. Enchanted ammo does not
recover; item scripts remain closed. Checks:
`build/logs/npc-ranged-{bow,crossbow,thrown}-test-30.log`,
`build/logs/projectile-enchanted-{bow,crossbow,thrown}-complete-01.log`.

V64 persists camera aim and sweeps server hulls. Misses pay once; reconnect
does not replay cues. Checks: `build/logs/aim-protocol-test.log`,
`build/logs/aim-cue-test.log`, `build/logs/bow-aim-enchanted-test.log`.

The body timeline interpolates knockdown loops. Four-actor loss/reconnect passes
`build/logs/body-timeline-test.log`; desktop object builds in
`build/logs/body-pose-object-build.log`. Two-desktop rendering is unverified.

Two clients under 10% loss shared impacts, death, corpse transfer and a miss:
`build/logs/desktop-ranged-aim-live-01.log`. Floor contact and cues pass
`build/logs/desktop-ranged-world-hull-05.log`.

Eight movement IDs admit passives; cast, expiry and restart pass
`build/logs/movement-passive-test-02.log`. Deep-water WaterWalking rejects NPCs
and players before effect install; shallow water accepts and lifts moving actors.
V65 commits stock-script Disable/EnableLevitation with effect removal/restoration,
atomic rejection and restart. Neighbors use effect-derived stock movement and
drowning; WaterBreathing protects them through expiry. Disconnect/reconnect
projections and restart converge. Checks: `build/logs/movement-script-disabled-test-08.log`,
`movement-player-{deep-test-02,wet-test-01}.log`,
`movement-neighbor-{physics-test-04,deep-test-01,combat-regression-01}.log`.
General script scheduling and broader neighbor movement responses remain.

Magic presentation replicates cast/hit cues and active ContinuousVfx by
actor/life, restoring loops without one-shot replay. Under 10% loss, two
desktops cast four-Target-effect `vivec's_wrath`, rendering five bolt models
and light color `[.831373,.57549,.521569,1]` each. An extended fixture showed
four loops, reconnect restoration and expiry. Evidence:
`build/logs/magic-multi-live-04.log`, `build/logs/magic-loop-live-02.log`,
their `build/*/result.json` and screenshots. Audible quality remains unverified.
Focused checks pass; capability 31 requires updated desktops.

**Next:** verify remaining neighbor movement responses during pursuit, then
continue AI/disposition, object/travel, bound equipment and summons. Generalize
actor/life ownership before player movement cutover.

62 effects remain in the M4 completion inventory across actors and sources:
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

81 + 62 = 143 IDs. Summons/player lives remain unproven.
