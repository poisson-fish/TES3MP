# Current state and next action

**M4 in [PLAN.md](PLAN.md) is active.** [Host](../../apps/tes3mp-server/native/inventory_host.hpp):
one NPC/doors at 60 Hz, commits at 30 Hz; neighbors freeze. T3C5 requires capability 23.

Inherited two-desktop evidence under 10% loss/100 ms delay/jitter:
- Vanilla/TR casting: `build/m4-cast-vanilla-live-10/result.json`,
  `build/m4-cast-tr-live-02/result.json`, `build/m4-cast-vanilla-item-live-03/result.json`,
  `build/m4-cast-tr-item-live-04/result.json`.
- Vanilla directional swings/interruption/reconnect/restart:
  `build/m4-swing-vanilla-live-07/result.json`.
Records unchanged; actors/placements synthetic. TR Lua awaits M5.

V49 persists flight/collision/resources/RNG and recycles receipts; inherited V50
`bow-flight` regression: `build/logs/m4-v50-bow-regression.log`.

V50 persists participant knockout/get-up clocks and stock knockdown rolls. Fresh
campaign required. Timed/constant Shield, Sanctuary, Blind and Fortify Attack feed combat.

Inherited headless checks:
- `tes3mp_native_melee_tests knockout-timing`, `hit-knockdown`:
  `build/logs/m4-knockout-timing-test.log`, `build/logs/m4-hit-knockdown-test.log`.
- `tes3mp_native_ai_magic_tests combat-modifiers`: `build/logs/m4-combat-modifiers-test.log`.
- `bow-combined-flight` (vanilla), `crossbow-combined-flight`/`thrown-combined-flight`
  (TR): synthetic combat/death/loot/respawn, failure/restart with real weapons/armor. Logs: `build/logs/m4-combined-test.log`,
  `build/logs/m4-combined-tr-crossbow-test.log`, `build/logs/m4-combined-tr-thrown-test.log`.

**M4 incomplete.** Missing: overlapping desktop combat under network disruption, live knockout presentation evidence, broader effects/defenses, multi-NPC
simulation, player respawn/life generations, ranged ammunition recovery/player
contacts/NPC execution/input/rendering, movement/collision cutover.
Plain ranged sources and body-center proxies remain limitations.

V50 shares stock zero-base-fatigue knockout; older formats retain behavior.
Interrupted casts cancel without tick rollback/costs. Inherited vanilla `tes3mp_native_loadout_tests interrupted-casts`:
`build/logs/m4-interrupted-test.log` covers concurrent spells/items, failed writes and restart.

Death history preserves attribution beyond 1,024 deaths within the 16 MiB image
budget. Inherited synthetic V50/vanilla `tes3mp_native_loadout_tests death-history`
(`build/logs/m4-death-history-test.log`) covers deaths 1,024/1,025/2,049, atomic
failure/recovery, attribution, corpse-loot deduplication, respawn and restart.
M5 quest rewards remain unimplemented.

V50 projects knockout/knockdown frames for self, peers and NPCs. Clients sample
clips without wall-time advancement or gameplay/Lua callbacks; upright/death and
disconnect clear poses. Local incapacity lasts through get-up. Save layout unchanged.
New checks: `tes3mp_combat_replication_tests`
(`build/logs/m4-knockout-protocol-test.log`), real vanilla KF sampling without a scene
(`tes3mp_native_loadout_tests knockout-presentation`,
`build/logs/m4-knockout-presentation-test.log`), and synthetic vanilla pose
projection (`knockout-getup`, `build/logs/m4-knockout-presentation-getup.log`;
`zero-base-fatigue`, `build/logs/m4-knockout-presentation-loop.log`). Capability
negotiation passed `tes3mp_protocol_handshake_tests`
(`build/logs/m4-knockout-handshake-test.log`); desktop provider target builds.
Desktop visual acceptance remains unproven.

Next: remaining defenses and broader effects/enchantments; include get-up in live acceptance.
