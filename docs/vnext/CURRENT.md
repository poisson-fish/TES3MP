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

**V86–88:** capability 41; one dry interior/NPC. Stock Disable discovers private
scope through rollback/retry. Lookup/visibility/Bullet/combat agree without copied
loot/lives. Grounded separated bodies return; fixed Enable restores the original
life. Outcomes stay frozen; recovery is silent.
Native: `build/logs/m5-encounter-state-recheck-01.log`,
`build/logs/m5-encounter-{host,wire,stock}.log`,
`build/logs/m5-return-host-recheck-03.log`, `build/logs/m5-enable-{state,host-02,stock}.log`.
Rendered: `build/m5-encounter-restart-recheck-03/result.json`,
`build/m5-return-{desktop,restart}-recheck-02/result.json`,
`build/m5-enable-{desktop-02,restart-01}/result.json`.
GUI lifetime guard closes removed replicas.

**V89–90 relocation/return:** fresh images/capability 42. Stock source-only
Position/PositionCell/GetPos/GetAngle retain [bounded admission](DEVELOPMENT.md).
V89 stays occupied. V90 returns grounded online living bodies beyond quiescent
original/destination NPC and shared peer hulls, against the living original source life.
Destination/origin/banks/progression/receipts stay dormant; B retains the original
opportunity. Rejected writes install nothing; uncertain writes close authority.
No Enable, teleport home or public inventory/life copies; recovery is silent.
Native: `build/logs/m5-relocation-return-{state,host-02}.log`;
V89/V88 regressions: `build/logs/m5-relocation-return-{v89-state,enable-regression}.log`.
Rendered: `build/m5-relocation-return-{desktop,restart}-01/result.json`.

**V91 durable source waits:** fresh images/capability 41. Foreign NPC death persists
one dependency/story and pauses the stock clock after bank rollback. Newer canonical
lives resume once. Rejected death/respawn writes, silent reconnect/restart, stale-life
rejection and B's legitimate continuation: `build/logs/m5-source-wait-{state,host-09}.log`.

**Next:** attributed life-event cursors and fair opportunities under the
[contract](DECISIONS.md#m5-implementation-contract). Health/OnDeath, wait UI,
escorts and unchanged Seyda Neen/TR proofs remain open. M5 stays open.
