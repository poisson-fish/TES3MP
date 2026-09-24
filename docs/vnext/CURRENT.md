# Current state and next action

**M4 in [PLAN.md](PLAN.md) is active.**
V24 resolves the selected NPC's KF hit against detached OpenMW stats: hit
chance, RNG, fatigue, weapon damage and wear. Simultaneous player inventory
intents and attacks compose in one durable actor/inventory/door tick; rejection
leaves it unchanged. NPC death stops travel and opens shared corpse loot.
Native combat joins receive a baseline and reliable hit events. V24 requires
a fresh campaign.

[Host](../../apps/tes3mp-server/native/inventory_host.hpp): **native-inventory-24**,
V22 descriptor with `melee GROUP DIRECTION SPEED`. One dry interior or nine
adjacent exteriors; one traveler, 0–128 doors, two 60 Hz substeps per tick.
Saturation pauses both steps. Travel persists without players and across
restart/unload; offline players freeze.

Background actors freeze. General AI, scripts, water and sliding neighborhoods
are unavailable. Player movement is inherited.

Inherited: two-client travel (`build/logs/m4-v20-unattended.log`), real KF
proposal (`build/logs/m4-melee-resource-real.log`), V21 restart
(`build/logs/m4-melee-campaign-test.log`).

Verified 2026-09-24: `npc-combat-owner`:
`build/logs/m4-combat-events-final-test.log`. Server and desktop builds:
`build/logs/m4-combat-events-final-server-build.log`,
`build/logs/m4-combat-desktop-final-build.log`.
It covers rejection/retry, player attacks, durable NPC death, one-time corpse
loot, duplicate request and reconnect (synthetic actor, real KF). Live
two-desktop Raflod encounter:
`build/logs/m4-combat-live-final/result.json`, with screenshots in that
directory. At 100 ms one-way latency, jitter and 10% loss, Bob lost 15.54
health; both clients presented one reliable NPC hit, and his resumed session
kept that damage. Player attacks, death and loot remain synthetic evidence.
One immediate post-hit reconnect failed; the passing capture waited one second
after peer convergence.

Next: persist NPC life generations, respawn deadlines and attributed death
events for M5, with stale-life rejection and restart proof. Then extend through
effects, spells and enchantments; player movement comes last. Armor, block,
resistances, native effects, unarmed damage, attack cadence and player hulls
remain unwired. Exterior graphical crossings remain unverified. M3 campaigns:
`build/m3-tr-varyon-doors`, `build/m3-tr-noran-dry`.
