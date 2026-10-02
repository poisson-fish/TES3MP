# Current state and next action

**M4 in [PLAN.md](PLAN.md) is active.** [Host](../../apps/tes3mp-server/native/inventory_host.hpp)
runs NPC/door substeps at 60 Hz and commits at 30 Hz.

**Spell audit:** all 143 built-in IDs have explicit gameplay paths in
[preparation](../../apps/tes3mp-server/native/magic_runtime.cpp),
[resolution/consumers](../../apps/tes3mp-server/native/inventory_service.cpp) and shared
equipment/summon handlers. Resource/stat/defense/condition, awareness/movement/AI,
object/travel, equipment and summon families reach existing writers. This establishes
handler coverage, not arbitrary source combinations or desktop parity.

**Connected fixes (V74):** mixed travel resolves by ordinal on the receiving player;
Recall followed by Mark uses the staged destination. Self-only sources follow players
across bound cells and ignore desktop focus. Lock/Open on actors and travel on
nonplayers are stock no-ops without suppressing valid ordinals. Object bolts retain
object contact revisions. Authored/racial/stock-autocalculated spells share source
lookup; powers retain stock cost/success/Silence and a durable 24-game-hour cooldown.
Dispel leaves powers intact. Abilities/constants ignore non-Self ordinals without
dropping Self entries. Fresh V74 campaigns; capability-33 clients remain compatible.
Reused transaction/recovery checks pass: `build/logs/spell-completeness-08.log`,
`spell-object-compatibility-02.log`, `spell-launch-01.log`, `spell-passive-ranges-02.log`,
`spell-travel-regression-01.log`.

**Presentation audit:** [desktop path](../../apps/openmw/tes3mp/desktop_providers.cpp)
consumes committed release/impact/area cues; bolts interpolate committed positions
with looping sound. Persistent ContinuousVfx follows authoritative life/effect
snapshots and cleanup; reconnect restores loops without replaying impacts. Local
cast windup is predicted. Release cues use world anchors and record/range effects,
without per-effect applied results; failed-cast events currently skip failure audio.
Windup/release duplication, hit attachment and resisted/ignored-effect visuals need
desktop comparison.

**Restrictions:** stock AI excludes powers and uses equipped WhenUsed sources;
passives apply Self only, travel requires players, Soultrap requires creatures, and
two of 22 summon selectors are empty. Migration scaffolding still bounds effects
(eight/source, area 64, duration 3,600s, magnitude 1,000), active effects and actors;
magic requires a bound target, passive Self families remain restricted, interventions
require bound destinations, and NPC collision is frozen. M5 owns learned/removed
spellbook script state, scripted/keyed/trapped activation, summon overrides and
story/quest consequences.

**Migration base:** awareness, jump/air control/fall damage and passives share
canonical writers. All 11 bound effects/ExtraSpell and 20 stock summons retain
ownership/cleanup. Wait/rest expiry commits with game time. Player/NPC lives bind
attribution; ordinary inventory/social/Mark survive respawn. V73 body clocks and
stock death selection survive rollback/restart; non-strike enchanted ammunition
preserves release consumption/recovery. Existing evidence: `actor-bodies-06.log`,
`actor-fall-03.log`, `actor-body-timeline-01.log`, `actor-body-resources-01.log`,
`summon-scheduling-final-04.log` under `build/logs`.

**One desktop acceptance pass:** retain the 16 V73 two-desktop cases
(`build/combat-acceptance-16/result.json`: 100ms one-way delay, ±25ms jitter, 10% loss)
as prior evidence. Run fresh V74 mixed travel/object/power/racial sources alongside
the remaining 11 AI/disposition variants, Telekinesis, Soultrap and both interventions.
Compare cast windup/release/failure, Touch/Target/areas, resisted effects, persistent
VFX/equipment glow and complete cast/hit/area/bolt audio against stock, including
expiry/death, rejected writes, reconnect/restart and cross-cell travel. Prior bolt
looping-sound evidence does not establish full audio parity.

**Next:** finish the identified spell presentation/source limits and this acceptance
pass before player movement cutover. M4 sign-off still requires PLAN's exit criteria.
