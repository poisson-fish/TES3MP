# Current state and next action

**M4 in [PLAN.md](PLAN.md) is active.** [Host](../../apps/tes3mp-server/native/inventory_host.hpp):
one selected NPC or walking bipedal creature/doors at 60 Hz, commits at 30 Hz;
neighbors freeze. **V52, T3C7/capability 25; fresh campaigns required.**

The shared timeline now presents an unarmed Dremora's melee, hit reactions,
physical knockdown, exhaustion and get-up. Creature admission requires no known
spells or equipped weapon; quadrupeds/flying creatures remain unsupported.
Callbacks stay suppressed; pauses/starvation hold.

Two-desktop evidence with synthetic stats/placements/spell and stock Dremora resources
(selected screenshots inspected):
- `build/m4-creature-fatigue-live-01/result.json`: melee, exhaustion,
  get-up, reconnect/restart.
- `build/m4-creature-physical-live-01/result.json`: creature melee/hit reactions,
  falls/get-up, reconnect/restart; 10 shared unique outcomes, four matching
  health hits, no duplicates.

Both include creature-only `presentation-validation.json`: render median ~8.5 ms,
95th percentile below 16.7 ms; melee/body ~1x stock before/after restart. Snapshot
gaps: median one tick, 95th percentile three. Impairment:
100 ms +/-25 ms, 10% loss, periodic extra 125 ms; 120-FPS capture cap.

Focused evidence in `build/logs`: `m4-creature-native-test.log` (atomic rejection,
exact retry/restart, two-observer events, retarget/retreat/pause),
`m4-creature-stock-pose-test.log` (four NPC bodies plus Dremora, callbacks suppressed),
`m4-creature-npc-regression.log`, `m4-creature-evidence-test.log`,
`m4-creature-stats-build.log` (client/server) and `m4-creature-final-build.log`.

**Next: bind one custom-body NPC's authoritative melee/body clips through the shared
timeline; verify matching layered resources and both desktops under impairment,
reconnect and restart.** Casting presentation and remaining combat scope follow.

100 built-in effects remain outside general native casting:

- Attribute/resources (8): DamageAttribute, DamageSkill, RestoreAttribute,
  RestoreSkill, FortifyHealth, FortifyMagicka, FortifyFatigue, FortifyMaximumMagicka.
- Defense/interference (7): FireShield, LightningShield, FrostShield, Silence,
  Sound, DisintegrateWeapon, DisintegrateArmor.
- Concealment/detection (7): Invisibility, Chameleon, Light, NightEye,
  DetectAnimal, DetectEnchantment, DetectKey.
- Movement (8): WaterBreathing, SwiftSwim, WaterWalking, Burden, Feather,
  Jump, Levitate, SlowFall.
- AI/disposition (12): Charm, CalmHumanoid, CalmCreature, FrenzyHumanoid,
  FrenzyCreature, DemoralizeHumanoid, DemoralizeCreature, RallyHumanoid,
  RallyCreature, CommandHumanoid, CommandCreature, TurnUndead.
- Conditions (16): WeaknessToCommonDisease, WeaknessToBlightDisease,
  WeaknessToCorprusDisease, ResistCommonDisease, ResistBlightDisease,
  ResistCorprusDisease, CureCommonDisease, CureBlightDisease, CureCorprusDisease,
  CurePoison, CureParalyzation, RemoveCurse, Corprus, Vampirism, SunDamage,
  StuntedMagicka.
- Objects (4): Lock, Open, Telekinesis, Soultrap.
- Travel (4): Mark, Recall, DivineIntervention, AlmsiviIntervention.
- Equipment (12): BoundDagger, BoundLongsword, BoundMace, BoundBattleAxe,
  BoundSpear, BoundLongbow, BoundCuirass, BoundHelm, BoundBoots, BoundShield,
  BoundGloves, ExtraSpell (preserve actual stock behavior).
- Summons (22): SummonScamp, SummonClannfear, SummonDaedroth, SummonDremora,
  SummonAncestralGhost, SummonSkeletalMinion, SummonBonewalker,
  SummonGreaterBonewalker, SummonBonelord, SummonWingedTwilight, SummonHunger,
  SummonGoldenSaint, SummonFlameAtronach, SummonFrostAtronach, SummonStormAtronach,
  SummonCenturionSphere, SummonFabricant, SummonWolf, SummonBear, SummonBonewolf,
  SummonCreature04, SummonCreature05.

43 implemented + 100 remaining = 143 IDs; arbitrary scripted/mod support unproven.
Powers/abilities/diseases remain outside general
casting; preserve stock restrictions. Multi-NPC/summon/player-life prerequisites remain.
Ranged completion and overlapping combat precede movement/collision cutover.
Plain ranged sources/body proxies remain;
TR Lua awaits M5.
