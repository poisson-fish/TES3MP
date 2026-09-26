# Current state and next action

**M4 in [PLAN.md](PLAN.md) is active.** The
[host](../../apps/tes3mp-server/native/inventory_host.hpp) simulates one native NPC
and doors at 60 Hz, committing at 30 Hz. Neighbors freeze; players use contact
proxies until movement cutover.

V42 persists selection/casting/flight/recovery, RNG, resources and effects.
Weapons/ammunition, spells and equipped WhenUsed compete. Bound KF keys and
source/life/visibility checks govern release. Two players cast alongside the NPC.
T3C3 snapshots require capability 21.

Verified evidence:

- `npc-cast-lifecycle`: durability, restart, concurrency, interruption and effects
  (`build/logs/m4-cast-lifecycle-final.log`).
- Combat wire/handshake: `build/logs/m4-cast-protocol-test-02.log` and
  `build/logs/m4-cast-handshake-test.log`.
- Two-desktop casting passed target switching, reconnect and wind-up restart
  under 10% loss/100 ms delay/jitter:
  `build/m4-cast-vanilla-live-10/result.json` (`Fireball_large`) and
  `build/m4-cast-tr-live-02/result.json` (`T_Ayl_Des_DWelkynd_FIR`), plus equipped
  `cruel flamebolt ring` in `build/m4-cast-vanilla-item-live-03/result.json` and
  `T_De_Ep_Ring_Chill` in `build/m4-cast-tr-item-live-04/result.json` (both wind-up images).
  Gameplay records unchanged; actor stats/placements synthetic.
  TR Lua packages are disabled; native Lua services remain M5 work.

V43 binds each participant's layered hit resources and timer bounds into the
campaign fingerprint. `tes3mp_native_melee_tests hit-resources` passed
(`build/logs/m4-hit-resources-unit.log`); `npc-hit-resources` covers female/Argonian/NPC
hits, armor, durability and recovery (`build/logs/m4-hit-resources-host-final.log`).

V34 retains melee defense and knockout; get-up timing remains pending.
V37 supports multi-slot constant fortification/resistance;
other constant effects reject.

V44 equips carried melee weapons and repeats swings using layered groups, stock
fallback and record speed. Targets persist before release; interruption,
disconnect or occlusion cancels. Inactivity pauses. Breakage removes passive
effects atomically; recovery never re-equips. V43/V44 require fresh campaigns.
`npc-weapon-execution` passed (`build/logs/m4-weapon-execution-test.log`), plus
`npc-hit-resources` (`build/logs/m4-weapon-hit-regression.log`) and `equipment-slots`
(`build/logs/m4-weapon-equipment-regression.log`).

V45 selects NPC chop/slash/thrust using shared stock weighted RNG (uniform unarmed).
The roll and clip persist without recovery rerolls; V44 retains fixed direction.
Fresh campaigns required. `attack-modes` passed RNG parity and clip/restart checks
(`build/logs/m4-attack-modes-unit.log`). `npc-attack-modes` passed five attacks across
all modes, directional restart, break/replacement, rejected-write replay, pause,
disconnect and uncertain-write recovery (`build/logs/m4-attack-modes-host.log`).
V44 regression passed (`build/logs/m4-attack-modes-v44.log`); both test targets built.
V43–V45 evidence remains synthetic/headless with vanilla KF resources.

Next: durable player swing scheduling at bound hit/release keys; player hits still
resolve immediately. Then ranged/ammunition/thrown flight, close combat,
physical/casting composition, death/loot and vanilla/TR two-desktop encounters
as specified in PLAN. Player movement remains last.
