# Current state and next action

**M4 in [PLAN.md](PLAN.md) is active; M3 accepted 2026-09-19.**
V18 reuses OpenMW door avoidance and turning: the selected obstructing NPC retreats,
then replans to its retained destination. Rotation updates native navigation.
Avoidance/RNG, physics/path, doors and inventory commit together; rejection and
recovery preserve them. Player movement remains inherited.

[Host](../../apps/tes3mp-server/native/inventory_host.hpp): **native-inventory-18**,
fresh campaign, one dry interior, one living unscripted nonleveled NPC, fixed
speed/destination, navigation settings, 1–128 ordinary doors. V16/V17 retain their
domains. Background NPCs remain frozen; neighbor propagation, general AI, automatic
door activation, combat, scripts, water and gameplay animation remain unavailable.
Either player sustains simulation; both leaving freezes it. Headless packaging
remains unproved; computer-use remains disabled.

Verified 2026-09-20, `build/vnext-desktop-evidence`:

- `tes3mp_native_actor_tests door-avoidance`: `build/logs/m4-door-avoidance.log`.
- `tes3mp_native_loadout_tests npc-door-avoidance`: synthetic geometry on retained
  TR loadout; retreat/resumption, rotating navigation, rejection, RNG/recovery,
  inventory composition, reversal, disconnect/freeze. `build/logs/m4-npc-door-avoidance.log`.
- V17/V16 regressions: `build/logs/m4-avoidance-v17.log`, `build/logs/m4-avoidance-v16.log`.
- Two desktops, Hlavora in Vivec's Redoran Records: obstruction, retreat, opening,
  resumption and convergence under latency/jitter/loss/reordering. Screenshots and
  results: `build/m4-door-desktop-accepted`; reproduce with
  `scripts/run_native_navigation_capture.py --doors`. Real-content probe:
  `build/logs/m4-real-door-probe.log`, maximum 23.3 ms, no overruns (excludes durability).

Next: investigate impaired V18 delayed-resume failure
(`build/m4-door-desktop-Alice/Alice-user/openmw.log`); reconnect acceptance is pending.
Then M4 step 3: traveler activity after both players leave.
Retained M3 campaigns: `build/m3-tr-varyon-doors`, `build/m3-tr-noran-dry`.
