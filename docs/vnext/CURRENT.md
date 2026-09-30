# Current state and next action

**M4 in [PLAN.md](PLAN.md) is active.** [Host](../../apps/tes3mp-server/native/inventory_host.hpp)
steps NPCs/doors at 60 Hz and commits at 30 Hz. V63 bounds same-cell
neighbors to three; others freeze. Player/NPC casts, conditions, stats,
Disintegration, concealment, visibility, movement effects and twelve AI
effects have bounded vanilla/TR checks. VFX, sneak stance and AI jumps remain.

Server-owned bows, crossbows and throwing stars use authored release, durable
flight, first-hull/world contact and atomic damage, recovery and loot. NPC
life/placement survives restart; death clears old flights. Narrow real-loadout
rejection, overlapping-impact, lethal-recovery and restart checks pass:
`build/logs/npc-ranged-{bow,crossbow,thrown}-test-30.log`. The three-neighbor
bound is provisional. Mixed passives, non-player Command and scripted or
enchanted ranged sources remain open; world-transfer conflicts reject atomically.

V64 accepts bounded desktop camera aim even without a mapped actor, freezes its
server launch direction through wind-up/restart and uses server hull/world sweep
for first contact. Targetless misses pay ammunition once and retain terminal
receipts. A terminal receipt seen within 30 ticks of release presents a 250 ms
impact cue, including when no flying snapshot arrived; reconnect does not replay
an already seen cue. Protocol, timeline and atomic world-miss/restart checks pass:
`build/logs/aim-protocol-test.log`, `build/logs/aim-cue-test.log`,
`build/logs/bow-aim-fixture-final.log`.

`build/logs/desktop-ranged-aim-live-01.log`: two graphical clients with 100 ms
one-way latency, jitter and 10% loss rendered both terminal cues, shared two
impacts/death and one 17-item corpse transfer. A targetless world shot after
death charged one arrow; Bob reconnected mid-flight, and both clients received
its terminal receipt. The relay dropped 1,519 packets. The world miss expired
without an actor contact in this fixture; quick world-hull contact still needs
live coverage.

**Next:** extend ranged sources and attack modes beyond plain bows, crossbows
and thrown weapons, retaining the V64 aim and receipt path.

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
