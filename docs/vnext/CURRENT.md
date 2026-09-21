# Current state and next action

**M4 in [PLAN.md](PLAN.md) is active; M3 accepted 2026-09-19.**
V19 sustains one traveler after both players leave. Player/traveler demand
runs one composed actor/door/inventory tick, independently of replication interest.
Completion releases an unoccupied collision/navigation scene. Unload/reload and
restart preserve destination, completion, physics and avoidance/RNG; mismatches
and retired-scene candidates reject atomically.

[Host](../../apps/tes3mp-server/native/inventory_host.hpp): **native-inventory-19**,
fresh campaign, V18 fields: one dry interior, one living unscripted nonleveled NPC,
fixed speed/destination, navigation settings, 1–128 ordinary doors. Bounded interior
processing. V16–V18 retain their domains and freeze when both players leave.

Background NPCs remain frozen. Multiple travelers, cell crossings, general AI,
neighbor propagation, automatic door activation, combat, scripts, water and gameplay
animation remain unavailable. Player movement is inherited; headless packaging
unproved; computer-use disabled.

Verified 2026-09-20, `build/vnext-desktop-evidence`:

- `openmw_tes3mp_adapter_tests native-capabilities`: `build/logs/m4-native-capabilities.log`.
  Reconnect omitted actor-motion/leveled-actor capabilities; join/resume now share
  one offer. Two V18 desktops resumed under latency/jitter/loss/reordering:
  identical final NPC positions, converged doors, preserved inventories/identities.
  Results/screenshots: `build/m4-v18-resume-verified`; log: `build/logs/m4-v18-resume.log`.
  Reproduce: `scripts/run_native_navigation_capture.py --doors`.
- `tes3mp_native_loadout_tests npc-traveler`: `build/logs/m4-npc-traveler.log`.
  Synthetic room on retained TR loadout: empty/occupied simulation equality,
  completion at tick 62, mid-travel restart, unload/reload, completed restart,
  range/destination/stale-scene rejection and composed durability failures.
  Fixture: `build/m4-traveler-fixture-verified`.
- V18 `npc-door-avoidance` / V16 `native-navigation`:
  `build/logs/m4-traveler-v18.log`, `build/logs/m4-traveler-v16.log`.
  Server built: `build/logs/m4-traveler-server-build.log`.

Next: M4 step 3 processing neighborhoods, cell-boundary transitions and saturation
diagnostics; then real-content unattended travel/restart acceptance. Preserve
one simulation and durable destinations/completion independently of client interest.
Retained M3 campaigns: `build/m3-tr-varyon-doors`, `build/m3-tr-noran-dry`.
