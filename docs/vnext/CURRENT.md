# Current state and next action

**M4 in [PLAN.md](PLAN.md) is active.** [Host](../../apps/tes3mp-server/native/inventory_host.hpp)
runs NPC/door substeps at 60 Hz, commits at 30 Hz; sneak/jumps/mixed passives remain.

Ranged/V64 aim passes loss (`build/logs/desktop-ranged-aim-live-01.log`);
enchanted ammo/general scripts and live bodies remain open.

Neighbor combat/creature attribution pass loss/reconnect
(`build/logs/desktop-neighbor-creature-damage-live-04.log`). Reactions remain unverified.

Magic bolts, loops and reconnect pass under loss (`build/logs/magic-loop-live-02.log`).
Audio remains unverified.

**Object/travel (V69):** explicit-player intervention, Telekinesis reach/expiry,
ordered Target/area Lock/Open and smallest-gem Soultrap join existing writers.

Fixtures pass search/relocation rollback/restart
(`build/logs/object-travel-test-08.log`), mixed Target/area effects and Telekinesis
reach/occlusion/expiry (`build/logs/object-spells-test-10.log`), and smallest-gem
capture, rejected-write retry, death and restart deduplication
(`build/logs/object-soul-test-02.log`). Telekinesis codec bounds/round-trip pass
(`build/logs/object-reach-codec-test-final.log`); touched stock callers and desktop
focus provider compile. Fresh campaign/capability-30 clients required. Scripted/keyed/trapped
containers, unbound destinations and changing frozen NPC collision remain unsupported.
Travel remains single-effect Self.

**Bound equipment (V70):** all 11 effects/ExtraSpell share actor/inventory handlers,
temporary identities/restoration, gloves and expiry/dispel/death cleanup. Manual
changes win; overlap preserves ordinary items. Temporary items cannot stack/transfer;
failed permanent equips stay dormant.

Synthetic vanilla (`build/logs/bound-equipment-final-02.log`) and lifecycle
(`build/logs/bound-equipment-lifecycle-12.log`) checks cover rollback/restart,
restoration, wear, malformed recovery and coincident respawn/release. Soultrap
regression passes (`build/logs/bound-object-soul-regression.log`). Fresh V70 campaign;
scripted bound records/general item scripts unsupported; desktop acceptance deferred.

**Mark/Recall (V68):** disabled/unmarked casts pay. Loss/reconnect
(`build/logs/player-travel-desktop-live-14.log`), rollback/restart/epochs
(`build/logs/player-travel-test-20.log`) and cross-cell resync
(`build/logs/player-travel-discontinuity-resync-final.log`) pass.

**Summons (V71):** all 22 selectors, ownership, dormant attempts, capacity/removal and recovery pass
(`build/logs/summons-stock-seams-02.log`, `build/logs/summons-ownership-02.log`,
`build/logs/summons-capacity-02.log`, `build/logs/summons-recovery-02.log`).
Up to 32 bodies commit collision, equipment/stats, ownership, RNG and membership
without respawning. Shared stock movement/combat handles quadrupeds, flight,
melee/ranged/known-spell/WhenUsed choices. Authored timing, targets, lives and
animation resources bind across 20 stock records (two selectors are placeholders).

Synthetic lifecycle fixtures cover Clannfear, Twilight, Atronach, armed Skeleton
and Bonewalker, including combat, rollback/recovery and codecs. Arena/vitals change;
Skeleton receives bow/ammo; Twilight's walking flag becomes intrinsic flight. Bonewalker's exact stock Brown Rot initializer
uses the condition writer; general scripts/overrides remain unsupported.
Resource/body logs: `build/logs/summons-body-resources-final.log`,
`build/logs/summons-body-quadruped-final.log`, `build/logs/summons-body-flying-final.log`,
`build/logs/summons-body-caster-final-02.log`, `build/logs/summons-body-ranged-final.log`,
`build/logs/summons-body-disease-final.log` and `build/logs/summons-body-biped-final.log`.
V71 requires fresh campaigns/complete bound-area resources.
Collision unloads with inactive areas; reload retains identities, inventories,
targets and cast clocks. One scheduling fixture covers unload/reload, wait/rest,
elapsed damage, source/bound/summon expiry and rejected-write recovery/restart
(`build/logs/summon-scheduling-final-04.log`). Resource/caster regressions: `build/logs/summon-rest-regression-01.log`, `build/logs/summon-caster-scheduling-regression-01.log`. Effects/game time commit together;
action/respawn ticks remain unchanged. Desktop acceptance stays grouped.

**Player lives (V72):** attacks, casts, effects, Command, Soultrap and summon
ownership distinguish player lives. Death attribution/history, body-effect and
owned-source cleanup, respawn and canonical position/velocity/epochs commit together. Respawn uses first bound position,
configured stats and the descriptor's tick delay; ordinary inventory/social/Mark
survive. Offline players remain dead until active. Historical attribution survives without benefiting/controlling new caster lives.

A synthetic death/respawn/restart fixture passes rollback, stale-life rejection,
current-life controls, Soultrap and life-2 ownership/recovery (`build/logs/player-lives-14.log`). Cast codec and capability
negotiation pass (`build/logs/player-lives-codec-test.log`,
`build/logs/player-lives-handshake-test.log`); legacy equipment lifecycle passes
(`build/logs/player-lives-bound-regression.log`). Server/client capability callers
compile. Fresh V72 campaign/capability-32 clients required; M5 quest credit and
grouped live acceptance remain.

**Next:** finish sneak, jump and mixed-passive consumers before M4 exit. Keep desktop acceptance grouped.

60 M4 IDs still require acceptance or implementation:
- Movement (8) and AI/disposition (12): implemented headlessly; acceptance remains.
- Objects (4): Lock, Open, Telekinesis, Soultrap.
- Travel (2): DivineIntervention, AlmsiviIntervention.
- Equipment (12): 11 bound effects and stock ExtraSpell.
- Summons (22): configured bodies/residency/time skips covered headlessly; acceptance remains.
