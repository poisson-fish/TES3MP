# Current state and next action

**M4 in [PLAN.md](PLAN.md) is active.** [Host](../../apps/tes3mp-server/native/inventory_host.hpp)
steps NPCs/doors at 60 Hz, commits at 30 Hz and bounds same-cell neighbors
to three. Player/NPC casts, conditions, stats, Disintegration, concealment, visibility,
movement and twelve AI effects have bounded vanilla/TR checks. VFX, sneak
stance and AI jumps remain.

Server-owned bows, crossbows, arrows, bolts and thrown weapons use authored
release, durable flight, first-hull/world contact and atomic damage/recovery.
Unscripted enchanted launchers and projectiles share that path. On successful
actor impact, projectile WhenStrikes Self/Touch/Target and area effects commit
with damage and receipt; enchanted ammunition does not recover. Scripts remain
closed. Death clears flights. Ranged rejection, overlap, recovery and restart pass:
`build/logs/npc-ranged-{bow,crossbow,thrown}-test-30.log`. Synthetic enchanted
impact, failed-write, retry and restart checks pass:
`build/logs/projectile-enchanted-{bow,crossbow,thrown}-complete-01.log`.
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

**Next:** scripted sources and varied two-client ranged loadouts/attack modes.

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
