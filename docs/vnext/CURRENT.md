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

Desktop bow release intercepts OpenMW's shoot key for mapped targets. The
server charges ammunition, flies shots and writes contact, damage and loot.
Bounded combat snapshots carry flight/terminal receipts; desktop effects
interpolate and clear on contact/reconnect. Protocol/timeline checks pass.
`build/logs/desktop-ranged-live-24.log`: two graphical clients, 100 ms latency,
jitter and 10% loss; four shared flight ticks, 14/50 rendered frames, two arrow
costs, identical impacts/death and one 17-item corpse transfer retained through
reconnect. The fixture check passes in
`build/logs/desktop-ranged-fixture-test-07.log`.

**Next:** carry authoritative aim for misses and world shots. Unmapped aim is
currently suppressed. Very short flights can finish between delayed snapshots;
add a durable visual cue while preserving server-owned contact.

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
