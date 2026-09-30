# Current state and next action

**M4 in [PLAN.md](PLAN.md) is active.** [Host](../../apps/tes3mp-server/native/inventory_host.hpp)
steps NPCs/doors at 60 Hz and commits at 30 Hz with three neighbors maximum.
Casts, conditions, stats, visibility, movement and twelve AI effects have vanilla/TR
checks. VFX, sneak stance, AI jumps, mixed passives and non-player Command remain.

Server-owned bows, crossbows and thrown weapons use authored release, durable
flight, first contact and atomic damage/recovery. Scripted sources retain locals;
enchanted impact commits WhenStrikes/area effects and receipts. Enchanted ammunition
does not recover; item script instructions remain closed. Ranged checks pass:
`build/logs/npc-ranged-{bow,crossbow,thrown}-test-30.log`. Synthetic enchanted
impact, failed-write, retry and restart checks pass:
`build/logs/projectile-enchanted-{bow,crossbow,thrown}-complete-01.log`.
The neighbor cap is provisional.

V64 persists bounded camera aim and sweeps server hulls. Targetless misses pay
once; 250 ms terminal cues do not replay on reconnect. Checks pass:
`build/logs/aim-protocol-test.log`, `build/logs/aim-cue-test.log`,
`build/logs/bow-aim-enchanted-test.log`.

The body timeline interpolates knockdown loops and holds across hit-clip changes.
Desktop traces use attack/cast text keys. Four-actor checks cover attacks, hit,
fall, get-up, loss and reconnect:
`build/logs/body-timeline-test.log`. The desktop object compiled:
`build/logs/body-pose-object-build.log`. Two-desktop rendering is unverified.

`build/logs/desktop-ranged-aim-live-01.log`: two clients under 100 ms latency,
jitter and 10% loss shared impacts/death, corpse transfer and an expired miss.
`build/logs/desktop-ranged-world-hull-05.log` proves targetless floor contact,
one arrow spent and both terminal cues.

Eight movement IDs now admit authored passive abilities through the shared
source lifecycle. An eight-effect ability, timed casts, two-observer projection,
atomic retry, expiry and exact restart pass `build/logs/movement-passive-test-02.log`.
Earlier V56 images with ignored abilities need reset. Underwater WaterWalking,
disabled Levitate, neighbor physics and live visuals remain.

**Next:** close those movement exceptions, then complete AI/disposition, object/travel,
bound equipment and summons. Generalize actor/life ownership.
Cut over player movement. Defer two-desktop body,
ranged loadout, scripted projectile and overlapping-combat runs to M4 acceptance.

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
