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

**V86 terminal encounter:** fresh campaign/capability 41; one dry interior, one living
unscripted NPC and static geometry. Stock source-only Disable discovers a private
character/story scope through rollback/retry. Lookup, visibility, Bullet collision
and combat preserve B's opportunity without copying inventories/lives/loot.
Origins/scopes/banks/progression/receipts persist with gameplay; rejected writes
install nothing, uncertain writes close authority. Paired removals are atomic.
Scoped movement/resources continue; banks/new interactions suspend.

Synthetic proofs: `build/logs/m5-encounter-{state,host,wire,stock}.log`;
rollback, scoped collision, bounded input/lookup and silent recovery.
V85 NPC regression: `build/logs/m5-encounter-npc-regression.log`.
Paired rendered/semantic proof: `build/m5-encounter-desktop-05/result.json` and
`build/m5-encounter-restart-01/result.json`: A loses/passes the NPC; B retains
rendering/collision/unfinished choices. Reconnect/restart are silent; the stock
GUI lifetime guard closes removed replicas.

**V87 return:** fresh images, unchanged capability 41/views. Online living grounded
characters rejoin during a quiescent physics commit, beyond hull-sized NPC/peer
gaps (minimum 256/128 units). Occupancy changes; dormant removal/origin and terminal
bank/progression/receipts stay frozen. Other banks resume; B keeps its opportunity.
State/atomic failure/recovery proof: `build/logs/m5-return-{state,host-08}.log`.
Rendered return/reconnect/restart: `build/m5-return-{desktop,restart}-01/result.json`;
A/B regroup, shared NPC visibility/collision return, A stays complete and B unfinished.

**Next:** bounded source-NPC Enable under the
[M5 contract](DECISIONS.md#m5-implementation-contract). Relocation/escorts/mutable
scenes, broader restoration and unchanged Seyda Neen/TR proofs remain open. M5 stays open.
