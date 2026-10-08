# Current state and next action

**Active: M5 [PLAN.md](PLAN.md), bounded story encounters. M4 closed, 2026-10-06.**
[Host](../../apps/tes3mp-server/native/inventory_host.hpp): 60 Hz physics substeps;
30 Hz authoritative commits. Preserve the working V77–85 migration base.

**Accepted baseline:** shared exploration/character creation, NPC combat/lives/loot,
and V79 stock player physics retain split/regroup/reconnect/restart behavior.
Owner acceptance: `build/m4-user-acceptance-player-v79-20261006-live-01/operator-acceptance.json`;
impaired movement/recovery: `build/player-physics-desktop-{impaired-16,restart-04}/result.json`.
TR remains appearance-only; scripts await M5 content proof.

**M5 execution:** stock compiler/opcodes use explicit campaign/story/physical/character/
source/life/invocation/event identities. Shared execution and bounded personal banks
commit locals/globals/lifecycle/progression/receipts with gameplay; unavailable services
reject or defer, offline schedules never catch up, recovery is silent.
V80 shared presentation and V85 authenticated paired dialogue have semantic desktop
proof: `build/m5-ambient-desktop-{02,restart-01,ui-01}/result.json`,
`build/m5-dialogue-desktop-{06,restart-02}/result.json`.

**V85 dialogue:** capability 40 authenticates living unscripted speakers, reach/sight/life,
revision and offered choices. Stock filters read detached attributes/skills/resources,
disposition and private source-NPC bases. Bounded stock substitutions and result-time
journal text/calendar persist with banks/progression/output. Clients install committed
private views; reconnect never expands, restamps or replays output. Stats accepted,
2026-10-07: `build/m5-dialogue-ui-01/agent-signoff.json` and
`build/logs/m5-dialogue-operator-restart-01.log`. Substitution/disposition/journal visual
acceptance remains open. NPC relationships/atomic recovery:
`build/logs/m5-npc-state{,-host}.log`; journal: `build/logs/m5-journal-host.log`.

**V86 terminal encounter:** fresh campaign/capability 41; one dry interior, one living
unscripted NPC and static geometry. Stock source-only Disable discovers a private
character/story scope, discards discovery effects and retries from committed state.
GetDisabled and named/implicit lookup share the detached source. Scope-bound NPC
state, peer/NPC visibility, Bullet collision and combat preserve B's opportunity;
no inventories, lives or loot are copied. Persist source placement/record/life and
scope with banks/progression/receipts/gameplay. Rejected writes install nothing;
uncertain writes close authority until exact recovery. Two characters can remove
independently in one transaction. Scoped characters retain movement/resources;
banks suspend and new dialogue/inventory/combat interactions stay closed.

Synthetic proofs: `build/logs/m5-encounter-{state,host,wire,stock}.log`;
paired rollback, real stock movement/projectile collision, B's attack eligibility,
malformed/truncated images/wire, named/foreign lookup, silent duplicates and exact/
uncertain restart. V85 NPC regression: `build/logs/m5-encounter-npc-regression.log`.
Owning desktop-provider/server builds:
`build/logs/m5-encounter-client-server-build.log`. No rendered V86 acceptance yet.

**Next:** paired rendered V86 removal/reconnect/restart proof, then one bounded
restoration/return boundary under the [M5 contract](DECISIONS.md#m5-implementation-contract).
Enable, relocation, escorts, mutable scene dependencies, broader NPC restoration,
and unchanged Seyda Neen/TR quest proofs remain open. M5 stays open.
