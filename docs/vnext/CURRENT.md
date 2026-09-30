# Current state and next action

**M4 in [PLAN.md](PLAN.md) is active.** [Host](../../apps/tes3mp-server/native/inventory_host.hpp)
steps NPCs/doors at 60 Hz, commits at 30 Hz and binds three neighbors maximum.
Casts, conditions, stats, visibility, movement and twelve AI effects have vanilla/TR
checks. Sneak stance, AI jumps, mixed passives and non-player Command remain.

Server-owned bows, crossbows and thrown weapons have durable release, flight,
contact, damage and recovery. Enchanted impact commits WhenStrikes/area effects;
scripted sources retain locals. Enchanted ammunition does not recover; item script
instructions remain closed. Ranged and enchanted retry/restart checks pass:
`build/logs/npc-ranged-{bow,crossbow,thrown}-test-30.log`,
`build/logs/projectile-enchanted-{bow,crossbow,thrown}-complete-01.log`.
The neighbor cap is provisional.

V64 persists camera aim and sweeps server hulls. Misses pay once; terminal cues
do not replay on reconnect. Checks: `build/logs/aim-protocol-test.log`,
`build/logs/aim-cue-test.log`, `build/logs/bow-aim-enchanted-test.log`.

The body timeline interpolates knockdown loops and holds across hit clips.
Four-actor attack, hit, fall, get-up, loss and reconnect checks pass:
`build/logs/body-timeline-test.log`; desktop object compiled in
`build/logs/body-pose-object-build.log`. Two-desktop body rendering is unverified.

Two clients under latency/jitter/10% loss shared physical impacts, death, corpse
transfer and a miss: `build/logs/desktop-ranged-aim-live-01.log`. A floor contact,
one arrow cost and both cues pass `build/logs/desktop-ranged-world-hull-05.log`.

Eight movement IDs admit passive abilities through shared sources.
Timed casts, two observers, retry, expiry and restart pass
`build/logs/movement-passive-test-02.log`. Earlier V56 saves with ignored abilities
need reset. Underwater WaterWalking, disabled Levitate and neighbor physics remain.

Magic presentation replicates Target flights and reliable spell/enchanted impacts.
Current rendering lacks stock composite bolts, bolt light/sound and
cast/hit/loop parity. Protocol, two-observer, CastOnce/restart, area and timeline checks pass:
`build/logs/magic-vfx-{protocol-test,concurrent-test-10,area-test-02,enchanted-bow-test-01,adapter-test}.log`.
Client build: `build/logs/magic-vfx-openmw-build-dev.log`. Capability 30 requires
updated desktops. Live magic rendering is unverified.

**Next:** verify magic bolts and explosions on desktops, then close movement
exceptions. Continue AI/disposition, object/travel, bound equipment and summons;
generalize actor/life ownership before player movement cutover.

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

81 implemented + 62 incomplete = 143 IDs. Broader multi-NPC combat, summons
and player lives remain unproven.
TR Lua awaits M5.
