# Current state and next action

**M4 in [PLAN.md](PLAN.md) is active.** [Host](../../apps/tes3mp-server/native/inventory_host.hpp):
one NPC/doors at 60 Hz, commits at 30 Hz; neighbors freeze.
**V52, T3C7/capability 25; fresh campaigns required.**

NPC swings replicate committed identity/life, timing, direction, strength and facing.
Hit reactions persist their stock clip. A shared timeline samples self/remote/NPC
actions and body poses per frame, aligned with motion/facing. Gameplay remains
server-owned; visual callbacks are suppressed. Pauses/starvation hold; recovery
restores actions.

Four latest-state streams now fit each pump; the old budget limited combat to ~7 Hz.

Verified synthetic two-desktop captures with stock resources:
- `build/m4-smoothness-live-05/result.json`: physical get-up, retargeting, 128-tick
  retreat, reconnect/restart; stock male/iron longsword, selected screenshots inspected. Bob restarts first; offline Alice pauses.
- `build/m4-smoothness-npc-live-02/result.json`: NPC exhaustion/loop/get-up and recovery.
- `build/m4-smoothness-players-live-01/result.json`: player swings/interruption/restart;
  shared identities.

Each has `presentation-validation.json`: median render frame ~8.5 ms, 95th percentile
better than 60 FPS; recovery ~1x stock speed. Snapshot gaps: median one tick,
95th percentile three. Impairment: 100 ms +/-25 ms, 10% loss, periodic extra 125 ms.
120-FPS capture cap; overlapping combat/modded bodies remain unproven.

Focused evidence in `build/logs`: `m4-smoothness-timeline-test.log`,
`m4-smoothness-protocol-test.log`, `m4-smoothness-queue-test.log`,
`m4-smoothness-stock-pose-test.log` (four body layers),
`m4-smoothness-windup-test.log` (V52 rejection/retry/restart/retreat/pause),
`m4-smoothness-final-build.log` (client/server).

**Next: one creature's authoritative melee/body clips through the shared timeline,
verified on both desktops under impairment/reconnect/restart.** Custom-body/casting coverage and remaining combat scope follow.

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
