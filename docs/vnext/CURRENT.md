# Current state and next action

**M4 in [PLAN.md](PLAN.md) is active.** [Host](../../apps/tes3mp-server/native/inventory_host.hpp)
steps NPCs/doors at 60 Hz, commits at 30 Hz and binds three neighbors.
Casts, conditions, stats, visibility, movement and twelve AI effects have vanilla/TR
checks. Sneak, AI jumps, mixed passives and non-player Command remain.

Server bows/crossbows/throws persist flight, contact, damage and recovery,
including enchanted WhenStrikes/area and script locals. Enchanted ammo does not
recover; item script instructions remain closed. Retry/restart checks pass:
`build/logs/npc-ranged-{bow,crossbow,thrown}-test-30.log`,
`build/logs/projectile-enchanted-{bow,crossbow,thrown}-complete-01.log`.
The neighbor cap is provisional.

V64 persists camera aim and sweeps server hulls. Misses pay once; reconnect
does not replay cues. Checks: `build/logs/aim-protocol-test.log`,
`build/logs/aim-cue-test.log`, `build/logs/bow-aim-enchanted-test.log`.

The body timeline interpolates knockdown loops and holds across hit clips.
Four-actor attack, fall, get-up, loss and reconnect checks pass:
`build/logs/body-timeline-test.log`; desktop object compiled in
`build/logs/body-pose-object-build.log`. Two-desktop body rendering is unverified.

Two clients under 10% loss shared impacts, death, corpse transfer and a miss:
`build/logs/desktop-ranged-aim-live-01.log`. Floor contact, one arrow cost and
both cues pass `build/logs/desktop-ranged-world-hull-05.log`.

Eight movement IDs admit passive abilities. Timed casts, observers, retry,
expiry and restart pass
`build/logs/movement-passive-test-02.log`. Earlier V56 saves with ignored abilities
need reset. Underwater WaterWalking, disabled Levitate and neighbor physics remain.

Magic presentation replicates Target flights and reliable impacts. Clients
assemble composite meshes, light bolts and play/stop moving loop sounds.
Both desktops showed Fireball_large flight/impact under 10% loss; reconnect
did not replay impact. Each started one sound loop. Evidence:
`build/logs/magic-vfx-live-bolt-08.log`,
`build/magic-vfx-live-bolt-08/result.json` and screenshots there.
Composite/light color and audible quality remain unverified; cast/hit/active
loops remain open. Protocol, two-observer, CastOnce/restart and area checks pass:
`build/logs/magic-vfx-{protocol-test,concurrent-test-10,area-test-02,enchanted-bow-test-01,adapter-test}.log`.
Client build: `build/logs/magic-vfx-product-evidence-build-07.log`. Capability 30 requires
updated desktops.

**Next:** verify composite bolts/light; restore cast/hit/active loops across
reconnect. Then close underwater WaterWalking and disabled Levitate exceptions.
Continue AI/disposition, object/travel, bound equipment and summons;
generalize actor/life ownership before player movement cutover.

62 effects remain in the M4 completion inventory across actors and sources:
- Movement (8): WaterBreathing, SwiftSwim, WaterWalking, Burden, Feather,
  Jump, Levitate, SlowFall.
- AI/disposition (12): Charm, CalmHumanoid, CalmCreature, FrenzyHumanoid,
  FrenzyCreature, DemoralizeHumanoid, DemoralizeCreature, RallyHumanoid,
  RallyCreature, CommandHumanoid, CommandCreature, TurnUndead.
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

81 + 62 = 143 IDs. Summons/player lives remain unproven.
