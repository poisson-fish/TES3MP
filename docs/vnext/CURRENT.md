# Current state and next action

**M4 in [PLAN.md](PLAN.md) is active.** [Host](../../apps/tes3mp-server/native/inventory_host.hpp)
steps NPCs/doors at 60 Hz, commits at 30 Hz and binds four neighbors. Vanilla/TR
checks cover core combat and AI; sneak, AI jumps and mixed passives remain.

Ranged combat and V64 aim pass under loss (`build/logs/desktop-ranged-aim-live-01.log`).
Enchanted ammo, scripts and bodies remain open.

Eight movement IDs pass cast/expiry/restart; Levitation scripts and water use stock rules.

Four-neighbor combat and attributed creature kills pass under loss and reconnect
(`build/logs/desktop-neighbor-creature-damage-live-04.log`). Body reactions remain unverified.

Magic bolts, loops and reconnect pass under loss (`build/logs/magic-loop-live-02.log`).
Audio remains unverified.

**Objects/travel started:** trusted and player Touch Lock/Open use one streamed
unowned door. Contact, range and sight are checked at admission and release;
cost/RNG and lock commit atomically. Restart and two desktops under 10% loss
pass (`build/logs/object-spell-test-06.log`, `build/logs/object-magic-live-18.log`).
One unkeyed container shares that path. Locked activation
and take/put fail; Open restores both. Failed writes, retry, payment,
restart and two observers pass (`build/logs/container-lock-test-final-15.log`);
both desktops see Lock/Open and stock activation under loss
(`build/logs/container-spell-live-final-07.log`). Synthetic lit room, vanilla loadout;
door/container visuals, door audio, Target range and mixed effects remain open.

The rebuilt ground-item codec check passes (`build/logs/ground-item-codec-test-03.log`).
The earlier malformed case used a valid distinct placement ID; the focused check
now exceeds the 192-entry bound and retains the other malformed cases.

**Mark/Recall (V67):** a successful Self Mark saves one transform per player.
Recall pays through the native cast and stages that player's destination in the
same durable candidate as the canonical teleport and authority-epoch change.
Focused two-player synthetic checks cover distinct marks, rejected writes,
reconnect generations and restart (`build/logs/player-travel-test-12.log`);
the reducer durability check covers atomic image/spatial publication
(`build/logs/travel-reducer-test-05.log`). Live travel, presentation/audio,
cross-cell behavior and stock restrictions remain unverified.

**Next:** verify Mark/Recall cross-cell and live behavior, then intervention
travel; continue creature reactions and mixed combat, then bound equipment,
summons and actor/life ownership.

60 M4 effects remain:
- Movement (8): WaterBreathing, SwiftSwim, WaterWalking, Burden, Feather,
  Jump, Levitate, SlowFall.
- AI/disposition (12): Charm, CalmHumanoid, CalmCreature, FrenzyHumanoid,
  FrenzyCreature, DemoralizeHumanoid, DemoralizeCreature, RallyHumanoid,
  RallyCreature, CommandHumanoid, CommandCreature, TurnUndead.
- Objects (4): Lock, Open, Telekinesis, Soultrap.
- Travel (2): DivineIntervention, AlmsiviIntervention.
- Equipment (12): BoundDagger, BoundLongsword, BoundMace, BoundBattleAxe,
  BoundSpear, BoundLongbow, BoundCuirass, BoundHelm, BoundBoots, BoundShield,
  BoundGloves, ExtraSpell (preserve actual stock behavior).
- Summons (22): SummonScamp, SummonClannfear, SummonDaedroth, SummonDremora,
  SummonAncestralGhost, SummonSkeletalMinion, SummonBonewalker,
  SummonGreaterBonewalker, SummonBonelord, SummonWingedTwilight, SummonHunger,
  SummonGoldenSaint, SummonFlameAtronach, SummonFrostAtronach, SummonStormAtronach,
  SummonCenturionSphere, SummonFabricant, SummonWolf, SummonBear, SummonBonewolf,
  SummonCreature04, SummonCreature05.

83 + 60 = 143 IDs. Summons/player lives remain unproven.
