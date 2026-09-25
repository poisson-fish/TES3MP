# Current state and next action

**M4 in [PLAN.md](PLAN.md) is active.** V25 persists one selected, initially
living placed NPC's life generation, death history with killer player ID, and
respawn deadline. Death opens shared corpse loot. At the deadline one
transaction restores actor state and baseline inventory with
new item identities; rejected writes leave the corpse intact. Earlier-life
attack ticks and target revisions, and stale corpse loot, reject. V25 requires
a fresh campaign and `respawn TICKS` in its descriptor.

[Host](../../apps/tes3mp-server/native/inventory_host.hpp):
**native-inventory-25**, with bounded 30 Hz server-tick delay. One dry
interior or nine adjacent exteriors, one traveler, 0–128 doors, two 60 Hz
substeps per tick. Saturation pauses both steps. Travel and life deadlines
persist without players and across restart; offline players freeze.

Verified 2026-09-24: `npc-life-cycle` in
`build/logs/m4-npc-life-final-test.log` covers attributed death, malformed
history, loot, rejected respawn, staged restore,
fresh identities, stale-life rejection, and restart. Server and desktop
builds: `build/logs/m4-npc-life-final-server-build.log`,
`build/logs/m4-immediate-resume-desktop-build.log`.

Live two-desktop Raflod kill, immediate Bob reconnect and GUI corpse Take All:
`build/logs/m4-npc-life-live-15/result.json` with screenshots. The capture
uses the desktop melee interception hook through an automation
trigger, real loadouts and OpenMW resolution, over 100 ms one-way
latency, jitter and 10% loss. The corpse emptied for both clients.

The earlier immediate reconnect failure was an exact revision/acknowledgement
continuity check while commands could still commit in flight. Identity remains
fixed and revisions/acknowledgements now advance monotonically. A moving-NPC
immediate reconnect passed in `build/logs/m4-immediate-resume-live-01/result.json`.

Background actors freeze. General AI, scripts, water, sliding neighborhoods,
armor, block, resistances, unarmed damage and player hulls remain unwired.
Player movement is inherited. Next: extend this shared encounter through
effects, spells and enchantments. Cut player movement over last, after shared
collision and smoothness are verified. M3 content campaigns:
`build/m3-tr-varyon-doors`, `build/m3-tr-noran-dry`.
