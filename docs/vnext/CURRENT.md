# Current state and next action

**M4 in [PLAN.md](PLAN.md) is active.** [Host](../../apps/tes3mp-server/native/inventory_host.hpp):
60 Hz NPC/door substeps, 30 Hz commits.

**V77:** discovered actors, shared interior/exterior simulation, empty-cell freeze,
filtered unload/reentry. Bounds: 37 actors/summons, 256 bound cells.
Connected exterior regions share collision; interiors/disconnected regions stay separate.
Navigation follows actors. `build/logs/actor-regions-07.log`: synthetic 12-cell crossing/contact,
overlap/freeze/retry/restart/reentry. Startup geometry remains bound; discovery anchors one 3x3.
Scripts/leveled/corpses reject; full AI packages remain M5.
`build/logs/cell-actors-regions-02.log`: synthetic collision/rollback/restart/combat/flight.
Impaired desktop acceptance remains pending.

**V74–V76:** 143 selectors/141 lifecycles; capability 34/35 presentation.
`build/logs/{target-recipients-08,source-limits-02,spell-presentation-transaction-04,spell-passive-self-18,spell-passive-stats-08,world-collision-09}.log`.
AI powers/animated ground reject; WhenUsed needs equipment; passives Self,
travel players, Soultrap creatures. Two summons empty. Bind intervention destinations.

**V78:** fresh players cannot simulate; inventories are empty. Durable completion/
recovery owns stock bodies/stats/spells/resources/safe points; capabilities 6/36.
Winning appearances persist. Catalog pairs bound before allocation: 8,192/race, 65,536 total.
`build/logs/character-{catalog-bounds,appearances-stock,appearances-tr}.log`: ten races/both sexes, stock/TR.
Revision-gated installation resyncs delayed lanes; empty images clear starters.
Independent gear preserves bodies/locomotion; shared angles preserve interpolation.
Synthetic `build/logs/{chargen-installer-15,chargen-installation,chargen-install-protocol,chargen-install-server-delivery,chargen-install-server-reverse,chargen-install-restart,appearance-protocol,appearance-adapter,appearance-installer,chargen-clock,appearance-clock-adapter,public-outfit-adapter,public-outfit-body-contract,remote-locomotion-coordinates,chargen-control}.log`.

**Rendered acceptance, synthetic content:**
`build/character-desktop-fixture-02/{complete-03,restart-02}/result.json`: Breton female/
Argonian male completion/recovery, empty inventories.
`build/character-acceptance-02/{drop,pickup-restart,reentry,movement}/result.json`: chest-owned shirt equip,
longsword drop/replication, three discovered interiors/reentry, durable ground restart,
pickup/restart, four-heading clips/reconnect; screenshots inspected.
`build/character-tr-acceptance/{complete,restart-facing}/result.json`: both Breton sexes/TR heads/hair,
empty inventory/recovery; appearance-only mods. Capture samples profile/gear/inventory/locomotion.
`build/chargen-ui-{pick,generate-03,create}/result.json`: MyGUI pick/questions/custom,
Back/refresh/birthsign/completion; injected custom names. Refresh retains the modal,
transitions hide others, resume ignores closed dialogs; physical OS input unverified.
Resume lifetime now bounds inactivity, retaining expiry/generation/replay rejection
(`build/logs/resume-inactivity.log`). Inventory rejection diagnostics are bounded.

**Death/respawn:** capability 37; life cleanup preserves ordinary inventory.
`build/logs/respawn-{protocol,adapter,native}.log`, `player-lives-regression.log`,
`chargen-respawn-launch-build.log`; death-menu visuals pending.

**Inherited desktops:** `build/v76-collision-acceptance-10/result.json` (100ms delay,
±25ms jitter, 10% loss), `build/spell-v74-acceptance-23/result.json`,
`build/combat-acceptance-16/result.json`.
[Stock comparisons](../../scripts/native_spell_presentation_capture.py) prove dispatch only.

**Exterior trace:** `build/logs/seyda-six-actors.log`: six actors/nine interiors.
Single-cell preview disables travel. Excluded scripts/scripted inventory require M5;
restore only proven content.

**Next:** expand bounded exterior discovery/streaming and cross-boundary gameplay/visibility;
accept impaired two-client crossings. Retain body, stock-door exclusion,
NPC movement/death-menu acceptance before player physics cutover.
