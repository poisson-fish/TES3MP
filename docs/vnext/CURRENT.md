# Current state and next action

**M4 in [PLAN.md](PLAN.md) is active.** [Host](../../apps/tes3mp-server/native/inventory_host.hpp)
runs 60 Hz NPC/door substeps and 30 Hz commits.

**V77 NPCs:** `actors auto "NAV_SETTINGS"`; isolated interiors/shared exterior
neighborhoods, single simulation, empty-cell clock freeze, filtered unload/reentry.
Bounds: 1–37 NPCs/summons, one exterior 3x3, 256 cells. Scripted/leveled/corpse
placements reject; AI packages remain M5. `build/logs/cell-actors-21.log`:
synthetic occupancy/collision/rollback/reload/restart/combat/flight.
Builds pass; desktop acceptance pending.

**V74–V76:** 143 selectors/141 lifecycles, Target/ground collision,
capability-34/35 presentation. `build/logs`:
`target-recipients-08.log`, `source-limits-02.log`,
`spell-presentation-transaction-04.log`, `spell-passive-self-18.log`,
`spell-passive-stats-08.log`, `world-collision-09.log`.
Stock helpers restore loops without transient replay.

**Restrictions:** AI powers/animated ground reject; WhenUsed needs equipment;
passives Self, travel players, Soultrap creatures. Two summons empty. Bind
intervention destinations; scripts/quests remain M5.

**Chargen:** [native provider](../../apps/tes3mp-server/native/character_construction.cpp)
shares stock stats/F_PCStart and winning-record discovery. Fresh V78 players have
empty inventories and cannot simulate. Atomic completion/recovery owns bodies,
stats, sources/resources and safe points. `iAutoPCSpellMax=100` works.
Capability 36 hides fresh bodies and preserves established race/sex/head/hair.
Synthetic `build/logs/chargen-installer-15.log`, `appearance-{protocol,adapter,installer}.log`:
isolation/order/validation/rollback/reentry/restart. Captures emit `player_appearance_sample`.
`chargen-clock.log` and `appearance-clock-adapter.log`: fixed scheduler/delivery regression.

**Chargen installation/outfits/motion repair:** profile replies bind the committed
canonical revision, including join/resume images. Completion waits for compatible
body/inventory/resources and destination baselines; delayed lanes resync. Stock
profile assembly precedes forced inventory replacement, including empty images.
`chargen-installation.log`, `chargen-install-protocol.log`,
`chargen-install-server-delivery.log`, `chargen-install-server-reverse.log`,
`chargen-install-restart.log`: synthetic ordering/delay/resume/restart evidence.
Public equipment now presents without private inventory/ground agreement;
rebuilds retain selected bodies and locomotion phase, with committed action timelines.
`public-outfit-adapter.log`, `public-outfit-body-contract.log`: synthetic/contracts.
Shared engine/network angle conversion fixes remote walking directions;
`remote-locomotion-coordinates.log`: players/both NPC streams, 17 headings,
diagonals/gaits and unchanged world interpolation. Desktop/server builds pass.
Rendered two-client correctness remains unverified.

**Death/respawn:** capability 37 adds life-bound Respawn to the death menu.
Cleanup restores stats/spawn/zero velocity/new generation; ordinary inventory/history
survive. `build/logs/respawn-{protocol,adapter,native}.log`, `player-lives-regression.log`:
request/automatic respawn bounds/rollback/restart. Builds pass; visuals unverified.
Capabilities sorted; launch passes (`build/logs/chargen-respawn-launch-build.log`).

**Inherited desktop acceptance:** V76/capability 35:
`build/v76-collision-acceptance-10/result.json`: synthetic Target/intervention/
ground/reconnect at 100ms delay, ±25ms jitter, 10% loss. V74:
`build/spell-v74-acceptance-23/result.json`; V73:
`build/combat-acceptance-16/result.json`.
[Stock comparisons](../../scripts/native_spell_presentation_capture.py) prove dispatch only.

**Next:** retest both clients' empty completion inventory/outfits, reentry/reconnect/
restart and walking directions. Test legitimate server-owned drops through durable
commit/replication/rendering/pickup/restart, with rejection diagnostics. Then bind
discovered interior doors, trace the six loaded exterior actors before restoring
excluded scripted/AI content, and share stock appearance assembly. Death-menu
respawn and varied/modded bodies still need rendered acceptance.
V77 latency/jitter/loss acceptance still precedes player physics cutover.
