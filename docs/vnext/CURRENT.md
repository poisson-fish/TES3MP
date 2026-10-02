# Current state and next action

**M4 in [PLAN.md](PLAN.md) is active.** [Host](../../apps/tes3mp-server/native/inventory_host.hpp)
runs NPC/door substeps at 60 Hz, commits at 30 Hz.

**Connected consumers:** shared stock awareness reads canonical Sneak, skills,
fatigue, boots, distance, facing, Blind and concealment for detection/witnesses.
Life-bound ForceJump/ForceMoveJump controls feed navigation, stock launch/air
control and physics; Jump/SlowFall affect actual trajectories. Landing damage,
death and grounded movement share durability. Mixed abilities/constants retain
stat arguments and durable rolls through movement/AI, equipment and summon writers.
Invisibility suppression preserves the remaining source ordinals.

Focused synthetic checks: `build/logs/consumer-awareness-rules-01.log`,
`build/logs/consumer-sneak-02.log`, `build/logs/consumer-jump-fall-rules-01.log`,
`build/logs/consumer-jumps-08.log`, `build/logs/consumer-mixed-rules-02.log`,
`build/logs/consumer-mixed-04.log`. Existing rejection/restart machinery checks
canonical state and physics/source continuation. Movement-enabled scene image 3
and changed resource binding require fresh campaigns; earlier non-movement layouts
remain unchanged. General scripts remain M5; bounded controls require explicit actor/life.
Desktop acceptance stays grouped.

Ranged/V64 aim passes loss (`build/logs/desktop-ranged-aim-live-01.log`).
Neighbor combat/creature attribution passes loss/reconnect
(`build/logs/desktop-neighbor-creature-damage-live-04.log`); reactions remain unverified.
Magic bolts/loops/reconnect pass loss (`build/logs/magic-loop-live-02.log`);
audio remains unverified. Enchanted ammo/general scripts and live bodies remain open.

**Object/travel (V69):** intervention, Telekinesis, ordered Target/area Lock/Open
and smallest-gem Soultrap share existing writers. Search/relocation rollback/restart
(`build/logs/object-travel-test-08.log`), reach/occlusion/expiry
(`build/logs/object-spells-test-10.log`) and capture/death deduplication
(`build/logs/object-soul-test-02.log`) pass. Fresh campaign/capability-30 clients.
Scripted/keyed/trapped containers, unbound destinations and changing frozen NPC
collision remain unsupported; travel is single-effect Self.

**Bound equipment (V70):** all 11 effects/ExtraSpell share handlers, temporary
identities/restoration, gloves and cleanup. Manual changes win; overlap preserves
ordinary items; temporary items cannot stack/transfer. Failed permanent equips stay
dormant. Synthetic lifecycle rollback/restart passes
(`build/logs/bound-equipment-lifecycle-12.log`). Fresh V70 campaign;
scripted bound records/general item scripts unsupported.

**Mark/Recall (V68):** disabled/unmarked casts pay. Loss/reconnect
(`build/logs/player-travel-desktop-live-14.log`), rollback/restart/epochs
(`build/logs/player-travel-test-20.log`) and cross-cell resync pass.

**Summons (V71):** all 22 selectors, ownership, capacity/removal and recovery pass.
Up to 32 bodies share collision, stock movement/combat, equipment/stats, RNG and
membership. Quadrupeds, flight, melee/ranged/known-spell/WhenUsed choices and authored
animation resources bind across 20 stock records; two selectors are placeholders.
Bonewalker's exact stock disease initializer uses the condition writer; overrides
remain unsupported. Fresh campaigns/complete bound-area resources required.
Collision unload/reload, wait/rest, elapsed resources/effects and source/bound/summon
expiry commit with game time, preserving action/respawn ticks
(`build/logs/summon-scheduling-final-04.log`).

**Player lives (V72):** attacks/casts/effects/Command/Soultrap/summons distinguish
lives. Death cleanup, respawn, canonical motion/epochs and history commit together.
Respawn restores first bound position/configured stats after the descriptor delay
while online; ordinary inventory/social/Mark survive. Historical attribution cannot
benefit/control new caster lives. Synthetic rollback/restart/stale-life and life-2
ownership pass (`build/logs/player-lives-14.log`); codecs/negotiation and callers
compile. Fresh V72 campaign/capability-32 clients; quest credit remains M5.

**Next:** finish enchanted ammunition and remaining body reactions before grouped M4 desktop acceptance.

60 M4 IDs still require acceptance or implementation:

- Movement (8) and AI/disposition (12): implemented headlessly; acceptance remains.
- Objects (4): Lock, Open, Telekinesis, Soultrap.
- Travel (2): DivineIntervention, AlmsiviIntervention.
- Equipment (12): 11 bound effects and stock ExtraSpell.
- Summons (22): configured bodies/residency/time skips covered headlessly; acceptance remains.
