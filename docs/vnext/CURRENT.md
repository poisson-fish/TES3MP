# Current state and next action

**M4 in [PLAN.md](PLAN.md) is active.** [Host](../../apps/tes3mp-server/native/inventory_host.hpp)
runs 60 Hz NPC/door substeps and 30 Hz commits.

**V74:** ordered travel/cross-cell Self preserves stock no-ops/object revisions. Authored/racial/autocalculated sources, stock launch/Silence,
24-hour power cooldowns, Dispel immunity and passive range filtering remain connected.
Transaction/recovery: `build/logs/spell-presentation-transaction-04.log`.

**Shared presentation:** capability 34 carries committed windup, failure school and
per-ordinal outcomes/lives. The [desktop path](../../apps/openmw/tes3mp/desktop_providers.cpp)
shares stock cast/hands/hit/protection/object/area helpers, prediction deduplication,
replica animations and safe sound detach. Snapshots own
ContinuousVfx cleanup; transient chunks admit atomically without duplicate gameplay
events. Capability-33 clients retain legacy cues.

**V74 source expansion:** missed Touch/valid Self, zero-magnitude Lock/Open and
harmful Drain, combat, condition and resource passives use existing writers. Evidence: `spell-presentation-wire-03.log`,
`spell-cue-admission-04.log`, `spell-harmful-passives-02.log`,
`spell-adapter-capabilities-01.log`, `spell-patch-registry-03.log` in `build/logs`.

**V75 passive Self:** abilities/constants admit all 143 built-in selectors through
shared modifier, periodic mutation, applied-once and stock no-op handlers. Ability
fortifications/Damage Skill use base stats; constants retain resistance/rolls.
Corprus clocks, quenched ordinals and application witnesses preserve cure/removal,
replacement, inactive activation and recovery. Fresh V75 campaigns wrap V74;
older layouts retain their rules. Admission does not establish arbitrary compatibility.
Synthetic admission/validation: `passive-self-rules-01.log`; 141 authored stock
records plus composed Absorb, cures, Corprus, Dispel, travel, no-ops, resistance,
rollback/retry and restart: `spell-passive-self-18.log`. Stat/resource/wear regression:
`spell-passive-stats-08.log` in `build/logs`. Presentation evidence remains V74.

**Restrictions:** AI excludes powers and uses equipped WhenUsed. Passives apply Self;
travel requires players; Soultrap requires creatures; two summon selectors are empty.
Scaffolding bounds eight effects/source, area 64, duration 3,600s, magnitude 1,000,
active effects and actors. Target needs a bound target; interventions need bound
destinations; NPC collision is frozen. M5 owns spellbook script state,
scripted/keyed/trapped activation, summon overrides and story/quest consequences.

**Desktop acceptance:** four V74 two-desktop cases passed
(`build/spell-v74-acceptance-23/result.json`; runtime hashes verified):
[spell comparison](../../scripts/native_spell_presentation_capture.py), loops, objects and
cross-cell travel cover 11 AI variants, Telekinesis, Soultrap, interventions, powers,
mixed/zero sources, deduplication, attachment and reconnect under 100ms delay,
±25ms jitter, 10% loss. Six stock resolver comparisons match VFX/audio dispatch,
without establishing pixel/waveform parity or generic compatibility. Prior V73:
16 cases (`build/combat-acceptance-16/result.json`).

**Next:** generic Target recipients, then intervention destinations and spell-size
limits. Resolve frozen NPC collision before player movement
cutover, which remains last. PLAN defines sign-off.
