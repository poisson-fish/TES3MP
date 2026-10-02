# Current state and next action

**M4 in [PLAN.md](PLAN.md) is active.** [Host](../../apps/tes3mp-server/native/inventory_host.hpp)
runs 60 Hz NPC/door substeps and 30 Hz commits.

**V76 Target:** unbound launches follow authoritative release facing; collision
selects resident players, NPCs, creatures, doors and containers. Misses/world impacts
and impact areas reuse ordered effect transactions, stock eligibility/no-ops,
life generations, object revisions and committed ordinal outcomes. Old flights
cannot affect reborn recipients. Fresh V76 wraps V75; earlier save validation remains.
Bootstrap discovers intervention destinations before manifest binding (256 cells).
Sources bound 32 effects, area 65,535, duration 86,400s and magnitude 100,000;
effect/cue/image budgets reject atomically. Capability 35 admits extended ordinals;
older peers omit those transient applications.
Evidence in `build/logs`: `target-recipients-08.log`, `source-limits-02.log`,
`target-wire-01.log`, `target-capability-01.log`. Desktop acceptance pending.

**Retained spells:** V74 connects authored/racial/autocalculated sources,
launch/Silence, power cooldowns, Dispel immunity, ordered travel/cross-cell Self,
missed Touch and zero-magnitude object effects. Transaction evidence:
`build/logs/spell-presentation-transaction-04.log`.
V75 abilities/constants admit 143 selectors through shared stock handlers;
base stats, resistance/rolls, Corprus, cures, dormant actions and recovery remain
life/source-owned. Admission does not establish arbitrary compatibility.
141 authored stock records and composed lifecycle checks:
`build/logs/spell-passive-self-18.log`; stat/resource/wear regression:
`build/logs/spell-passive-stats-08.log`.

**Presentation:** capability 34 carries committed windup, failure school and
ordinal outcomes/lives. [Desktop](../../apps/openmw/tes3mp/desktop_providers.cpp)
shares stock cast/hands/hit/protection/object/area helpers and prediction deduplication.
Snapshots own ContinuousVfx cleanup; transient chunks admit atomically.
Capability-33 clients retain legacy cues.

**Restrictions:** AI excludes powers and uses equipped WhenUsed. Passives apply Self;
travel requires players; Soultrap requires creatures; two summon selectors are empty.
Sources/effects/cues/actors/cells remain bounded. Custom descriptors must include
intervention destinations. NPC collision is frozen. M5 owns spellbook/scripts,
scripted/keyed/trapped activation, summon overrides and story/quest consequences.

**Desktop acceptance:** V74 cases passed
(`build/spell-v74-acceptance-23/result.json`; runtime hashes verified):
[comparison](../../scripts/native_spell_presentation_capture.py), loops, objects and
cross-cell travel cover 11 AI variants and reconnect under 100ms delay,
±25ms jitter, 10% loss. Six stock resolver comparisons match VFX/audio dispatch,
without establishing pixel/waveform parity or generic compatibility. Prior V73:
16 cases (`build/combat-acceptance-16/result.json`).

**Next:** frozen NPC collision, then player movement cutover, last.
New Target/destination/size behavior needs desktop acceptance. PLAN defines sign-off.
