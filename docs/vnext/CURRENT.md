# Current state and next action

**M4 in [PLAN.md](PLAN.md) is active.**
Step 4 has shared [melee primitives](../../apps/openmw/mwmechanics/meleestate.hpp),
a [scheduler](../../apps/tes3mp-server/native/melee_animation.hpp) and
[scene-bound animation keys](../../apps/tes3mp-server/native/actor_scene.hpp).
V21 resolves a third-person KF source in stock priority and saves its identity
and swing snapshot in the actor tick. Rejection preserves the snapshot; restart
resumes it. Custom NPC models and random attacks remain unsupported. Attack
release, contact, damage and the encounter remain unwired.

[Host](../../apps/tes3mp-server/native/inventory_host.hpp): **native-inventory-21**,
fresh campaign, V20 fields plus `melee GROUP DIRECTION SPEED`. One dry interior or
nine adjacent exteriors; one unscripted nonleveled traveler, 0–128 doors,
8,192 collision references plus terrain, 256 nav tiles, 2,048 path points,
two 60 Hz substeps per tick. Actor/door/inventory commit together. Saturation
pauses both steps. Travel continues without players. Restart/unload retains
physics, destination and avoidance/RNG. Offline players freeze.

Background actors freeze. General AI, combat, scripts, water, automatic doors
and sliding neighborhoods are unavailable. Player movement is inherited.

Verified 2026-09-20 in `build/vnext-desktop-evidence`:

- `tes3mp_native_melee_tests melee-scheduling`:
  `build/logs/m4-melee-scheduling.log`; build:
  `build/logs/m4-melee-scheduling-build.log`. Synthetic timing,
  duplicate suppression and rejection.
- `tes3mp_native_melee_tests melee-context` and `melee-timing`, separately:
  `build/logs/m4-melee-context.log`, `build/logs/m4-melee-timing.log`;
  build: `build/logs/m4-melee-build.log`. Synthetic actor isolation, miss costs,
  wear/break values, reach boundaries, death time retention and text-key fallbacks.
- V20 `traveler-neighborhood`, V19 `npc-traveler`:
  `build/logs/m4-neighborhood.log`, `build/logs/m4-melee-v19-regression.log`.
  Synthetic crossings/restart fixture: `build/m4-neighborhood-verified`.
- Real TR Raflod, two desktops leaving/rejoining around unattended travel and
  server restart under latency/jitter/loss/reordering: `build/m4-v20-unattended-verified`,
  `build/logs/m4-v20-unattended.log`. Inherited V18: `build/m4-v18-resume-verified`.

Verified 2026-09-24: `tes3mp_native_actor_probe --melee` on real TR Raflod
resolved `meshes/xbase_anim.kf` and emitted one chop proposal:
`build/logs/m4-melee-resource-real.log`. Actor-probe and inventory-host builds:
`build/logs/m4-melee-resource-build.log`, `build/logs/m4-melee-resource-host-build.log`.
Probe.

Verified 2026-09-24: `melee-scheduling`:
`build/logs/m4-melee-durable-test.log` (synthetic codec/replay);
`npc-melee-campaign`: `build/logs/m4-melee-campaign-test.log`
(synthetic actor, KF, rejection/retry/restart). Builds:
`build/logs/m4-melee-durable-build.log`, `build/logs/m4-melee-campaign-build.log`.

Next: introduce the server-owned attack/release intent and validate a player
contact against current server positions at the bound hit key. Compose stats,
inventory, RNG, wear, death/life and corpse loot in the same durable tick;
then prove retries and two-client reconnect convergence.
Exterior graphical crossings remain unverified. Retained M3 campaigns:
`build/m3-tr-varyon-doors`, `build/m3-tr-noran-dry`.
