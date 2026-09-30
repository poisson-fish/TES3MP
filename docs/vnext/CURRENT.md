# Current state and next action

**M4 in [PLAN.md](PLAN.md) is active.** [Host](../../apps/tes3mp-server/native/inventory_host.hpp)
steps NPCs/doors at 60 Hz, commits at 30 Hz and bounds neighbors to three.
Casts, conditions, stats, visibility, movement and twelve AI effects have
bounded vanilla/TR checks. VFX, sneak stance and AI jumps remain.

Server-owned bows, crossbows, arrows, bolts and thrown weapons use authored
release, durable flight, first-hull/world contact and atomic damage/recovery.
Enchanted launchers and projectiles share that path. Scripted ranged source
records now retain bounded locals in the source inventory during saved flight; recovery
copies the consumed source state to the victim with a fresh identity. On successful
actor impact, projectile WhenStrikes Self/Touch/Target and area effects commit
with damage and receipt; enchanted ammunition does not recover. Item script
instructions remain closed. Death clears flights. Earlier ranged rejection,
overlap, recovery and restart checks pass:
`build/logs/npc-ranged-{bow,crossbow,thrown}-test-30.log`. Synthetic enchanted
impact, failed-write, retry and restart checks pass:
`build/logs/projectile-enchanted-{bow,crossbow,thrown}-complete-01.log`.
The neighbor cap is provisional. Mixed passives and non-player Command remain.

V64 accepts bounded desktop camera aim without a mapped actor, freezes launch
direction through restart and sweeps server hulls. Targetless misses pay once
and retain receipts. Receipts seen within 30 ticks present a 250 ms impact cue
even without a flight snapshot; reconnect does not replay it. Checks pass:
`build/logs/aim-protocol-test.log`, `build/logs/aim-cue-test.log`,
`build/logs/bow-aim-enchanted-test.log`.

`build/logs/desktop-ranged-aim-live-01.log`: two clients under 100 ms latency,
jitter and 10% loss shared impacts/death, corpse transfer and an expired miss;
reconnect received a mid-flight receipt. `build/logs/desktop-ranged-world-hull-05.log`
proves targetless floor contact, one arrow spent and both terminal cues.

**Next:** exercise scripted sources and varied two-client ranged loadouts/attack
modes in the deferred testing pass. Only the three touched C++ objects compiled;
scripted impact/recovery and live convergence are unverified.

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
