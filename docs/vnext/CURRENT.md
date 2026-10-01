# Current state and next action

**M4 in [PLAN.md](PLAN.md) is active.** [Host](../../apps/tes3mp-server/native/inventory_host.hpp)
steps NPCs/doors at 60 Hz, commits at 30 Hz and binds four neighbors. Sneak, AI
jumps and mixed passives remain.

Ranged combat and V64 aim pass under loss (`build/logs/desktop-ranged-aim-live-01.log`).
Enchanted ammo, scripts and bodies remain open.

Four-neighbor combat and attributed creature kills pass under loss and reconnect
(`build/logs/desktop-neighbor-creature-damage-live-04.log`). Body reactions remain unverified.

Magic bolts, loops and reconnect pass under loss (`build/logs/magic-loop-live-02.log`).
Audio remains unverified.

**Object/travel family (V69):** shared stock Divine/Almsivi marker search takes
explicit player context; destinations must belong to the bound campaign.
Telekinesis extends authoritative object reach using stock settings/feet conversion,
with actor/teleport-door restrictions, sight checks, expiry and restart. T3D2 /
capability 30 carries committed magnitude into stock desktop focus.
Target and ordered mixed Lock/Open effects use collision identity and the existing
door/container writer; area effects can reach both in one commit. Soultrap chooses
the smallest eligible empty gem, splits one item and commits its soul with creature
death, inventory revision and payment. Caster/dead-life checks prevent stale or duplicate capture.

Synthetic vanilla fixtures pass marker ordering/search and reused relocation rollback/restart
(`build/logs/object-travel-test-08.log`), mixed Target/area effects and Telekinesis
reach/occlusion/expiry (`build/logs/object-spells-test-10.log`), and smallest-gem
capture, rejected-write retry, death and restart deduplication
(`build/logs/object-soul-test-02.log`). Telekinesis codec bounds/round-trip pass
(`build/logs/object-reach-codec-test-final.log`); touched stock callers and desktop
focus provider compile. V69 requires a fresh campaign and capability-30 clients. Scripted/keyed/trapped
containers, unbound destinations and changing frozen NPC collision remain unsupported.
Travel remains single-effect Self.

**Bound equipment (V70):** all 11 effects and stock ExtraSpell share explicit
actor/inventory handlers. Sources own distinct temporary identities and previous
instance/record links, including both gloves. Stock equip restrictions, NPC
auto-equipment, manual-change priority and expiry/dispel/death cleanup join the
existing actor/inventory commit. Overlap removal splices restoration links;
ordinary same-record items survive. Temporary items cannot stack or transfer.
Constant/passive failed equips stay dormant until their source changes.

Synthetic vanilla checks pass all 11 effects for two players and an NPC
(`build/logs/bound-equipment-final-02.log`). Shared checks pass rollback/restart, failed equips,
overlapping sources, manual replacement, ordered wear, paired gloves, WhenUsed,
directed Touch, malformed recovery, attack admission/wind-up at expiry and
coincident NPC respawn/player release
(`build/logs/bound-equipment-lifecycle-12.log`). This also fixes staged caster-life
reconciliation and neighbor-image composition on respawn. V69 Soultrap regression
passes (`build/logs/bound-object-soul-regression.log`). V70 requires a fresh
campaign. Scripted bound records and general item scripts remain unsupported;
desktop equipment/body/audio acceptance is deferred.

Inherited Touch Lock/Open desktop captures cover their original behavior (`build/logs/object-magic-live-18.log`,
`build/logs/container-spell-live-final-07.log`). V69 desktop visuals/audio, focus,
soul-gem UI and destination presentation await grouped acceptance.

**Mark/Recall (V68):** independent Marks and teleport rules; disabled/unmarked
casts pay without moving. Desktops converge under 100 ms jitter and 10% loss (`build/logs/player-travel-desktop-live-14.log`). Rollback/restart, epochs and cross-cell resync pass
(`build/logs/player-travel-test-20.log`,
`build/logs/player-travel-discontinuity-resync-final.log`).

**Next:** review stock summons handlers and implement one connected, bounded
ownership/lifecycle slice using the existing actor commit. Desktop acceptance
is deferred; V69/V70 and creature reactions/mixed combat still need grouped
two-desktop verification under loss/reconnect.

60 M4 IDs still require milestone acceptance or implementation, including the
headless object/travel and equipment families above:
- Movement (8) and AI/disposition (12): implemented headlessly; acceptance remains.
- Objects (4): Lock, Open, Telekinesis, Soultrap.
- Travel (2): DivineIntervention, AlmsiviIntervention.
- Equipment (12): BoundDagger, BoundLongsword, BoundMace, BoundBattleAxe,
  BoundSpear, BoundLongbow, BoundCuirass, BoundHelm, BoundBoots, BoundShield,
  BoundGloves, ExtraSpell (preserve actual stock behavior).
- Summons (22): SummonScamp, SummonClannfear, SummonDaedroth, SummonDremora,
  SummonAncestralGhost, SummonSkeletalMinion, SummonBonewalker,
  SummonGreaterBonewalker, SummonBonelord, SummonWingedTwilight, SummonHunger,
  SummonGoldenSaint, SummonFlameAtronach, SummonFrostAtronach, SummonStormAtronach,
  SummonCenturionSphere, SummonFabricant, SummonWolf, SummonBear, SummonBonewolf,
  SummonCreature04, SummonCreature05.

Summons/player lives remain unproven.
