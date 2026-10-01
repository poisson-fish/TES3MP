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
An unkeyed container shares this path: locks block activation and take/put;
Open restores both. Failed writes, retry, payment, restart and observers pass
(`build/logs/container-lock-test-final-15.log`);
both desktops see Lock/Open and stock activation under loss
(`build/logs/container-spell-live-final-07.log`). Synthetic lit room, vanilla loadout;
door/container visuals, door audio, Target range and mixed effects remain open.

The ground-item codec check passes, including the corrected 192-entry overflow
case (`build/logs/ground-item-codec-test-03.log`).

**Mark/Recall (V68):** each player owns one Mark. Unmarked Recall pays without
teleporting. Bounded stock Enable/DisableTeleporting scripts persist the rule;
disabled Mark/Recall pay without changing Marks or position. Fresh campaign required.
Self travel follows online players across bound cells; interior NPC processing
stays separate from streaming. Synthetic checks prove unloaded-interior Recall,
split wire baselines, old-epoch rejection, independent Marks, rejected-write retry,
failed-write closure and coherent restart/recovery without repeated payment or
teleport (`build/logs/player-travel-test-20.log`). Levitation regression passes
(`build/logs/travel-movement-regression-01.log`). Live travel/presentation/audio remain unverified.

**Next:** capture two desktops exercising the complete Mark/Recall travel path
under loss with a fresh V68 campaign, then intervention travel; continue creature
reactions and mixed combat, then bound equipment, summons and actor/life ownership.

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
