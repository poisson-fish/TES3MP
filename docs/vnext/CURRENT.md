# Current state and next action

**M4 in [PLAN.md](PLAN.md) is active.** [Host](../../apps/tes3mp-server/native/inventory_host.hpp)
steps NPCs/doors at 60 Hz and commits at 30 Hz with three neighbors maximum.
Casts, conditions, stats, visibility, movement and twelve AI effects have vanilla/TR
checks. VFX, sneak stance, AI jumps, mixed passives and non-player Command remain.

Server-owned bows, crossbows and thrown weapons use authored release, durable
flight, first-hull/world contact and atomic damage/recovery. Scripted sources
retain locals through saved flight and recover with fresh identity. Enchanted
launchers/projectiles share flight; actor impact commits WhenStrikes/area effects,
damage and receipt. Enchanted ammunition does not recover; item script instructions
remain closed. Death clears flights. Ranged rejection/recovery/restart checks pass:
`build/logs/npc-ranged-{bow,crossbow,thrown}-test-30.log`. Synthetic enchanted
impact, failed-write, retry and restart checks pass:
`build/logs/projectile-enchanted-{bow,crossbow,thrown}-complete-01.log`.
The neighbor cap is provisional.

V64 accepts bounded camera aim without a mapped actor, persists launch direction
and sweeps server hulls. Targetless misses pay once. Recent terminal receipts
present 250 ms cues; reconnect does not replay them. Checks pass:
`build/logs/aim-protocol-test.log`, `build/logs/aim-cue-test.log`,
`build/logs/bow-aim-enchanted-test.log`.

The body timeline now interpolates knockdown loop wraps and holds across hit-clip
changes. Desktop traces use each attack/cast section's text key. A filtered
four-actor check covers attacks, hit, fall, loop, get-up, loss and reconnect:
`build/logs/body-timeline-test.log`. The desktop object compiled:
`build/logs/body-pose-object-build.log`. Two-desktop rendering is unverified.

`build/logs/desktop-ranged-aim-live-01.log`: two clients under 100 ms latency,
jitter and 10% loss shared impacts/death, corpse transfer and an expired miss.
`build/logs/desktop-ranged-world-hull-05.log` proves targetless floor contact,
one arrow spent and both terminal cues.

**Next:** capture 60-FPS body pose/frame traces on both desktops across attack
modes, hit, knockdown/knockout and get-up under loss/jitter/reconnect, including
stock-speed clip comparison. Exercise scripted projectile sources and varied
two-client ranged loadouts in the deferred testing pass; scripted impact/recovery
and live convergence remain unverified.

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

81 implemented + 62 incomplete = 143 IDs. Broader multi-NPC combat, summons
and player lives remain unproven. Ranged/body work precedes movement cutover.
TR Lua awaits M5.
