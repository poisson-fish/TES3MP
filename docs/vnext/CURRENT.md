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

V34 retains defense/knockout; get-up timing remains pending. V37 supports constant
fortification/resistance only. V43 binds participant hit resources. V44-V45 equip
carried melee weapons, repeat stock directions and remove broken passive sources.
Recovery never re-equips; evidence is headless.

V46 persists player swings/identities. Source/target loss, active disconnect and
hit recovery cancel; inactivity/capacity pause. Simultaneous hits share wear.
T3C4 projects committed sections without gameplay callbacks.

Two desktops passed vanilla `iron longsword` directions/strengths, interruption,
reconnect/restart and duplicate outcomes under 10% loss/100 ms delay/jitter:
`build/m4-swing-vanilla-live-07/result.json`; `build/logs/m4-swing-live-test.log`.
Synthetic actors/placements isolate presentation, not sustained mixed combat.

V49 adds 60 Hz player flight using shared stock speed/gravity/damage and
NPC/world/door collision. Velocity, age, launch condition, terminal outcome, damage,
wear and RNG persist atomically. Flight survives disconnect/restart. Fresh V49
campaigns required; V47/V48 retain frozen launches.

Headless `tes3mp_native_loadout_tests` filters `bow-flight`, `crossbow-flight`,
`thrown-flight` passed concurrent releases, NPC/geometry impacts, rejected/uncertain
writes, wind-up/flight/terminal restart, malformed recovery, disconnect and exhaustion:
`build/logs/m4-bow-flight-test.log`, `build/logs/m4-crossbow-flight-test.log`,
`build/logs/m4-thrown-flight-test.log`.
Fixtures use unchanged vanilla weapon/KF records with synthetic actors, increased
marksman skill and placements. `tes3mp_native_melee_tests projectile-mechanics`
passed: `build/logs/m4-projectile-mechanics-test.log`.

Plain player sources only, against one NPC/world scene using a body-center launch
proxy. Eight records, including terminal launches, remain the campaign limit.
NPC ranged execution, player contacts, record recycling, ammunition recovery,
desktop input/rendering and mixed vanilla/TR live acceptance remain pending.
**Two-client ranged acceptance is not met.**
V48 `thrown-release` regression passed: `build/logs/m4-flight-release-regression.log`.

Next: terminal recycling with durable retry protection, then remaining combat per PLAN. Movement last.
