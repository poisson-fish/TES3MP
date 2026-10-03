# Current state and next action

**M4 in [PLAN.md](PLAN.md) is active.** [Host](../../apps/tes3mp-server/native/inventory_host.hpp)
runs 60 Hz NPC/door substeps and 30 Hz commits.

**V77 shared cell NPCs:** bootstrap's navigation argument emits `actors auto "NAV_SETTINGS"`.
Interiors isolate Bullet/navigation; exteriors share one neighborhood. Occupancy runs
physics/pursuit/melee/casting once per NPC. Empty-cell NPC clocks freeze; scenery/flight
collision remains. Cell-filtered replication and atomic ownership survive unload/reentry.
Bounds: 1–37 NPCs including summons, one exterior 3x3, 256 cells.
Scripted/leveled actors and authored corpses reject; general AI packages remain M5.
Synthetic `build/logs/cell-actors-21.log`: occupancy, isolated collision, rollback,
restart/reload, destination combat/flight, empty/one-NPC domains.
Legacy: `cell-actors-collision-05.log`. Server/bootstrap builds and V77 export pass;
two-desktop acceptance remains pending.

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
shares OpenMW stats/F_PCStart selection and discovers bounded winning playable
race/body/class/sign choices. Completion stages native candidates with the safe
point; durability precedes installation/publication. Unavailable preparation rejects.
Synthetic `build/logs/chargen-native-{construction,completion,discovery,transaction}.log`
covers construction, discovery overrides/budgets, two-player isolation,
cancellation/staleness/failure/retry and profile identity-file restart (fake crypto).
`chargen-native-visibility.log` (synthetic, production crypto) proves projection-failure
retry and staged inventory/combat delivery between ticks. Legacy profile/visibility checks pass.
Server build: `chargen-server-build.log`.
[Startup](../../apps/tes3mp-server/main.cpp) still requires established native characters.
Discovered-choice startup and concrete inventory/body/stat/spell/resource installation
remain unwired; desktop acceptance is unverified.

**Inherited desktop acceptance:** V76/capability 35:
`build/v76-collision-acceptance-10/result.json`: hashes and 23 synthetic vanilla
checks under 100ms delay, ±25ms jitter, 10% loss cover Target/sources, intervention,
ground collision and reload/reconnect. V74:
`build/spell-v74-acceptance-23/result.json`; V73:
`build/combat-acceptance-16/result.json`.
Six [stock comparisons](../../scripts/native_spell_presentation_capture.py) verify
dispatch; pixels/waveforms and V77 remain unverified.

**Next:** wire discovery and concrete native completion into the campaign hook,
enable fresh characters, then test two-client completion/reconnect/restart.
V77 latency/jitter/loss acceptance still precedes player physics cutover.
