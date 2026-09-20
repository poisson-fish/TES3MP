# Current state and next action

**M3 is accepted and closed (2026-09-19); M4 in [PLAN.md](PLAN.md) is active.**
Next: prove one server-controlled NPC navigating/colliding in one interior,
replicated smoothly to two clients and continuing when either disconnects.
Reuse OpenMW movement, navigation and collision; retain current player movement
until DECISIONS.md's prediction/reconciliation cutover requirements are met.
Computer-use tools are disabled by request.

M3 acceptance: Varyon door motion, edge cases and restored state; Noran travel,
matching weather and consistency after Bob's reconnect were visually confirmed.
The user accepted the final door-latency check as passed; no new timing capture
was produced. Fresh process rejoins exercise authenticated late-join baselines.
Detailed split/unload/reentry coverage is synthetic, not individually confirmed live:
`build/logs/m3-area-crossings-02.log`.

Client AI/local leveled spawning remain suppressed. Launchers await server readiness.

`openmw_tes3mp_adapter_tests native-door-presentation` passes;
`teleport-presentation` and `inventory-pickup-barrier` pass. Logs in `build/logs`:
`m3-door-presentation-{before,fixed}.log`, `m3-door-{teleport,pickup}-regression.log`.
Ninja log reset may trigger dependency rebuilds.

[Host](../../apps/tes3mp-server/native/inventory_host.hpp): **native-inventory-15**,
fresh campaigns, campaign-seeded initial living actors; selections/none persist without
rerolling. AI/combat/respawn, leveled corpses, scripts/Lua and locked/trapped inventories
remain unsupported. Streaming: 1–256 cells, occupied interiors and both players' 3×3
exteriors; unloaded doors freeze, weather continues, inventory persists.

Setups use level 1/seed 0, exclude mod Lua and provide
`launch.ps1 -Role server|Alice|Bob [-Evidence]`:

- `build/m3-tr-varyon-doors`, port 25617, stopped: confirmed doors; campaign retained.
- `build/m3-tr-noran-dry`, port 25616: server/two clients running. Land spawn
  `(310272,-240512,200)` in exterior `(37,-30)` replaces the underwater grotto start.
  Original `build/m3-tr-noran` campaign retained, stopped. Same interior/nine exteriors,
  Nedothril/Padomaic regions. Test-only weather selection: one game hour (~2 real minutes),
  stock transition rates. Alice's log reached 2,048 events.
  Earlier 24 matching weather images show completed
  Nedothril/Padomaic transitions; Bob's rejoin also records completion. Evidence:
  `build/logs/m3-tr-noran-dry/{setup,live-start}.json`; terrain read through OpenMW in
  `build/logs/m3-noran-dry-spawn.log`.

Loadout hashes and setup/recovery evidence:
`build/logs/m3-v15-acceptance.json`. Warehouse previously passed transfers/reconnect/restart
at 164–166 ms; that does not measure the fixed door path.
Reconnect: `build/logs/m3-tr-noran-dry/weather-reconnect.json` and
`build/logs/m3-tr-varyon-doors/reconnect-20260919.json`. Immediate Varyon rejoin
initially failed; retry after 36 seconds passed.
