# Current state and next action

**M4 in [PLAN.md](PLAN.md) is active.** [Host](../../apps/tes3mp-server/native/inventory_host.hpp)
steps NPCs/doors at 60 Hz, commits at 30 Hz and binds four fixed neighbors. Sneak, AI
jumps and mixed passives remain.

Ranged combat and V64 aim pass under loss (`build/logs/desktop-ranged-aim-live-01.log`).
Enchanted ammo, scripts and bodies remain open.

Neighbor combat/creature attribution pass loss/reconnect
(`build/logs/desktop-neighbor-creature-damage-live-04.log`). Reactions remain unverified.

Magic bolts, loops and reconnect pass under loss (`build/logs/magic-loop-live-02.log`).
Audio remains unverified.

**Object/travel (V69):** stock intervention search uses explicit player context
and bound destinations. Telekinesis commits reach/expiry to capability-30 focus.
Ordered Target/area Lock/Open effects share the door/container writer. Soultrap
commits smallest-gem capture, death, inventory and payment with caster/life deduplication.

Fixtures pass marker ordering/search and relocation rollback/restart
(`build/logs/object-travel-test-08.log`), mixed Target/area effects and Telekinesis
reach/occlusion/expiry (`build/logs/object-spells-test-10.log`), and smallest-gem
capture, rejected-write retry, death and restart deduplication
(`build/logs/object-soul-test-02.log`). Telekinesis codec bounds/round-trip pass
(`build/logs/object-reach-codec-test-final.log`); touched stock callers and desktop
focus provider compile. V69 requires a fresh campaign and capability-30 clients. Scripted/keyed/trapped
containers, unbound destinations and changing frozen NPC collision remain unsupported.
Travel remains single-effect Self.

**Bound equipment (V70):** all 11 effects and stock ExtraSpell share explicit
actor/inventory handlers. Temporary identities/restoration links, paired gloves,
stock restrictions, NPC auto-equipment, manual changes and expiry/dispel/death
cleanup join the actor/inventory commit. Overlap removal preserves ordinary items;
temporary items cannot stack/transfer. Failed permanent equips stay dormant.

Synthetic vanilla checks pass all 11 effects for two players and an NPC
(`build/logs/bound-equipment-final-02.log`). Rollback/restart, failed equips,
overlap, replacement/wear, gloves, WhenUsed/Touch, malformed recovery, attack expiry
and coincident respawn/release pass
(`build/logs/bound-equipment-lifecycle-12.log`). V69 Soultrap regression
passes (`build/logs/bound-object-soul-regression.log`). V70 requires a fresh
campaign. Scripted bound records and general item scripts remain unsupported;
desktop equipment/body/audio acceptance is deferred.

**Mark/Recall (V68):** disabled/unmarked casts pay. Loss/reconnect
(`build/logs/player-travel-desktop-live-14.log`), rollback/restart/epochs
(`build/logs/player-travel-test-20.log`) and cross-cell resync
(`build/logs/player-travel-discontinuity-resync-final.log`) pass.

**Summons (V71):** all 22 stock selectors use the explicit store; creation/failure,
collision fallback and nested delete/purge share handlers. Reused ownership checks
cover source/owner/life, dormant attempts, capacity, removal and recovery
(`build/logs/summons-stock-seams-02.log`, `build/logs/summons-ownership-02.log`,
`build/logs/summons-capacity-02.log`, `build/logs/summons-recovery-02.log`).
The actor transaction stages up to 32 owned dynamic bodies, shared collision,
stock-filled/equipped inventories, stats, sources, RNG and removal together.
Stock follow distance and melee targeting retain owner/enemy life; summons never
respawn. Creation, equipment, motion/combat and removal use existing replication.
Desktop providers consume dynamic identities through native actor presentation.

One synthetic vanilla lifecycle passes actual cast, following, actor damage,
restart during attack, expiry, rejected-write retry, malformed owner recovery,
wire round-trip and both durable outcomes after storage failure
(`build/logs/summons-integrated-20.log`). The provider and stock follow caller compile.
V71 requires a fresh campaign. Bodies require unscripted NPCs/walking
biped creatures, complete combat resources and the bound processing area;
other bodies remember failed placement. Collision resources stay resident;
wait/rest with active summons rejects.
Desktop acceptance remains deferred.

**Next:** extend shared dynamic body/animation support beyond walking bipeds and
prove stock summon records through the connected lifecycle. Keep desktop acceptance grouped.

60 M4 IDs still require acceptance or implementation:
- Movement (8) and AI/disposition (12): implemented headlessly; acceptance remains.
- Objects (4): Lock, Open, Telekinesis, Soultrap.
- Travel (2): DivineIntervention, AlmsiviIntervention.
- Equipment (12): 11 bound effects and stock ExtraSpell.
- Summons (22): selectors checked; complete body/behavior coverage remains.

Full summon-body coverage and player lives remain unproven.
