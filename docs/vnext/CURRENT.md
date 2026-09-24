# Current state and next action

**M4 in [PLAN.md](PLAN.md) is active; M3 accepted 2026-09-19.**
Step 4 has shared [melee primitives](../../apps/openmw/mwmechanics/meleestate.hpp),
a detached [scheduler](../../apps/tes3mp-server/native/melee_animation.hpp)
and [scene-bound animation keys](../../apps/tes3mp-server/native/actor_scene.hpp).
Stock callers share costs, death timing and directional key interpretation.
The scene resolves third-person KF sources in stock priority and returns one hit
proposal with resource identity. Custom NPC models and random attacks remain
unsupported. Host combat and campaign identity are unwired; no encounter yet.

[Host](../../apps/tes3mp-server/native/inventory_host.hpp): **native-inventory-20**,
fresh campaign, V19 fields plus `processing CELLS STEPS`. One dry interior or
nine adjacent exteriors; one unscripted nonleveled traveler, 0–128 doors,
8,192 collision references plus terrain, 256 nav tiles, 2,048 path points,
two 60 Hz substeps per tick. Actor/door/inventory commit together. Saturation
pauses both steps; unavailable routes retain destinations. Travel continues
without players. Restart/unload retains physics, destination and avoidance/RNG.
Appearances coalesce; offline players freeze.

Background actors freeze. General AI, combat, scripts, water, automatic doors,
sliding neighborhoods and gameplay animation are unavailable. Player movement
is inherited; headless packaging is unproved.

Verified 2026-09-20 in `build/vnext-desktop-evidence`:

- `tes3mp_native_melee_tests melee-scheduling`:
  `build/logs/m4-melee-scheduling.log`; build:
  `build/logs/m4-melee-scheduling-build.log`. Synthetic release timing,
  duplicate suppression, discarded-copy determinism and input rejection.
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

Verified 2026-09-24: `tes3mp_native_actor_probe --melee` on real TR Raflod
resolved `meshes/xbase_anim.kf` and emitted one chop proposal:
`build/logs/m4-melee-resource-real.log`. Actor-probe and inventory-host builds:
`build/logs/m4-melee-resource-build.log`, `build/logs/m4-melee-resource-host-build.log`.
This is a read-only probe.

Next: bind animation identity to a combat campaign; compose scheduler, validated
player contacts, stats/inventory, RNG, attributed death/life and corpse loot in
one durable tick. Prove retries and two-client reconnect convergence.
Exterior graphical crossings remain unverified. Retained M3 campaigns:
`build/m3-tr-varyon-doors`, `build/m3-tr-noran-dry`.
