# Current state and next action

**M4 in [PLAN.md](PLAN.md) is active. M3 was accepted on 2026-09-19.**
M4 groundwork: stock OpenMW movement now accepts explicit actor/weather frames;
physics workers no longer read the global store for storm movement.
The stock caller supplies the same actor inputs and captures the winning GMST
before scheduling. Player movement authority is unchanged. Computer-use tools
remain disabled by request.

Next: isolate stock collision side effects and bind a content-derived interior
collision scene/actor in the app-local runtime. Then reuse engine navigation for
the first server-controlled NPC proof: smooth two-client replication under
latency/jitter/loss, continuing when either disconnects. Follow DECISIONS.md's
movement cutover requirements.

Verified 2026-09-20: `tes3mp_native_actor_tests` built in
`build/vnext-desktop-evidence`; filters `movement-collision` and
`movement-environment` passed separately. Synthetic geometry exercises stock
wall/actor obstruction, sliding, steps, gravity, slow fall and storm settings
without constructing Environment/World/player/rendering. Logs:
`build/logs/m4-physics-build.log`, `build/logs/m4-movement-collision.log`,
`build/logs/m4-movement-environment.log`.
This is a physics-input seam, not native NPC AI, navigation, transactional physics
or live replication. Stock object/projectile collision callbacks still have side
effects; the probe's broad engine link does not prove production headless packaging.

[Host](../../apps/tes3mp-server/native/inventory_host.hpp): **native-inventory-15**,
campaign-seeded initial living actors; selections/none persist without rerolling.
AI/combat/respawn, leveled corpses, scripts/Lua and locked/trapped inventories
remain unsupported. Client AI/local leveled spawning remain suppressed.
Streaming retains occupied interiors/player 3×3 exteriors; unloaded doors freeze,
weather continues, inventories persist.

Inherited M3 acceptance: Varyon doors/restoration and Noran travel/weather/reconnect;
final door latency accepted without a new timing capture. Split/unload/reentry
evidence is synthetic: `build/logs/m3-area-crossings-02.log`.
Retained setups provide `launch.ps1 -Role server|Alice|Bob [-Evidence]`:

- `build/m3-tr-varyon-doors`, port 25617, stopped at handoff; campaign retained.
- `build/m3-tr-noran-dry`, port 25616, server/two clients running at prior handoff;
  land spawn `(310272,-240512,200)`, exterior `(37,-30)`. Original
  `build/m3-tr-noran` retained/stopped.

Loadout/recovery: `build/logs/m3-v15-acceptance.json`. Reconnect evidence:
`build/logs/m3-tr-noran-dry/weather-reconnect.json`,
`build/logs/m3-tr-varyon-doors/reconnect-20260919.json`.
