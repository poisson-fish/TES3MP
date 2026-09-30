# Current state and next action

**M4 in [PLAN.md](PLAN.md) is active.** [Host](../../apps/tes3mp-server/native/inventory_host.hpp)
steps NPCs/doors at 60 Hz, commits at 30 Hz and binds four neighbors.
Casts, stats, conditions, visibility, movement and AI have vanilla/TR checks;
sneak, AI jumps and mixed passives remain.

Ranged flight, damage, recovery, enchanted impacts and script locals persist.
Enchanted ammo does not recover; item scripts remain closed
(`build/logs/projectile-enchanted-bow-complete-01.log`). V64 aim pays once on
misses and suppresses replayed cues (`build/logs/aim-cue-test.log`). Desktops
shared impacts, death, loot and misses under 10% loss
(`build/logs/desktop-ranged-aim-live-01.log`). Knockdown interpolation passes
`build/logs/body-timeline-test.log`; two-desktop body rendering remains open.

Eight movement IDs pass cast/expiry/restart. V65 Levitation scripts commit
atomically; water effects use stock rules. Neighbor movement/restart pass
`build/logs/movement-script-disabled-test-08.log` and
`build/logs/neighbor-pursuit-test-08.log`.

Neighbor disposition, Command life, selected NPC/creature context and V66
four-neighbor combat pass `build/logs/neighbor-disposition-test-29.log`,
`build/logs/actor-context-creature-01.log`, `build/logs/neighbor-many-05.log`,
`build/logs/neighbor-creature-13.log` and
`build/logs/neighbor-creature-ranged-01.log`. Two desktops see all five
placements and two NPC arrow deaths under loss
(`build/logs/desktop-neighbor-creature-live-02.log`). Alice's arrow kills the
bound creature: both desktops receive one attributed hit, render its terminal
cue nearby, and agree on health 5 to 0 and death after Bob reconnects under
10% loss (`build/logs/desktop-neighbor-creature-damage-live-04.log`). Creature
hit/death body clips remain unverified.

Magic replicates actor/life cast/hit cues and ContinuousVfx. Five bolt models,
four loops and reconnect/expiry pass under loss
(`build/logs/magic-multi-live-04.log`, `build/logs/magic-loop-live-02.log`).
Audio is unverified; capability 31 needs updated desktops.

**Objects/travel started:** trusted Lock/Open stages one ordinary door in its
durable image. Rejection preserves state; committed Lock blocks activation,
Open restores it, and restart retains the lock
(`build/logs/object-lock-open-ordinary-04.log`,
`build/logs/object-lock-open-codec-02.log`,
`build/logs/object-lock-open-area-04.log`).
Player spell payment/contact, actor-tick composition, containers, client lock
projection and sound remain open.

**Next:** wire Lock/Open through player spell contact and the actor tick; then
containers/travel. Capture creature body reaction and mixed combat; bound
equipment, summons and actor/life ownership follow.

62 M4 effects remain:
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
