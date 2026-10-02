# Current state and next action

**M4 in [PLAN.md](PLAN.md) is active.** [Host](../../apps/tes3mp-server/native/inventory_host.hpp)
runs 60 Hz NPC/door substeps and 30 Hz commits.

**V76 Target:** unbound launches follow authoritative release facing; collision
selects players/NPCs/creatures/doors/containers. Misses and impact areas share
ordered stock transactions, lives/revisions and ordinal outcomes; old flights reject
reborn recipients. V76 wraps V75. Bootstrap binds up to 256 intervention cells.
Bounds reject atomically (DECISIONS). Capability 35 admits extended ordinals;
older peers omit them.
`build/logs`: `target-recipients-08.log`, `source-limits-02.log`,
`target-wire-01.log`, `target-capability-01.log`.

**Retained spells:** V74 sources/launch/Silence/powers/Dispel/travel/Touch/zero objects:
`build/logs/spell-presentation-transaction-04.log`.
V75: 143 ability/constant selectors; 141 stock lifecycles:
`build/logs/spell-passive-self-18.log`; stats/resources/wear:
`build/logs/spell-passive-stats-08.log`.

**Presentation:** capability 34: committed windup/failure/ordinal/life cues.
[Desktop](../../apps/openmw/tes3mp/desktop_providers.cpp) shares stock helpers and
deduplication. Snapshots clean ContinuousVfx; chunks admit atomically;
capability 33 retains legacy cues.

**Committed collision:** pickup/drop membership/transforms and inventory/ticks publish
physics/contact/visibility/navigation after durability. Reload/restart/rollback:
`build/logs/world-collision-09.log`.
Desktop baselines add omitted Bullet/navigation bodies, retain reload membership
and clear before deletion.

**Restrictions:** AI excludes powers and uses equipped WhenUsed. Passives apply Self;
travel requires players; Soultrap requires creatures; two summon selectors are empty.
Custom descriptors must bind intervention destinations. Animated ground rejects.
M5 owns spellbook/scripts,
scripted/keyed/trapped activation, summon overrides and story/quest consequences.

**Desktop acceptance:** V74: `build/spell-v74-acceptance-23/result.json`, verified hashes;
11 AI variants, loops/objects/travel/reconnect under 100ms delay, ±25ms jitter, 10% loss.
Six [stock comparisons](../../scripts/native_spell_presentation_capture.py) verify resource
dispatch; pixels/waveforms unverified. V73: 16 cases,
`build/combat-acceptance-16/result.json`.

V76/capability 35: `build/v76-collision-acceptance-10/result.json`, verified hashes,
23 checks under that profile. Synthetic vanilla-backed placements:
unbound player/NPC/creature hits, misses, impact areas, door Lock,
156 known sources, 32 effects/ordinal 31, bootstrap-discovered intervention cells,
drop/pickup physics/projectile/render visibility, reload and both reconnects.
Inventory/magicka persist; loops restore without transient replay. Frame p95:
9.3ms; pauses reach 2.16s/1.24s during loads/screenshots. Inherited V74 comparisons
and focused V76/collision rollback/restart remain evidence. Bounded acceptance.

**Next:** shared-physics player movement cutover, last, meeting prediction/reconciliation
and smoothness budgets.
