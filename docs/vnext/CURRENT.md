# Current state and next action

**M4 in [PLAN.md](PLAN.md) is active.** [Host](../../apps/tes3mp-server/native/inventory_host.hpp): one NPC/doors
at 60 Hz, commits at 30 Hz. Neighbors freeze; players retain contact proxies.

V42 persists casting/flight/recovery for all participants. T3C4 requires capability 22.

Two desktops passed vanilla/TR casting, target switching and reconnect/restart
under 10% loss/100 ms delay/jitter:
`build/m4-cast-vanilla-live-10/result.json`,
`build/m4-cast-tr-live-02/result.json`,
`build/m4-cast-vanilla-item-live-03/result.json`,
`build/m4-cast-tr-item-live-04/result.json`.
Records unchanged; stats/placements synthetic. TR Lua remains disabled until M5.

V34 defense/knockout lacks get-up timing. V37 constants cover fortification/resistance.
V43-V46 bind participant resources, weapon selection, directional swings,
interruption and shared wear. Recovery never re-equips. T3C4 presents committed poses.

Two desktops passed vanilla `iron longsword` directions/strengths, interruption,
reconnect/restart and duplicate outcomes under 10% loss/100 ms delay/jitter:
`build/m4-swing-vanilla-live-07/result.json`; `build/logs/m4-swing-live-test.log`.
Synthetic actors/placements; sustained mixed combat remains unproven.

V49 persists 60 Hz player flight, NPC/world/door impacts, resources and RNG atomically
through disconnect/restart. V47/V48 retain frozen launches.

Inherited headless `tes3mp_native_loadout_tests` flight/failure/recovery evidence:
`build/logs/m4-bow-flight-test.log`, `build/logs/m4-crossbow-flight-test.log`,
`build/logs/m4-thrown-flight-test.log`.
Unchanged vanilla weapon/KF records; synthetic actors/skills/placements.
Shared mechanics: `build/logs/m4-projectile-mechanics-test.log`.

V49 now recycles terminal receipts after the 64-tick input window once no saved
swing references them. The committed tick rejects stale retries after restart;
eight records bound concurrent flight/receipts, not campaign shots. Save layout unchanged.
`tes3mp_native_loadout_tests`: `bow-recycling-flight`, `crossbow-recycling-flight`,
`thrown-recycling-flight` passed 24 launches each, rejected/uncertain writes and retirement/retry recovery:
`build/logs/m4-bow-recycling-test.log`, `build/logs/m4-crossbow-recycling-test.log`,
`build/logs/m4-thrown-recycling-test.log`.
`bow-flight` regression passed: `build/logs/m4-recycling-flight-regression.log`.

Plain player sources/body-center launch proxy only. NPC ranged execution, player
contacts, ammunition recovery, desktop input/rendering and vanilla/TR live acceptance remain pending.
**Two-client ranged acceptance is not met.**
V48 `thrown-release` regression passed: `build/logs/m4-flight-release-regression.log`.

Next: ammunition recovery and physical player contacts; finish ranged live acceptance,
then remaining combat per PLAN. Movement last.
