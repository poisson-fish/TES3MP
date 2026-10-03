# Current state and next action

**M4 in [PLAN.md](PLAN.md) is active.** [Host](../../apps/tes3mp-server/native/inventory_host.hpp)
runs 60 Hz NPC/door substeps and 30 Hz commits.

**V77 shared cell NPCs:** bootstrap's navigation argument emits `actors auto "NAV_SETTINGS"`.
Interiors isolate Bullet/navigation; exteriors share a neighborhood. Occupancy runs
physics/pursuit/melee/casting once; empty-cell NPC clocks freeze, scenery/flight
collision remains. Filtered replication/ownership survive unload/reentry.
Bounds: 1–37 NPCs including summons, one exterior 3x3, 256 cells.
Scripted/leveled actors and authored corpses reject; general AI packages remain M5.
Synthetic `build/logs/cell-actors-21.log`: occupancy, isolated collision, rollback,
restart/reload, destination combat/flight, empty/one-NPC domains.
Legacy: `cell-actors-collision-05.log`. Builds/export pass; desktop acceptance pending.

**Inherited V74–V76:** Target/ground collision, 143 passive selectors,
141 stock lifecycles, capability-34/35 presentation. `build/logs` evidence:
`target-recipients-08.log`, `source-limits-02.log`,
`spell-presentation-transaction-04.log`, `spell-passive-self-18.log`,
`spell-passive-stats-08.log`, `world-collision-09.log`.
[Desktop](../../apps/openmw/tes3mp/desktop_providers.cpp) shares stock helpers;
reconnect restores loops without transient replay.

**Restrictions:** AI powers reject; WhenUsed needs equipment. Passives apply Self;
travel needs players; Soultrap needs creatures. Two summons empty; animated ground
rejects. Bind intervention destinations. Scripts/quests remain M5.

**Chargen:** [native provider](../../apps/tes3mp-server/native/character_construction.cpp)
shares stock stats/F_PCStart and bounded race/body/class/sign discovery.
V78 fresh players have empty inventory and cannot move/simulate.
Completion stages bodies/inventory/stats/sources/resources/safe point before publication.
Recovery validates profiles/baselines/resources/custom classes. Stock `iAutoPCSpellMax=100` works.
Synthetic `build/logs/chargen-installer-15.log`: distinct bodies/custom class,
inventory/resources, missing meshes, magicka, atomic retry, unload/reentry,
identity/restart, malformed envelopes/failure closure.
`chargen-native-{construction,completion,discovery,transaction}.log`: synthetic
discovery/isolation/retry/profile restart (fake crypto).
`chargen-native-visibility.log`: synthetic projection retry/delivery (production crypto).
Also: `chargen-{construction,completion}-final.log`,
`chargen-v77-regression-01.log`, `chargen-final-build.log`,
`chargen-startup-restart.log` (fresh-profile smoke).
V78/capability 36 replicates established race/sex/head/hair with cell-filtered gear.
Replicas retain selected bases through gear/reentry; fresh characters hide.
`appearance-protocol.log`, `appearance-adapter.log`,
`appearance-installer.log` cover bounded/malformed wire, delivery order, stale/retry,
atomic completion, distinct bodies, filtering and restart (synthetic).
Desktop captures emit `player_appearance_sample`; `appearance-desktop-build.log` proves builds.
Seyda playtest stopped after completion consumed the pending scheduler tick.
Completion now preserves the simulation clock; `chargen-clock.log` passes both same-tick
completions/restart. `appearance-clock-adapter.log` passes delivery. Live acceptance pending.

**Death/respawn:** capability 37 adds life-bound Respawn to the death menu.
Requests reuse durable cleanup, restored stats/spawn, zero velocity and new generation;
ordinary inventory/history survive. Automatic deadlines remain. `build/logs/respawn-{protocol,adapter,native}.log`
cover bounds, duplicate/stale/offline/living requests, rollback/retry and restart;
`player-lives-regression.log` covers automatic respawn. Builds pass.
Death-menu visuals remain unverified.

**Inherited desktop acceptance:** V76/capability 35:
`build/v76-collision-acceptance-10/result.json`: 23 synthetic vanilla checks under
100ms delay, ±25ms jitter, 10% loss cover Target/intervention/ground/reconnect. V74:
`build/spell-v74-acceptance-23/result.json`; V73:
`build/combat-acceptance-16/result.json`.
Six [stock comparisons](../../scripts/native_spell_presentation_capture.py) verify
dispatch; pixels/waveforms/V77 remain unverified.

**Next:** prove two-client V78 completion/reconnect/restart and death-menu respawn,
with distinct rendered bodies and no fresh/template avatars.
V77 latency/jitter/loss acceptance still precedes player physics cutover.
