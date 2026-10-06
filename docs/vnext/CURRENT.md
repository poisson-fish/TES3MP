# Current state and next action

**M4 in [PLAN.md](PLAN.md) is active.** [Host](../../apps/tes3mp-server/native/inventory_host.hpp):
60 Hz NPC/door substeps, 30 Hz commits.

**V77 bounded exploration accepted:** radius 0–7 start/door-exit discovery shares
256 cells; remote exits add bounded anchors. Startup caps 37 actors/summons,
8,192 collision references/region and 256 cached navigation tiles/actor.
Connected exteriors share collision; separate regions stay isolated. Demand
streams/releases geometry, retaining canonical actor state and frozen hulls/contact
witnesses. Resource changes stage bounded domains before swap.
Adjacent movement/combat/visibility follows current actor cells with one simulation writer.
Synthetic `build/logs/exploration-{bootstrap-radius2,discovery-03,regions-03,host-07}.log`:
27-cell export, remote-anchor exhaustion/retry, 12-cell navigation/contact,
geometry rejection/release, overlapping demand, split/regroup, rollback/retry/recovery.
`build/logs/exploration-{cell-actors-02,placed-actors-01,boundary}.log`:
collision/combat/flight regression, unsupported actor rejection, engine independence.
Scripts/leveled/corpses reject; full AI packages remain M5.

**Synthetic exploration:**
`build/exploration-desktop-{06,restart-01}/result.json`: two desktop clients cross
3→6→3 through inherited AutoMove, separate/regroup, unload/reentry and reconnect;
restart preserves item identities/counts and committed adjacent-cell damage.
100ms delay, ±25ms jitter, 10% loss/reorder. Crossings use desktop movement. `build/exploration-desktop-presentation-02/result.json`:
recovered shared actors/peer; screenshots inspected.

**V74–V76:** 143 selectors/141 lifecycles; capability 34/35 presentation.
`build/logs/{target-recipients-08,source-limits-02,spell-presentation-transaction-04,spell-passive-self-18,spell-passive-stats-08,world-collision-09}.log`.
AI powers/animated ground reject; WhenUsed needs equipment; passives Self,
travel players, Soultrap creatures. Two summons empty. Bind intervention destinations.

**V78:** fresh players cannot simulate; starters are campaign-configured. Durable completion/
recovery owns stock bodies/stats/spells/resources/safe points; capabilities 6/36.
Appearances persist. Catalog pairs bound: 8,192/race, 65,536 total.
`build/logs/character-{catalog-bounds,appearances-stock,appearances-tr}.log`: ten races/both sexes, stock/TR.
Starter grant/validation/recovery: `build/logs/character-starters-native.log`.
Desktop starters: `build/m4-user-acceptance-starters-20261004-210941/starter-installation.json`;
session accepted 2026-10-04.
Installation resyncs delayed lanes; empty images clear starters.
Gear/angles preserve body/motion.
Synthetic `build/logs/{chargen-installer-15,chargen-installation,chargen-install-protocol,chargen-install-server-delivery,chargen-install-server-reverse,chargen-install-restart,appearance-protocol,appearance-adapter,appearance-installer,chargen-clock,appearance-clock-adapter,public-outfit-adapter,public-outfit-body-contract,remote-locomotion-coordinates,chargen-control}.log`.

**Synthetic acceptance:**
`build/character-desktop-fixture-02/{complete-03,restart-02}/result.json`: Breton female/
Argonian male completion/recovery, empty inventories.
`build/character-acceptance-02/{drop,pickup-restart,reentry,movement}/result.json`: chest-owned shirt equip,
longsword drop/replication, three discovered interiors/reentry, durable ground restart,
pickup/restart, four-heading clips/reconnect; screenshots inspected.
`build/character-tr-acceptance/{complete,restart-facing}/result.json`: both Breton sexes/TR heads/hair,
empty inventory/recovery; appearance-only mods.
`build/chargen-ui-{pick,generate-03,create}/result.json`: MyGUI pick/questions/custom,
Back/refresh/birthsign/completion; injected custom names. Refresh retains the modal,
transitions hide others, resume ignores closed dialogs; physical OS input unverified.
Resume lifetime now bounds inactivity, retaining expiry/generation/replay rejection
(`build/logs/resume-inactivity.log`). Inventory rejection diagnostics are bounded.

**Death/respawn:** capability 37; life cleanup preserves ordinary inventory.
`build/logs/respawn-{protocol,adapter,native}.log`, `player-lives-regression.log`,
`chargen-respawn-launch-build.log`. Same-cell correction passed
(`build/logs/respawn-same-cell-{client-build,locomotion}.log`). Operator accepted
revival/Seyda Neen safe-point respawn, 2026-10-04; unarmed confirmed.

**Inherited desktops:** `build/v76-collision-acceptance-10/result.json` (100ms delay,
±25ms jitter, 10% loss), `build/spell-v74-acceptance-23/result.json`,
`build/combat-acceptance-16/result.json`.
[Stock comparisons](../../scripts/native_spell_presentation_capture.py) prove dispatch only.

**Exterior trace:** `build/logs/seyda-six-actors.log`: six actors/nine interiors.
Single-cell preview disables travel. Excluded scripts/scripted inventory require M5;
restore only proven content.

**Melee accepted, 2026-10-05:** operator passed Argonian combat after recoil fix.
Synthetic recoil/stock mouse input: stall reproduced, nine swings passed
(`build/logs/melee-recoil-{baseline,fixed}.log`). Fatigue/HUD match (182.25–185).
Death label added.
Replica stock bounds/visibility; server rolls. Shieldless creatures need no inventory.
`build/logs/melee-contact-geometry.log`, `build/melee-contact-validation-01/result.json`:
crab contact/dagger damage.

**Shared corpse pending:** operator denied opening/peer visibility. Neighbor-creature
regression and numeric desktop activation/transfer passed
(`build/logs/shared-corpse-creature-native-02.log`, `build/shared-player-corpse-validation-01/result.json`).
Creature respawn supports plain stores and absent destinations.

**Next:** nearby-peer/shared-loot acceptance, then reconnect/restart.
PLAN gates M4; retain body/door/NPC acceptance before player physics cutover. M5 unstarted.
