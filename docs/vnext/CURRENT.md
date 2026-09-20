# Current state and next action

**M4 in [PLAN.md](PLAN.md) is active. M3 was accepted on 2026-09-19.**
M4 binds one content-derived NPC to a detached interior collision scene. Stock
movement/sweeps/stair stepping accept an explicit collision-effect owner; stock
callers retain effects, while the native scene journals bounded contacts. NPC model
selection and hull construction are shared. Player movement authority is unchanged;
computer-use remains disabled.

Next: reuse engine navigation, then compose actor physics with the native host's
authoritative actor/inventory state and durable tick installation. Prove smooth
two-client replication under latency/jitter/loss, continuing when either disconnects.
Live navigation and replication remain pending; follow DECISIONS.md's cutover rules.

Verified 2026-09-20 in `build/vnext-desktop-evidence`:

- `tes3mp_native_actor_tests` filters `collision-effects` and `movement-collision`
  passed separately: isolated effects, invalid/inactive projectile targets, hull
  selection, wall/actor obstruction, sliding and steps. Logs under `build/logs`:
  `m4-collision-build.log`, `m4-collision-effects.log`, `m4-collision-movement.log`.
  Earlier environment evidence: `m4-movement-environment.log`.
- `tes3mp_native_actor_probe`: Raflod the Braggart in **Seyda Neen, Arrille's
  Tradehouse**, using the retained Morrowind/Tribunal/Bloodmoon/Tamriel_Data/TR_Mainland
  loadout. 206 collision bodies, 900 movement steps, 888 grounded, seven contacted
  objects; invalid velocity left the snapshot unchanged. Logs:
  `build/logs/m4-interior-build.log`, `build/logs/m4-interior-npc.log`;
  the latter records placement and content/model fingerprints.

`tes3mp_native_actor_runtime` remains a frozen dry-interior physics diagnostic:
no AI, gameplay animation, live authority or durability. It constructs no
Environment/World/player/rendering services. Only the selected living unscripted
NPC moves. Water, corpses, non-NPC/unresolved leveled actors, projectiles and animated
object collision are unsupported. Inventories/scripts do not execute; production
headless packaging remains unproved.

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
