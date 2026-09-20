# Current state and next action

**M4 in [PLAN.md](PLAN.md) is active. M3 was accepted on 2026-09-19.**
Raflod the Braggart now navigates **Seyda Neen, Arrille's Tradehouse** using
OpenMW DetourNavigator/PathFinder and stock collision/stepping. Two 60 Hz physics
steps compose with the existing native actor/inventory owner in each durable
30 Hz tick. Position, remaining path, contacts and inventory install only after
commit. Either active player sustains simulation. Clients interpolate committed
motion; player movement authority is unchanged. Computer-use remains disabled.

Next: server-owned NPC-door contact/avoidance using shared OpenMW physics, following
[DECISIONS.md](DECISIONS.md). Traveler activity after both players leave follows later.

[Host](../../apps/tes3mp-server/native/inventory_host.hpp): **native-inventory-16**
requires a fresh campaign, one dry interior, one living unscripted nonleveled NPC,
a fixed destination/speed and navigation settings. Background collision is frozen;
background inventories, doors, teleports and pickup/drop are unavailable in V16.
Both players leaving freezes the path. AI packages, gameplay animation timing,
combat, scripts, water, creatures, corpses and animated collision remain outside
this slice. Production headless packaging remains unproved. V15 campaigns retain
M3 behavior.

Verified 2026-09-20 in `build/vnext-desktop-evidence`, retained
Morrowind/Tribunal/Bloodmoon/Tamriel_Data/TR_Mainland loadout:

- `tes3mp_native_loadout_tests native-navigation`: atomic rejected ticks,
  inventory composition, exact mid-path/arrival recovery, malformed input,
  uncertain-write closure and both disconnects through production codecs/smoothing.
  Latest isolated maximum tick: 9.9 ms; an earlier run had one 35.1 ms outlier.
  `build/logs/m4-navigation-durable.log`.
- `tes3mp_headless_client_tests early-native-snapshot`: bounded pre-authentication
  buffering without early installation. Same-tick revision ordering is covered
  by `native-navigation`. `build/logs/m4-navigation-early.log`.
- Two real desktop clients at 30 FPS, 40 units/second: 100 ms one-way latency,
  ±25 ms jitter, 10% loss, periodic reordering; either player disconnects durably.
  Survivors move another 324 units and converge. Captured rendered frame steps
  stay below 5 units. Each scenario's `result.json` and bounded samples are under
  `build/logs/m4-navigation-Alice-final` and `build/logs/m4-navigation-Bob-final`.
  Reproduce with `scripts/run_native_navigation_capture.py`; capture processes stopped.
- User visually confirmed Raflod moving with two desktop clients on 2026-09-20.
  The observation run used 10 units/second to extend viewing time; the temporary
  slowdown was removed and the launch setup restored to 40 units/second.

Retained M3 campaigns: `build/m3-tr-varyon-doors` (25617),
`build/m3-tr-noran-dry` (25616). Prior acceptance:
`build/logs/m3-v15-acceptance.json`; inherited split/unload evidence remains synthetic.
