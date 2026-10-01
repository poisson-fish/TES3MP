# Current state and next action

**M4 in [PLAN.md](PLAN.md) is active.** [Host](../../apps/tes3mp-server/native/inventory_host.hpp)
steps NPCs/doors at 60 Hz, commits at 30 Hz and binds four fixed neighbors. Sneak, AI
jumps and mixed passives remain.

Ranged combat and V64 aim pass under loss (`build/logs/desktop-ranged-aim-live-01.log`).
Enchanted ammo/general scripts and live body acceptance remain open.

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

Synthetic vanilla checks pass two players/NPC
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

**Summons (V71):** the 22 selectors were already checked. Ownership, dormant
attempts, capacity/removal and recovery evidence remains
(`build/logs/summons-stock-seams-02.log`, `build/logs/summons-ownership-02.log`,
`build/logs/summons-capacity-02.log`, `build/logs/summons-recovery-02.log`).
Up to 32 bodies stage native hulls, stock equipment/stats, sources, RNG and
membership in one commit; summons never respawn. Stock creature speed/damage,
rotating quadruped hulls and intrinsic flight use shared movement/combat.
Movement/attack/hit/death/cast resources bind for all 20 configured stock records
across Morrowind/Tribunal/Bloodmoon (two selectors name reserved placeholders).
Random attacks retain authored hit/start timing and zero-length knockout loops.
Known spells/WhenUsed compete with melee/ranged choices through existing ratings,
payment, flight, defense and death writers. Cast clocks, target lives and animation
hashes persist; replication/presentation reuse dynamic identities.

The integrated fixture covers Clannfear, Winged Twilight, Storm Atronach, armed
Skeleton and Bonewalker: follow/combat, attack/cast restart, expiry, rollback,
malformed recovery, codecs and uncertain durable outcomes. Evidence is synthetic:
arena/vitals change; Skeleton receives stock bow/ammo; Twilight's stock walking
flag changes to intrinsic flight. Bonewalker's exact stock Brown Rot initializer
uses the condition writer; general scripts/overrides remain unsupported.
Resource/body logs: `build/logs/summons-body-resources-final.log`,
`build/logs/summons-body-quadruped-final.log`, `build/logs/summons-body-flying-final.log`,
`build/logs/summons-body-caster-final-02.log`, `build/logs/summons-body-ranged-final.log`,
`build/logs/summons-body-disease-final.log` and `build/logs/summons-body-biped-final.log`.
V71 requires a fresh campaign and complete resources within the bound area.
Collision resources stay resident; wait/rest with active summons rejects.
Desktop acceptance stays grouped.

**Next:** continue M4 player lives and remaining movement/AI consumers; resolve
summon residency/time-skip gaps before M4 exit. Keep desktop acceptance grouped.

60 M4 IDs still require acceptance or implementation:
- Movement (8) and AI/disposition (12): implemented headlessly; acceptance remains.
- Objects (4): Lock, Open, Telekinesis, Soultrap.
- Travel (2): DivineIntervention, AlmsiviIntervention.
- Equipment (12): 11 bound effects and stock ExtraSpell.
- Summons (22): configured stock bodies covered headlessly; residency/time skips and acceptance remain.

Player lives and live stock-body acceptance remain unproven.
