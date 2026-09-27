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

V49 persists player flight/collision/resources/RNG, recycling terminal receipts
beyond the retry window. Inherited 24-launch/failure/recovery checks:
`build/logs/m4-bow-recycling-test.log`, `build/logs/m4-crossbow-recycling-test.log`,
`build/logs/m4-thrown-recycling-test.log`.
V49 `bow-flight` regression: `build/logs/m4-v50-bow-regression.log`.

V50 adds participant-bound CPU knockout/get-up clocks and shared stock hit-knockdown
rolls. Positive fatigue cannot skip get-up. Inactive participants pause; death/respawn
clear body clocks. Player casts respect hit recovery. Fresh campaign required;
older layouts retain their behavior. Timed Shield, Sanctuary, Blind and Fortify Attack,
and nonharmful constant variants, feed existing combat calculations.

New headless checks:
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

**Full M4 acceptance is not ready.** Combined checks stage attack types in sequence;
sustained overlapping desktop combat under network disruption remains unproven.
Missing implementation includes knockout poses, broader effects/defenses, multi-NPC
simulation, player respawn/life generations, ranged ammunition recovery/player
contacts/NPC execution/input/rendering, and the final movement/collision cutover.
Plain ranged sources and body-center proxies remain limitations.

Source review found stock zero-base-fatigue knockout missing, interrupted-cast
exceptions potentially rejecting unrelated composed progress, and a 1,024-death
history ceiling rejecting later deaths. These findings have no new reproducer yet.
Next: narrow parity/interruption/saturation tests and fixes, then the remaining
ordered M4 work in PLAN. Ranged live acceptance follows combined combat;
movement remains last. Existing headless passes do not close these gaps.
