# Current state and next action

**M4 in [PLAN.md](PLAN.md) is active.**
V22 saves server-selected target, release and KF-key contact with the actor
tick, rechecking current server positions. V23 adds combat stats for both
players and the selected NPC plus dedicated RNG to that durable tick.
Weapon condition remains in its inventory image; the selected NPC's equipped
condition is sampled at preparation. Player hulls, damage, wear, death, loot,
custom NPC models and random attacks remain unwired.

[Host](../../apps/tes3mp-server/native/inventory_host.hpp): **native-inventory-23**,
fresh campaign, V22 descriptor with `melee GROUP DIRECTION SPEED`. One dry
interior or nine adjacent exteriors; one traveler, 0–128 doors, two 60 Hz
substeps per tick. Actor/door/inventory commit together. Saturation pauses
both steps. Travel continues without players. Restart/unload retains physics,
destination and avoidance/RNG. Offline players freeze.

Background actors freeze. General AI, combat, scripts, water, automatic doors
and sliding neighborhoods are unavailable. Player movement is inherited.

Inherited evidence: two-client unattended TR Raflod travel
(`build/logs/m4-v20-unattended.log`) and a real KF chop proposal
(`build/logs/m4-melee-resource-real.log`). V21 codec/restart fixtures:
`build/logs/m4-melee-durable-test.log`, `build/logs/m4-melee-campaign-test.log`.

Verified 2026-09-24: `npc-melee-campaign` on a synthetic actor and real KF:
`build/logs/m4-melee-contact-test.log`; target build:
`build/logs/m4-melee-contact-build.log`. The fixture exercises a committed
contact, rejected/retried hit ticks, restart equality and an out-of-reach miss.

Verified 2026-09-24: `npc-combat-owner` on a synthetic actor and real KF:
`build/logs/m4-combat-owner-build.log`, `build/logs/m4-combat-owner-test.log`.
It checks rejected/retried ticks, restart equality, malformed RNG/stat and
equipped shortsword condition through rejection/recovery.
V22 regression: `build/logs/m4-combat-v22-regression.log`.

Next: at the KF hit key, use the staged state to resolve accuracy, resource
costs, damage and wear with OpenMW mechanics; commit once before installing.
Then route authenticated player attacks through the same tick, add durable NPC
life/death/corpse access, and prove duplicate requests and two-client reconnect.
No damage or player-facing combat event exists yet.
Exterior graphical crossings remain unverified. Retained M3 campaigns:
`build/m3-tr-varyon-doors`, `build/m3-tr-noran-dry`.
