# Current state and next action

**M4 in [PLAN.md](PLAN.md) is active.** [Host](../../apps/tes3mp-server/native/inventory_host.hpp)
steps NPCs/doors at 60 Hz, commits at 30 Hz and binds four neighbors. Vanilla/TR
checks cover casts, stats, conditions, visibility, movement and AI; sneak,
AI jumps and mixed passives remain.

Ranged combat persists and V64 aim pays once on misses
(`build/logs/aim-cue-test.log`). Two desktops share it under loss
(`build/logs/desktop-ranged-aim-live-01.log`). Enchanted ammo recovery,
item scripts and body rendering remain open.

Eight movement IDs pass cast/expiry/restart; Levitation scripts and water
use stock rules (`build/logs/movement-script-disabled-test-08.log`).

Four-neighbor combat passes (`build/logs/neighbor-creature-ranged-01.log`).
Two desktops see five placements and arrow deaths under loss
(`build/logs/desktop-neighbor-creature-live-02.log`). Both agree on Alice's
attributed creature kill after Bob reconnects
(`build/logs/desktop-neighbor-creature-damage-live-04.log`). Body reactions remain unverified.

Magic bolts, loops and reconnect pass under loss (`build/logs/magic-multi-live-04.log`,
`build/logs/magic-loop-live-02.log`). Audio remains unverified.

**Objects/travel started:** trusted and player Lock/Open use one streamed
unowned door. Touch checks door, contact revision, range and sight at admission
and release. The actor tick commits cost/RNG with the lock; failed writes leak
nothing. Lost contact or another activation cancels before payment. Both
baselines carry lock level/contact revision, and restart retains paid
Lock/Open (`build/logs/object-spell-test-06.log`). Under 10% loss, both desktops
receive Lock 50/revision 201 then Open 0/revision 1. Activation blocks then
resumes; Bob reconnects to the open door without duplicate costs or cues
(`build/logs/object-magic-live-18.log`). Synthetic lit room, vanilla loadout;
door glow/sound, Target range and mixed effects remain unverified.

**Next:** extend committed object magic to containers/travel.
Capture creature reactions and mixed combat; bound equipment, summons and
actor/life ownership follow.

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
