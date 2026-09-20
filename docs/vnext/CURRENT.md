# Current state and next action

**M4 in [PLAN.md](PLAN.md) is active. M3 was accepted on 2026-09-19.**
V17 stages ordinary-door swings against server NPC hulls, then moves one NPC
against staged door angles. Doors, NPC physics/path and an inventory intent commit
together. Player movement is unchanged. Computer-use remains disabled.

Next: extract stock NPC door avoidance and update navigation for rotating doors,
then verify a real interior with two desktop clients. M4 step 2 remains active;
traveler activity after both players leave follows later.

[Host](../../apps/tes3mp-server/native/inventory_host.hpp): **native-inventory-17**
requires a fresh campaign, one dry interior, one living unscripted nonleveled NPC,
a fixed destination/speed, navigation settings and 1-128 ordinary doors.
The area image owns angles; physics derives transforms, including on recovery.
All retained NPC hulls obstruct doors; authenticated player reports supplement
server sensing. Either player sustains simulation; both leaving freezes it.

Navigation still uses authored obstacles. Background inventories, teleports,
pickup/drop, AI packages/avoidance, gameplay animation timing, combat, scripts,
water, creatures, corpses and animated collision remain unavailable.
Headless packaging remains unproved. V15/V16 campaigns retain their domains.

Verified 2026-09-20 in `build/vnext-desktop-evidence`:

- `tes3mp_native_actor_tests door-contact`: direction, reversal, Bullet pair
  ordering, actor isolation; `build/logs/m4-door-contact.log`.
- `tes3mp_native_loadout_tests npc-doors`: synthetic rooms on the retained
  Morrowind/Tribunal/Bloodmoon/Tamriel_Data/TR_Mainland loadout. Collision follows
  door angle; inventory composition, rejection/retry, stale input, mid-swing
  recovery, reversal, two player projections, disconnect, inactive freeze and
  uncertain-write closure pass. `build/logs/m4-npc-doors.log`.
  Desktop/published-mod interaction acceptance remains pending.
- Regressions: `native-navigation` (V16), `build/logs/m4-npc-door-v16.log`;
  `area-door-service` (V14), `build/logs/m4-npc-door-legacy.log`.

Retained V16 desktop evidence (2026-09-20): Raflod's navigation passed two clients,
100 ms latency, jitter/loss/reordering and either disconnect. User confirmed motion;
capture processes stopped. Evidence: `build/logs/m4-navigation-Alice-final`,
`build/logs/m4-navigation-Bob-final`, `build/logs/m4-navigation-durable.log`,
`build/logs/m4-navigation-early.log`. Reproduce with
`scripts/run_native_navigation_capture.py`.

Retained M3 campaigns: `build/m3-tr-varyon-doors` (25617),
`build/m3-tr-noran-dry` (25616); `build/logs/m3-v15-acceptance.json`.
Inherited split/unload evidence remains synthetic.
