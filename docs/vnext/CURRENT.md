# Current state and next action

**M4 in [PLAN.md](PLAN.md) is active.** [Host](../../apps/tes3mp-server/native/inventory_host.hpp)
steps NPCs/doors at 60 Hz, commits at 30 Hz and bounds same-cell neighbors
to three. Player/NPC casts, conditions, stats, Disintegration, concealment, visibility,
movement and twelve AI effects have bounded vanilla/TR checks. VFX, sneak
stance and AI jumps remain.

Server-owned bows, crossbows and throwing stars use authored release, durable
flight, first-hull/world contact and atomic damage/recovery/loot. Unscripted
enchanted bow/crossbow launchers share that path; projectile enchantments and
scripts await atomic on-hit execution. NPC life survives restart; death clears
flights. Real-loadout rejection, overlapping-impact, lethal-recovery and
restart checks pass: `build/logs/npc-ranged-{bow,crossbow,thrown}-test-30.log`.
The neighbor cap is provisional. Mixed passives and non-player Command remain;
world-transfer conflicts reject atomically.

V64 accepts bounded desktop camera aim without a mapped actor, freezes launch
direction through restart and sweeps server hulls. Targetless misses pay once
and retain receipts. Receipts seen within 30 ticks present a 250 ms impact cue
even without a flight snapshot; reconnect does not replay it. Checks pass:
`build/logs/aim-protocol-test.log`, `build/logs/aim-cue-test.log`,
`build/logs/bow-aim-enchanted-test.log`. The V64 fixture retains a Slash intent
through the bound shoot clip and restart.

`build/logs/desktop-ranged-aim-live-01.log`: two graphical clients under
100 ms latency, jitter and 10% loss shared two impacts/death, one corpse
transfer and a targetless expired miss. Bob reconnected mid-flight; both
received its receipt. Relay drops: 1,519. With a plain bow,
`build/logs/desktop-ranged-world-hull-05.log` proves targetless floor contact
in seven ticks, one arrow spent, no actor hit and both terminal cues under
325 relay drops.

**Next:** carry projectile enchantments/scripts through atomic impact and
recovery, then cover varied ranged sources and attack modes live while retaining
the V64 aim and receipt path.

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
