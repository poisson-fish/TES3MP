# Current state and next action

**M4 in [PLAN.md](PLAN.md) is active.** The
[host](../../apps/tes3mp-server/native/inventory_host.hpp) simulates one native NPC
and doors at 60 Hz, committing at 30 Hz. Neighbors freeze; players use contact
proxies until movement cutover.

V42 persists autonomous selection/casting/flight/recovery with RNG, resources and
effects. Weapons/ammunition, spells and equipped WhenUsed compete. Bound KF keys,
visibility and source/life checks govern release; inactivity pauses execution.
Two players can cast alongside the NPC. Effects include timed Fortify Attribute/Skill,
mixed ranges and areas. T3C3 casting snapshots require capability 21.

Verified narrow evidence:

- `npc-cast-lifecycle`: durability, restart, concurrency, interruption and effects
  (`build/logs/m4-cast-lifecycle-final.log`).
- Combat wire/handshake: `build/logs/m4-cast-protocol-test-02.log` and
  `build/logs/m4-cast-handshake-test.log`.
- Four two-desktop encounters passed concurrent casting, target switching,
  reconnect and process restart during wind-up, with 10% loss/100 ms delay/jitter:
  `build/m4-cast-vanilla-live-10/result.json` (`Fireball_large`) and
  `build/m4-cast-tr-live-02/result.json` (`T_Ayl_Des_DWelkynd_FIR`), plus equipped
  `cruel flamebolt ring` in `build/m4-cast-vanilla-item-live-03/result.json` and
  `T_De_Ep_Ring_Chill` in `build/m4-cast-tr-item-live-04/result.json` (both wind-up images).
  Gameplay records are unchanged; actor stats/room placements are synthetic.
  TR Lua packages are disabled; native Lua services remain M5 work.

V43 binds each participant's layered hit resources and timer bounds into the
campaign fingerprint. `tes3mp_native_melee_tests hit-resources` passed
(`build/logs/m4-hit-resources-unit.log`); `npc-hit-resources` covers female/Argonian/NPC
hits, armor, durability and recovery (`build/logs/m4-hit-resources-host-final.log`).

V34 retains melee defense and knockout; get-up timing remains pending.
V37 supports multi-slot constant fortification/resistance;
other constant effects reject.

V44 equips carried melee weapons and repeats NPC swings. Layered weapon groups,
stock fallback and record speed drive timing; the descriptor chooses one direction.
Targets persist before release. Interrupted/disconnected/occluded swings cancel;
inactivity pauses. Breakage removes passive effects atomically. V43/V44 require
fresh campaigns; recovery never re-equips.
`npc-weapon-execution` passed carried sword selection, breakage, spear replacement,
three attacks, passive equip/break, rejected-write replay, wind-up/follow/equipment
restart, malformed source rejection, offline pause, disconnect and uncertain-write
closure/recovery (`build/logs/m4-weapon-execution-test.log`). V43/V44 evidence is
synthetic/headless with vanilla KF resources. The loadout-test target built
(`build/logs/m4-weapon-build.log`); regressions passed: `npc-hit-resources`
(`build/logs/m4-weapon-hit-regression.log`) and `equipment-slots`
(`build/logs/m4-weapon-equipment-regression.log`).

Next: generalize attack-mode selection and bind player swing hit/release keys,
then ranged/ammunition/thrown flight and remaining close combat. Complete
physical/casting composition, durable death/loot and varied vanilla/TR two-desktop
encounters as specified in PLAN. Player movement remains last.
