# Current state and next action

**M4 in [PLAN.md](PLAN.md) is active.** The
[host](../../apps/tes3mp-server/native/inventory_host.hpp) simulates one native NPC
and doors at 60 Hz, committing at 30 Hz. Neighbors freeze; players use contact
proxies until movement cutover.

V42 persists casting/flight/recovery, RNG, resources and effects through bound KF
keys. Two players cast alongside the NPC. T3C3 requires capability 21.

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

V43 binds participant hit resources/timers to the campaign fingerprint.
`hit-resources` and `npc-hit-resources` passed female/Argonian/NPC recovery:
`build/logs/m4-hit-resources-unit.log`, `build/logs/m4-hit-resources-host-final.log`.

V34 retains melee defense and knockout; get-up timing remains pending.
V37 supports multi-slot constant fortification/resistance;
other constant effects reject.

V44 equips carried melee weapons and repeats bound swings; breakage removes
passive effects atomically. Recovery never re-equips. V45 adds stock weighted
chop/slash/thrust selection (uniform unarmed), persisting rolls and clips.
Fresh campaigns required. RNG parity, all directions, restart, weapon replacement
and durability passed: `build/logs/m4-attack-modes-unit.log`,
`build/logs/m4-attack-modes-host.log`; V44 regression:
`build/logs/m4-attack-modes-v44.log`. V43–V45 evidence is synthetic/headless.

V46 schedules both players through bound wind-up, release, KF hit and follow-through.
Intents request direction/strength; the server advances to release. Weapon
instance/record, target life, phase and interruption persist.
Source changes, active disconnect, target loss and hit recovery cancel; capacity
and inactivity pause. Simultaneous hits share outcomes and accumulated armor wear.
Fresh campaigns required; older domains keep immediate player hits. Player swing
presentation remains inherited, without committed phase replication.

`tes3mp_native_loadout_tests player-swings` passed simultaneous armored hits,
restart, durability, interruption, pause, malformed saves, contact and life checks
(`build/logs/m4-player-swings-final.log`; synthetic/headless, vanilla resources).
Regressions: `build/logs/m4-player-swings-v45.log`
(`npc-attack-modes`) and `build/logs/m4-player-swings-defense.log`
(`npc-armor-block`).

Next: committed player-swing presentation, then ranged/ammunition/thrown flight,
close-combat completion, physical/casting composition and vanilla/TR two-desktop
encounters with death/loot as specified in PLAN. Player movement remains last.
