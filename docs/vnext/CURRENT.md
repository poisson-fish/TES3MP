# Current state and next action

**M4 in [PLAN.md](PLAN.md) is active.** The
[host](../../apps/tes3mp-server/native/inventory_host.hpp) simulates one native NPC
and doors at 60 Hz, committing at 30 Hz. Neighbors freeze; players use contact
proxies until movement cutover.

V42 persists KF-bound casting/flight/recovery, RNG, resources and effects for
two players and the NPC. T3C4 requires capability 22.

Two desktops passed vanilla/TR casting, target switching, reconnect and wind-up
restart under 10% loss/100 ms delay/jitter:
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

V46 persists player swing phases, direction/strength and identities. Source/target
loss, active disconnect and hit recovery cancel; inactivity/capacity pause.
Simultaneous hits share wear. T3C4 projects committed sections without gameplay callbacks.

Two desktops passed vanilla `iron longsword` directions/strengths, interruption,
reconnect/restart and duplicate outcomes under 10% loss/100 ms delay/jitter:
`build/m4-swing-vanilla-live-07/result.json`; `build/logs/m4-swing-live-test.log`.
Synthetic actors/placements isolate presentation, not sustained mixed combat.

V47 binds headless player bow/arrow release to the participant's `shoot release`
KF key. One transaction spends an arrow and stock fatigue, persisting animation
progress, launch/source/target identity, strength, position and direction.
Fresh campaigns required. Plain sources only;
up to eight launches remain frozen pending flight/impact. Desktop bow input and
projectile rendering are not wired.

`tes3mp_native_loadout_tests bow-release` passed interruption, duplicate requests,
rejected/uncertain writes, wind-up/release restart, malformed recovery and last-arrow
exhaustion: `build/logs/m4-bow-release-test.log`. Synthetic actors/placements use
vanilla bow/arrow records and KF resources. Extended `melee-presentation` passed
bow section sampling without gameplay callbacks: `build/logs/m4-bow-pose-test.log`.
The shared-path `player-swings` regression passed: `build/logs/m4-bow-melee-regression.log`.

Next: extend ranged release to crossbows/thrown weapons, then shared flight/impact
and desktop ranged input/presentation. Later combat/encounter work follows PLAN;
movement remains last.
