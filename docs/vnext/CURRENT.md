# Current state and next action

**M4 in [PLAN.md](PLAN.md) is active.** [Host](../../apps/tes3mp-server/native/inventory_host.hpp):
one NPC/doors at 60 Hz, commits at 30 Hz; neighbors freeze. T3C4 requires capability 22.

Inherited two-desktop evidence under 10% loss/100 ms delay/jitter:
- Vanilla/TR casting: `build/m4-cast-vanilla-live-10/result.json`,
  `build/m4-cast-tr-live-02/result.json`, `build/m4-cast-vanilla-item-live-03/result.json`,
  `build/m4-cast-tr-item-live-04/result.json`.
- Vanilla directional swings/interruption/reconnect/restart:
  `build/m4-swing-vanilla-live-07/result.json`.
Records unchanged; actors/placements synthetic. TR Lua awaits M5.

V49 persists flight/collision/resources/RNG and recycles receipts. Inherited checks:
`build/logs/m4-bow-recycling-test.log`, `build/logs/m4-crossbow-recycling-test.log`,
`build/logs/m4-thrown-recycling-test.log`.
V49 `bow-flight` regression: `build/logs/m4-v50-bow-regression.log`.

V50 binds participant knockout/get-up clocks and stock knockdown rolls. Positive
fatigue cannot skip get-up; inactivity pauses clocks. Fresh campaign required.
Timed/constant Shield, Sanctuary, Blind and Fortify Attack feed combat.

Inherited headless checks:
- `tes3mp_native_melee_tests knockout-timing`, `hit-knockdown`:
  `build/logs/m4-knockout-timing-test.log`, `build/logs/m4-hit-knockdown-test.log`.
- `tes3mp_native_ai_magic_tests combat-modifiers`: `build/logs/m4-combat-modifiers-test.log`.
- `tes3mp_native_loadout_tests knockout-getup`: three resources, rejected/uncertain
  writes, offline pause/restart; `build/logs/m4-getup-test.log`.
- `bow-combined-flight` (vanilla), `crossbow-combined-flight`/`thrown-combined-flight`
  (TR loadout): physical hits, modifiers/expiry, attributed magic death, corpse loot,
  respawn, atomic failure, exact restart and convergent observers. Real weapons/armor;
  synthetic actors/spells/placements. Logs: `build/logs/m4-combined-test.log`,
  `build/logs/m4-combined-tr-crossbow-test.log`, `build/logs/m4-combined-tr-thrown-test.log`.

**M4 acceptance remains incomplete.** Overlapping desktop combat under network
disruption remains unproven. Missing: knockout poses, broader effects/defenses, multi-NPC
simulation, player respawn/life generations, ranged ammunition recovery/player
contacts/NPC execution/input/rendering, movement/collision cutover.
Plain ranged sources and body-center proxies remain limitations.

V50 zero-base-fatigue knockout shares stock rules; older formats retain behavior.
Inherited synthetic vanilla `tes3mp_native_loadout_tests zero-base-fatigue`:
`build/logs/m4-zero-base-test.log`; get-up regression:
`build/logs/m4-zero-base-getup-regression.log`.

Interrupted casts cancel without tick rollback/costs; NPC release clocks clear.
Inherited synthetic vanilla `tes3mp_native_loadout_tests interrupted-casts`:
`build/logs/m4-interrupted-test.log` covers spells/items, concurrent casts,
rejected/uncertain writes, retry/restart and observer convergence.

Damage/recovery no longer cap history at 1,024 deaths. Complete attribution remains
atomic within the existing 16 MiB image budget. Recovery bounds counts before allocation.
`tes3mp_native_loadout_tests death-history` fails before
(`build/logs/m4-death-history-before.log`), passes after
(`build/logs/m4-death-history-test.log`). Synthetic V50/vanilla seeded histories exercise
deaths 1,024/1,025/2,049: competing lethal effects, old-life NPC
attribution, rejected/uncertain writes, retries, malformed recovery, corpse loot once,
respawn, exact restart and convergent observers. M5 quest credit remains unimplemented.

Next: knockout/get-up presentation, then remaining defenses and broader
enchantments/effects per PLAN.
