# Current state and next action

**M4 in [PLAN.md](PLAN.md) is active.** V32 shares spell and `WhenUsed`
preparation. Failed casts spend magicka; items pay charge at launch. Eight
durable projectiles retain identities. Server contact applies Target areas,
excluding caster splash. Stats, timed resistance, death and outcomes commit
together. Inactive actors pause expiry. V32 requires a fresh campaign.

[Host](../../apps/tes3mp-server/native/inventory_host.hpp): one traveler,
128 doors, two 60 Hz substeps; saturation pauses simulation. Deadlines survive restart.

Earlier checks: NPC life (`build/logs/m4-npc-life-final-test.log`), unarmed
fatigue (`build/logs/m4-unarmed-effect-test-03.log`), two-desktop Self casts
(`build/m4-instant-spell-live-04/result.json`), `WhenUsed`
(`build/logs/m4-enchant-use-test-06.log`), timed resistance
(`build/logs/m4-timed-spell-test-08.log`), Target area
(`build/logs/m4-area-test-03.log`), player contact/splash
(`build/logs/m4-player-target-test-final.log`), ordinary casts
(`build/logs/m4-ordinary-target-test-01.log`), concurrent flights
(`build/logs/m4-projectile-collection-test-final.log`) and cast-once stacks
(`build/logs/m4-cast-stack-test-02.log`). Scripted cast-once items remain unsupported.

NPC and player melee hits trigger script-free equipped
`WhenStrikes` weapons. The native tick stages physical damage, wear, item
charge, supported Self/Touch/Target effects, RNG, death and hit outcome in one
commit. Rejected writes preserve charge; accuracy misses and lost contact do
not spend it. Two item charges may compose in a tick. Synthetic real-loadout
fixture and projectile regression passed `build/logs/m4-strike-test-final.log`
and `build/logs/m4-strike-projectile-regression.log`. Strike areas and effects
outside the bounded magic plan remain unsupported.

Background actors freeze. Projectiles may target NPC/player; player contact
uses a server-position proxy sphere. Bolt visuals are absent. Next: durable
knockout/recovery, then unarmed health damage, armor, block and remaining melee
rules. Scripted item effects and other cast sources still need support.
Player movement stays last after collision/smoothness checks.
