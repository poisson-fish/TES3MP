# Current state and next action

**M4 in [PLAN.md](PLAN.md) is active.**
Step 4 has shared [melee primitives](../../apps/openmw/mwmechanics/meleestate.hpp),
a [scheduler](../../apps/tes3mp-server/native/melee_animation.hpp) and
[KF keys](../../apps/tes3mp-server/native/actor_scene.hpp). V22 saves a
server-selected target, release and hit-key contact with the actor tick.
It rechecks current server positions at the key. Rejection and restart retain
the result. Player hulls, damage, wear, death, loot, custom NPC models and
random attacks remain unwired.

[Host](../../apps/tes3mp-server/native/inventory_host.hpp): **native-inventory-22**,
fresh campaign, V21 descriptor with `melee GROUP DIRECTION SPEED`. One dry interior or
nine adjacent exteriors; one unscripted nonleveled traveler, 0–128 doors,
8,192 collision references plus terrain, 256 nav tiles, 2,048 path points,
two 60 Hz substeps per tick. Actor/door/inventory commit together. Saturation
pauses both steps. Travel continues without players. Restart/unload retains
physics, destination and avoidance/RNG. Offline players freeze.

Background actors freeze. General AI, combat, scripts, water, automatic doors
and sliding neighborhoods are unavailable. Player movement is inherited.

Inherited 2026-09-20 evidence: synthetic melee timing/context
(`build/logs/m4-melee-scheduling.log`, `build/logs/m4-melee-context.log`),
V20 crossings (`build/logs/m4-neighborhood.log`) and two-client unattended TR
Raflod travel (`build/logs/m4-v20-unattended.log`).

Verified 2026-09-24: `tes3mp_native_actor_probe --melee` on real TR Raflod
resolved `meshes/xbase_anim.kf` and emitted one chop proposal:
`build/logs/m4-melee-resource-real.log`. Actor-probe and inventory-host builds:
`build/logs/m4-melee-resource-build.log`, `build/logs/m4-melee-resource-host-build.log`.
Probe.

V21 codec/replay and restart fixtures:
`build/logs/m4-melee-durable-test.log`, `build/logs/m4-melee-campaign-test.log`.

Verified 2026-09-24: `npc-melee-campaign` on a synthetic actor and real KF:
`build/logs/m4-melee-contact-test.log`; target build:
`build/logs/m4-melee-contact-build.log`. The fixture exercises a committed
contact, rejected/retried hit ticks, restart equality and an out-of-reach miss.

Next: compose OpenMW stats, inventory, RNG, wear, death/life and corpse loot in
the same durable native tick as the V22 hit. Then prove duplicate requests,
restart and two-client reconnect convergence. V22 has no damage or player-facing
combat event yet.
Exterior graphical crossings remain unverified. Retained M3 campaigns:
`build/m3-tr-varyon-doors`, `build/m3-tr-noran-dry`.
