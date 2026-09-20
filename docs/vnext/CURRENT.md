# Current state and next action

M3 in [PLAN.md](PLAN.md) remains open. **Varyon Ancestral Tomb door motion is
visually confirmed by the user (2026-09-19)** on the rebuilt clients. Alice/Bob
recorded 271/273 door samples with matching committed/rendered angles and final state.
Next: obstruction/reversal, latency, late join/reconnect, then Noran travel/weather.
Computer-use tools are disabled by request.

Door presentation now consumes committed ground images independently of
inventory/equipment/movement revision alignment, preserving session/cell/transition
and resync guards. No protocol/save change. Client AI and local leveled spawning
are suppressed from connection start through failure. Launchers wait for the listener;
the first client previously timed out before server readiness. Live Varyon samples:
health 35, AI inactive, alive, ground matching; the checker excludes fixed lights.

`openmw_tes3mp_adapter_tests native-door-presentation` failed before and passes after;
`teleport-presentation` and `inventory-pickup-barrier` pass. Logs in `build/logs`:
`m3-door-presentation-{before,fixed}.log`, `m3-door-{teleport,pickup}-regression.log`.
Build `m3-door-client-build.log` and registry `m3-door-patch-registry.log` passed.
`build/vnext-product/openmw.exe` matches `openmw-m3-doors.exe`; isolated compile/link
edges reused existing libraries. Regular builds may rebuild dependencies after the
older Ninja log reset.

[Host](../../apps/tes3mp-server/native/inventory_host.hpp): **native-inventory-15**,
fresh campaigns, campaign-seeded initial living actors; selections/none persist without
rerolling. AI/combat/respawn, leveled corpses, scripts/Lua and locked/trapped inventories
remain unsupported. Streaming: 1–256 cells, occupied interiors and both players' 3×3
exteriors; unloaded doors freeze, weather continues, inventory persists.

Both setups use level 1/seed 0, exclude mod Lua and provide
`launch.ps1 -Role server|Alice|Bob [-Evidence]`:

- `build/m3-tr-varyon-doors`, port 25617: two ordinary doors, travel disabled.
  Server retained its campaign; fixed clients launched. `Alice.evidence-path` and
  `Bob.evidence-path` locate timestamped committed/rendered door-angle logs.
- `build/m3-tr-noran`, port 25616, stopped: one interior/nine exteriors,
  Nedothril/Padomaic regions; no swinging doors. Crossings, teleports, separation,
  unload/return and weather still need acceptance.

Ordered loadout hashes, setup/recovery and inherited narrow evidence:
`build/logs/m3-v15-acceptance.json`. Warehouse previously passed transfers/reconnect/restart
at 164–166 ms. Diagnostic port-25618 processes stopped. Doors/weather must converge
after late join/reconnect before M3 closes.
