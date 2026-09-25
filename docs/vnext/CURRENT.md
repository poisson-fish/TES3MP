# Current state and next action

**M4 in [PLAN.md](PLAN.md) is active.** V26 accepts base-known Always Succeeds
spells with up to eight fixed, zero-duration Self Restore Health, Magicka or
Fatigue effects. A source-neutral, range-aware OpenMW effect plan now accepts
spell and enchantment effect lists; only Self spells reach the live server.
Shared OpenMW stat mutation resolves effects; one tick commits costs, stats
and events. V26 requires a fresh campaign. M4 remains open.

[Host](../../apps/tes3mp-server/native/inventory_host.hpp): one interior or
nine exteriors, one traveler, 128 doors, two 60 Hz substeps per 30 Hz tick.
Saturation pauses both. Travel/respawn deadlines survive restart; offline
players freeze.

Verified 2026-09-24: `build/logs/m4-npc-life-final-test.log` covers death,
loot, respawn and restart. Unarmed fatigue passed
`build/logs/m4-unarmed-effect-test-03.log` and desktop capture
`build/logs/m4-unarmed-live-07/result.json`.

Verified 2026-09-24: `build/m4-instant-spell-live-04/result.json` captures two
desktops with one durable Self cast under latency/loss. The focused
`npc-instant-spell` regression in `build/logs/m4-general-magic-test-02.log`
passes rejected writes, duplicates, two-client events, mixed Self effects,
restart, source-neutral Target spell/enchantment preparation and live Target rejection.
Enchantment use and Target effects have headless preparation evidence only.

Background actors freeze. AI, scripts, water, armor, block, resistances,
sustained effects, projectiles, enchantment use, unarmed knockout/health
damage and player hulls remain unwired. Player movement is inherited. Next:
route one Target spell through server-owned projectile flight/contact and the
shared effect plan, with restart/reconnect proof. Then wire enchantment use.
Cut player movement over last, after shared collision and smoothness checks.
