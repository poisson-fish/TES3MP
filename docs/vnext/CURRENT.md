# Current state and next action

**M4 in [PLAN.md](PLAN.md) is active; M3 accepted 2026-09-19.**
Step 4 has shared [melee primitives](../../apps/openmw/mwmechanics/meleestate.hpp)
and a detached [directional scheduler](../../apps/tes3mp-server/native/melee_animation.hpp),
not a native encounter. Stock callers share melee resources, simulation-time death,
section lookup, wind-up, release skipping, follow-through and hit-key interpretation.
Directional clips emit one hit proposal; random attacks are unsupported. Resource
binding and host integration remain unwired; no combat capability or campaign-format change.

[Host](../../apps/tes3mp-server/native/inventory_host.hpp): **native-inventory-20**,
fresh campaign, V19 fields plus `processing CELLS STEPS`. One dry interior or nine
exteriors in the first cell's 3×3 neighborhood; one living unscripted nonleveled
traveler, 0–128 ordinary doors, 8,192 collision references plus terrain,
256 nav tiles, 2,048 path points, two 60 Hz substeps per 30 Hz tick.
Actor/door/inventory state commits together. Saturation pauses both substeps;
unavailable routes retain destinations. Travel continues without players;
completion releases scenes. Restart/unload retains physics, destination and
avoidance/RNG. Origin/destination appearances coalesce; offline players freeze.

Background actors freeze. General AI, combat, scripts, water, automatic door
activation, sliding neighborhoods and gameplay animation remain unavailable.
Player movement is inherited; headless packaging is unproved.

Verified 2026-09-20 in `build/vnext-desktop-evidence`:

- `tes3mp_native_melee_tests melee-scheduling`:
  `build/logs/m4-melee-scheduling.log`; build:
  `build/logs/m4-melee-scheduling-build.log`. Synthetic early/held release,
  speed, hit boundaries, duplicate suppression, follow-through, discarded-copy
  determinism and input rejection. No durable receipts or live encounter.
- `tes3mp_native_melee_tests melee-context` and `melee-timing`, separately:
  `build/logs/m4-melee-context.log`, `build/logs/m4-melee-timing.log`;
  build: `build/logs/m4-melee-build.log`. Synthetic actor isolation, miss costs,
  wear/break values, reach boundaries, death time retention and text-key fallbacks.
- V20 `traveler-neighborhood`, V19 `npc-traveler`:
  `build/logs/m4-neighborhood.log`, `build/logs/m4-neighborhood-v19.log`.
  Synthetic crossings/restart fixture: `build/m4-neighborhood-verified`.
- Real TR Raflod, two desktops leaving/rejoining around unattended travel and
  server restart under latency/jitter/loss/reordering: `build/m4-v20-unattended-verified`,
  `build/logs/m4-v20-unattended.log`. Inherited V18: `build/m4-v18-resume-verified`.

Next: bind gameplay animation resources and compose the scheduler, validated player
contacts, stats/inventory, RNG, attributed death/life and corpse loot in one durable
tick. Prove durable retries and two-client reconnect convergence.
Exterior graphical crossings remain unverified. Retained M3 campaigns:
`build/m3-tr-varyon-doors`, `build/m3-tr-noran-dry`.
