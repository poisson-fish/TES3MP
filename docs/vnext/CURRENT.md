# Current state and next action

**Active: M5 [PLAN.md](PLAN.md), bounded story encounters. M4 closed, 2026-10-06.**
[Host](../../apps/tes3mp-server/native/inventory_host.hpp): 60 Hz physics substeps;
30 Hz authoritative commits. Preserve the working V77–85 migration base.

**Accepted baseline:** M4 exploration/chargen/combat/lives/loot and V79 physics retain
split/regroup/recovery.
Owner acceptance: `build/m4-user-acceptance-player-v79-20261006-live-01/operator-acceptance.json`;
impaired movement/recovery: `build/player-physics-desktop-{impaired-16,restart-04}/result.json`.
TR remains appearance-only; scripts await M5 content proof.

**M5 execution:** explicit stock contexts and bounded banks commit with gameplay;
unavailable services reject/defer, offline schedules suspend, recovery is silent.
V80 shared presentation and V85 authenticated paired dialogue have semantic desktop
proof: `build/m5-ambient-desktop-{02,restart-01,ui-01}/result.json`,
`build/m5-dialogue-desktop-{06,restart-02}/result.json`.

**V85 dialogue:** capability 40 authenticates speakers/choices. Detached stock filters,
private relationships, substitutions and journal text/calendar commit with banks;
clients install private views silently. Stats accepted,
2026-10-07: `build/m5-dialogue-ui-01/agent-signoff.json` and
`build/logs/m5-dialogue-operator-restart-01.log`. Substitution/disposition/journal visual
acceptance remains open. NPC relationships/atomic recovery:
`build/logs/m5-npc-state{,-host}.log`; journal: `build/logs/m5-journal-host.log`.

**V86–88 encounter base:** capability 41/fresh images; one dry interior,
one living unscripted NPC, static geometry. Stock Disable discovers private scope
through rollback/retry; origins/banks/progression/receipts share gameplay durability.
Lookup, visibility, Bullet collision and combat preserve B's opportunity without
new inventories/lives/loot. Grounded separated bodies can return; fixed stock
Enable restores only the original life. Completed stories stay frozen.
Native: `build/logs/m5-encounter-{state,host,wire,stock}.log`,
`build/logs/m5-return-{state,host-08}.log`, `build/logs/m5-enable-{state,host-02,stock}.log`.
Rendered: `build/m5-encounter-{desktop-05,restart-01}/result.json`,
`build/m5-return-{desktop,restart}-01/result.json`,
`build/m5-enable-{desktop-02,restart-01}/result.json`.
Recovery is silent; the stock GUI lifetime guard closes removed replicas.

**V89 relocation:** fresh images/capability 42; stock source-only
Position/PositionCell/GetPos/GetAngle within the same admitted interior. Discovery
rolls back/retries; finite coordinates ≤1,000,000, displacement ≤4,096,
rotation ≤360 degrees, existing instruction/call budgets. Terminal inert destination
visibility/collision persists with origin/banks/progression/receipts; B keeps the
original opportunity. Overlap checks precede commit/recovery; rejected writes
install nothing and uncertain writes close authority. No public inventory/life copies.
Relocated scopes stay occupied; AI/combat, new interactions and return remain closed.
Native: `build/logs/m5-relocation-{state,host-03,wire}.log`;
V88 regression: `build/logs/m5-relocation-enable-regression.log`.
Rendered: `build/m5-relocation-{desktop-04,restart-01}/result.json`; A retains destination, B unfinished; recovery silent.

**Next:** prove relocated-scope return while retaining the destination outcome under
the [M5 contract](DECISIONS.md#m5-implementation-contract). Escorts/mutable scenes,
broader restoration and unchanged Seyda Neen/TR proofs remain open. M5 stays open.
