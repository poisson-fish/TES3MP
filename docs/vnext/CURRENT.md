# Current state and next action

**M4 in [PLAN.md](PLAN.md) is active.** The
[host](../../apps/tes3mp-server/native/inventory_host.hpp) simulates one native NPC
and doors at 60 Hz, committing at 30 Hz. Neighbors freeze; players use contact
proxies until movement cutover.

V42 persists casting/flight/recovery, RNG, resources and effects through bound KF
keys. Two players cast alongside the NPC. T3C4 requires capability 22.

Two-desktop casting passed target switching, reconnect and wind-up restart
  under 10% loss/100 ms delay/jitter:
  `build/m4-cast-vanilla-live-10/result.json` (`Fireball_large`) and
  `build/m4-cast-tr-live-02/result.json` (`T_Ayl_Des_DWelkynd_FIR`), plus equipped
  `cruel flamebolt ring` in `build/m4-cast-vanilla-item-live-03/result.json` and
  `T_De_Ep_Ring_Chill` in `build/m4-cast-tr-item-live-04/result.json` (both wind-up images).
  Gameplay records unchanged; actor stats/placements synthetic.
  TR Lua packages are disabled; native Lua services remain M5 work.

V43 binds participant hit resources/timers.

V34 retains melee defense and knockout; get-up timing remains pending.
V37 supports multi-slot constant fortification/resistance;
other constant effects reject.

V44–V45 equip carried melee weapons, repeat swings and persist stock weighted
chop/slash/thrust selection. Breakage removes passive effects atomically; recovery
never re-equips.
V43–V45 evidence is synthetic/headless.

V46 persists player wind-up/release/hit/follow-through, requested direction/strength,
source and target life. Source changes, active disconnect, target loss and hit
recovery cancel; capacity/inactivity pause. Simultaneous hits share armor wear.
Fresh campaigns required; older domains retain immediate hits.

T3C4 replicates swing identities, phases, direction/strength, interruption and
section progress to self/peers without gameplay keys or Lua callbacks. Reconnect
retains terminal states. V46 persistence is unchanged; local input predicts before admission.

Headless `tes3mp_native_loadout_tests player-swings` and `melee-presentation` passed
transaction/recovery and KF clock/callback checks:
`build/logs/m4-swing-host-test.log`, `build/logs/m4-swing-pose-test.log`. Protocol/handshake:
`build/logs/m4-swing-protocol-test.log` and `build/logs/m4-swing-handshake-test.log`.

Two desktops passed V46 swings with vanilla `iron longsword` under 10% loss/100 ms
delay/jitter: three directions/strength tiers, disconnect cancellation, reconnect,
pending-swing restart and duplicate-outcome checks. Peer swing images reviewed.
`build/m4-swing-vanilla-live-07/result.json`; `build/logs/m4-swing-live-test.log`.
Actors/placements are synthetic; this isolates presentation, not sustained mixed combat.

Next: ranged release and durable ammunition expenditure, then flight/impact,
close-combat completion, physical/casting composition and varied vanilla/TR
death/loot encounters in PLAN. Movement remains last.
