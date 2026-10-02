# Current state and next action

**M4 in [PLAN.md](PLAN.md) is active.** [Host](../../apps/tes3mp-server/native/inventory_host.hpp)
runs 60 Hz NPC/door substeps and 30 Hz commits.

**V76 Target:** unbound launches follow authoritative release facing; collision
selects resident players, NPCs, creatures, doors and containers. Misses/world impacts
and areas share ordered effect transactions, stock eligibility, lives, revisions
and ordinal outcomes. Old flights cannot affect reborn recipients. V76 wraps V75.
Bootstrap binds up to 256 intervention cells. Bounded sources/effects/cues/images
reject atomically; limits are in DECISIONS. Capability 35 admits extended ordinals;
older peers omit those transient applications.
Evidence in `build/logs`: `target-recipients-08.log`, `source-limits-02.log`,
`target-wire-01.log`, `target-capability-01.log`. Desktop acceptance pending.

**Retained spells:** V74 connects stock sources, launch/Silence, powers, Dispel,
ordered travel, missed Touch and zero-magnitude objects. Evidence:
`build/logs/spell-presentation-transaction-04.log`.
V75 abilities/constants admit 143 selectors with life/source-owned stock handlers.
Admission does not establish arbitrary compatibility. 141 stock records/lifecycles:
`build/logs/spell-passive-self-18.log`; stat/resource/wear regression:
`build/logs/spell-passive-stats-08.log`.

**Presentation:** capability 34 carries committed windup/failure/ordinal/life cues.
[Desktop](../../apps/openmw/tes3mp/desktop_providers.cpp) shares stock helpers and
prediction deduplication. Snapshots own ContinuousVfx cleanup; chunks admit atomically.
Capability 33 retains legacy cues.

**Committed collision:** pickup/drop stages membership/transforms with inventory/ticks.
Detached physics/contact/visibility/navigation publishes after durability; reload/restart
rebuilds from inventory. Drop/pickup, physics, queries, rollback and recovery:
`build/logs/world-collision-09.log`.

**Restrictions:** AI excludes powers and uses equipped WhenUsed. Passives apply Self;
travel requires players; Soultrap requires creatures; two summon selectors are empty.
Sources/effects/cues/actors/cells remain bounded. Custom descriptors must include
intervention destinations. Animated ground collision still rejects. M5 owns spellbook/scripts,
scripted/keyed/trapped activation, summon overrides and story/quest consequences.

**Desktop acceptance:** V74: `build/spell-v74-acceptance-23/result.json`, verified hashes;
11 AI variants, loops/objects/travel/reconnect under 100ms delay, ±25ms jitter, 10% loss.
Six [stock comparisons](../../scripts/native_spell_presentation_capture.py) match VFX/audio
dispatch, without pixel/waveform or generic compatibility claims. V73: 16 cases,
`build/combat-acceptance-16/result.json`.

**Next:** grouped V76/collision desktop acceptance under latency/jitter/loss (PLAN),
then player movement cutover using shared physics, last.
