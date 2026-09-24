# Current state and next action

**M4 in [PLAN.md](PLAN.md) is active.**
V24 resolves the selected NPC's first KF hit against detached OpenMW stats:
hit chance and combat RNG, swing fatigue, physical weapon damage to the
selected player, and weapon condition. The actor, inventory and door candidate
save once before installation. A rejected hit tick retains the prior stats,
RNG, condition and contact. V24 requires a fresh campaign.

[Host](../../apps/tes3mp-server/native/inventory_host.hpp): **native-inventory-24**,
fresh campaign, V22 descriptor with `melee GROUP DIRECTION SPEED`. One dry
interior or nine adjacent exteriors; one traveler, 0–128 doors, two 60 Hz
substeps per tick. Saturation pauses both steps. Travel persists without
players and across restart/unload. Offline players freeze.

Background actors freeze. General AI, scripts, water, automatic doors
and sliding neighborhoods are unavailable. Player movement is inherited.

Inherited: two-client TR Raflod travel (`build/logs/m4-v20-unattended.log`),
real KF proposal (`build/logs/m4-melee-resource-real.log`), and V21 restart
(`build/logs/m4-melee-campaign-test.log`).

Verified 2026-09-24: `npc-melee-campaign` on a synthetic actor and real KF:
`build/logs/m4-melee-contact-test.log`; target build:
`build/logs/m4-melee-contact-build.log`. The fixture exercises a committed
contact, rejected/retried hit ticks, restart equality and an out-of-reach miss.

Verified 2026-09-24: `npc-combat-owner` on a synthetic actor and real KF:
`build/logs/m4-combat-owner-build.log`, `build/logs/m4-combat-owner-test.log`.
It checks rejection/retry, restart, malformed RNG/stats and shortsword condition.
V22 regression: `build/logs/m4-combat-v22-regression.log`.

Verified 2026-09-24: V24 `npc-combat-owner`:
`build/logs/m4-combat-hit-build.log`, `build/logs/m4-combat-hit-test.log`.
It covers damage, accuracy/reach misses, retry bytes, rejection and restart
(synthetic actor, real KF).

Next: compose player inventory intent with a hit-key tick, then route
authenticated player attacks through it. Add durable NPC life/death/corpse
access and prove duplicate requests and two-client reconnect. Armor, block,
resistances, effects, unarmed damage, player hulls and player-facing combat
events remain unwired; this slice resolves base physical weapon damage only.
Simultaneous player inventory intent at the KF key currently rejects the tick.
Exterior graphical crossings remain unverified. Retained M3 campaigns:
`build/m3-tr-varyon-doors`, `build/m3-tr-noran-dry`.
