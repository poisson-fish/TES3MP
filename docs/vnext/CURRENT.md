# Current state and next action

**M4 in [PLAN.md](PLAN.md) is active; M3 accepted 2026-09-19.**
V20 sustains one traveler after both players leave. One composed actor/door/inventory
tick crosses exterior edges through stock collision/navigation. Saturation pauses
both substeps; unavailable routes retain destinations. Completion releases the scene.
Unload/restart preserves destination, completion, physics and avoidance/RNG.

[Host](../../apps/tes3mp-server/native/inventory_host.hpp): **native-inventory-20**,
fresh campaign, V19 fields plus `processing CELLS STEPS`: one dry interior or nine
exterior cells maximum in the first cell's 3×3 neighborhood. That cell owns one
living unscripted nonleveled NPC with fixed speed/destination; 0–128 ordinary doors.
Limits: 8,192 collision references plus terrain, 256 nav tiles, 2,048 path points,
two 60 Hz substeps per 30 Hz tick. Limits may increase on restart without resetting
travel. Origin/destination appearances coalesce. Offline players freeze.
V16–V19 retain their domains; V16–V18 freeze without players.

Background NPCs remain frozen. Sliding neighborhoods, beyond-domain travel,
interior/exterior teleports, multiple travelers, general AI, neighbor propagation,
automatic door activation, combat, scripts, water and gameplay animation remain
unavailable. Player movement is inherited; headless packaging is unproved.

Verified 2026-09-20, `build/vnext-desktop-evidence`:

- `tes3mp_native_loadout_tests traveler-neighborhood`: `build/logs/m4-neighborhood.log`.
  Synthetic three-cell terrain: x=0 crossing, completion tick 74, occupied/empty
  equality, cell/step saturation, pending-route recovery, rejected crossing,
  exact restart/unload/reload, coalesced projections and offline falling-player guard.
  Fixture: `build/m4-neighborhood-verified`.
- `tes3mp_native_loadout_tests npc-traveler`: V19 regression passed,
  `build/logs/m4-neighborhood-v19.log`. Targets built: `tes3mp_server` and
  `tes3mp_native_loadout_tests`, `build/logs/m4-neighborhood-build.log`.
- `scripts/run_native_navigation_capture.py --traveler`: real Raflod in Arrille's
  Tradehouse on the retained TR loadout. Both desktops left at tick 239; server
  restarted after tick 330, continued unattended, completed by tick 1230. Returning
  clients converged to (−542.55, 67.7852, 385), identities preserved, local AI off,
  under latency/jitter/10% loss/reordering. Results and inspected screenshots:
  `build/m4-v20-unattended-verified`; log: `build/logs/m4-v20-unattended.log`.
- Inherited V18 door/resume evidence: `build/m4-v18-resume-verified`.

Next: M4 step 4's first composed melee encounter. Exterior graphical crossings remain unverified;
the exterior proof above uses synthetic terrain and projected client snapshots.
Retained M3 campaigns: `build/m3-tr-varyon-doors`, `build/m3-tr-noran-dry`.
