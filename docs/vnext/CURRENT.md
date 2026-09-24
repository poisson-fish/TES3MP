# Current state and next action

**M4 in [PLAN.md](PLAN.md) is active.**
V24 resolves the selected NPC's KF hit against detached OpenMW stats: hit
chance and combat RNG, swing fatigue, physical weapon damage to the selected
player, and weapon condition. A simultaneous player inventory intent now
composes with that hit tick in one durable actor/inventory/door image. A
rejected write retains the prior stats, RNG, condition, contact and inventory.
Authenticated player attacks enter the tick and can kill the NPC.
Durable zero health stops its travel and opens its existing inventory as a
lootable corpse; both clients receive the shared death and loot state. V24
requires a fresh campaign.

[Host](../../apps/tes3mp-server/native/inventory_host.hpp): **native-inventory-24**,
fresh campaign, V22 descriptor with `melee GROUP DIRECTION SPEED`. One dry
interior or nine adjacent exteriors; one traveler, 0–128 doors, two 60 Hz
substeps per tick. Saturation pauses both steps. Travel persists without
players and across restart/unload. Offline players freeze.

Background actors freeze. General AI, scripts, water and sliding neighborhoods
are unavailable. Player movement is inherited.

Inherited: two-client TR Raflod travel (`build/logs/m4-v20-unattended.log`),
real KF proposal (`build/logs/m4-melee-resource-real.log`), V21 restart
(`build/logs/m4-melee-campaign-test.log`). Earlier synthetic combat checks:
`build/logs/m4-combat-owner-test.log`, `build/logs/m4-combat-hit-test.log`.

Verified 2026-09-24: `npc-combat-owner`:
`build/logs/m4-native-combat-test.log`. The final source change compiled:
`build/logs/m4-native-combat-final-object.log`.
It covers simultaneous unequip/KF hit, rejection/retry, authenticated player
attacks, one durable NPC death, one-time corpse loot, duplicate request, and
two-client reconnect after restart (synthetic actor, real KF). Server and
desktop provider targets build: `build/logs/m4-server-build.log`,
`build/logs/m4-desktop-build.log`. A live graphical two-client encounter was
not run.

Next: prove a live two-client encounter with latency and reconnect, then add
durable life generations, respawn deadlines and attributed death events for
M5. Armor, block, resistances, effects, unarmed damage, attack cadence,
player hulls and player-facing combat events remain unwired; this slice
resolves base physical weapon damage only.
Exterior graphical crossings remain unverified. M3 campaigns:
`build/m3-tr-varyon-doors`, `build/m3-tr-noran-dry`.
