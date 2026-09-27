# Current state and next action

**M4 in [PLAN.md](PLAN.md) is active.** [Host](../../apps/tes3mp-server/native/inventory_host.hpp): one NPC/doors
at 60 Hz, commits at 30 Hz. Neighbors freeze; players retain contact proxies.

V42 persists KF-bound casting/flight/recovery, RNG, resources and effects for
two players and the NPC. T3C4 requires capability 22.

Two desktops passed vanilla/TR casting, target switching and reconnect/restart
under 10% loss/100 ms delay/jitter:
`build/m4-cast-vanilla-live-10/result.json`,
`build/m4-cast-tr-live-02/result.json`,
`build/m4-cast-vanilla-item-live-03/result.json`,
`build/m4-cast-tr-item-live-04/result.json`.
Records unchanged; stats/placements synthetic. TR Lua remains disabled until M5.

V34 retains melee defense/knockout; get-up timing remains pending. V37 supports
multi-slot constant fortification/resistance; other constant effects reject.
V43 binds participant hit resources/timers. V44–V45 equip carried melee weapons,
repeat stock weighted chop/slash/thrust and remove passive effects on breakage;
recovery never re-equips. V43–V45 evidence is synthetic/headless.

V46 persists player swings/identities. Source/target loss, active disconnect and
hit recovery cancel; inactivity/capacity pause. Simultaneous hits share wear.
T3C4 projects committed sections without gameplay callbacks.

Two desktops passed vanilla `iron longsword` directions/strengths, interruption,
reconnect/restart and duplicate outcomes under 10% loss/100 ms delay/jitter:
`build/m4-swing-vanilla-live-07/result.json`; `build/logs/m4-swing-live-test.log`.
Synthetic actors/placements isolate presentation, not sustained mixed combat.

V48 adds crossbow/bolt and thrown release to V47 bows. Participant
`shoot release` keys commit ammunition, stock fatigue, animation and launch identities.
Crossbows persist stock random strength/RNG for coincident wind-up keys.
Thrown weapons consume themselves, clearing exhausted slots without breaking recovery.
Fresh V48 campaigns required; V47 remains bow-only.

Plain sources only. Eight launches maximum remain frozen. NPC ranged execution,
desktop ranged input and projectile rendering remain pending.

Headless `tes3mp_native_loadout_tests crossbow-release` and `thrown-release` passed
overlapping participants, interruption/source removal, duplicates, rejected/uncertain
writes, wind-up/release restart, malformed recovery and exhaustion:
`build/logs/m4-crossbow-release-test.log`, `build/logs/m4-thrown-release-test.log`.
Synthetic actors/placements use vanilla records/KF resources. Expanded `bow-release`
and shared-path `player-swings` passed: `build/logs/m4-ranged-bow-regression.log`,
`build/logs/m4-ranged-melee-regression.log`. `melee-presentation` passed crossbow/thrown
sampling without gameplay callbacks: `build/logs/m4-ranged-pose-test.log`.
`tes3mp_native_melee_tests melee-timing` passed: `build/logs/m4-ranged-timing-test.log`.

Next: physical flight/collision/impact, NPC ranged execution, desktop input/presentation,
then close combat and composed mixed vanilla/TR encounters per PLAN. Movement remains last.
