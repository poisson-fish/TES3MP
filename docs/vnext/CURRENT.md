# Current state and next action

**Active: M5 [PLAN.md](PLAN.md), bounded story encounters. M4 closed, 2026-10-06.**
[Host](../../apps/tes3mp-server/native/inventory_host.hpp): 60 Hz physics substeps;
30 Hz authoritative commits. Preserve the working V77–85 migration base.

**Accepted baseline:** M4/V79 retain split/regroup/recovery.
Owner acceptance: `build/m4-user-acceptance-player-v79-20261006-live-01/operator-acceptance.json`;
impaired movement/recovery: `build/player-physics-desktop-{impaired-16,restart-04}/result.json`.
TR remains appearance-only; scripts await M5 content proof.

**M5 execution:** stock contexts/banks commit with gameplay;
unavailable services reject/defer, offline schedules suspend, recovery is silent.
V80 shared presentation and V85 authenticated paired dialogue have semantic desktop
proof: `build/m5-ambient-desktop-{02,restart-01,ui-01}/result.json`,
`build/m5-dialogue-desktop-{06,restart-02}/result.json`.

**V85 dialogue:** capability 40 authenticates speakers/choices. Detached stock filters,
private relationships, substitutions and journal text/calendar commit with banks.
Stats accepted,
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

**V89–90:** capability 42; stock source Position/PositionCell/GetPos/GetAngle
retain [bounded admission](DEVELOPMENT.md). V89 stays occupied; V90 returns
grounded bodies beyond original/destination NPC and peer hulls against the living
original life. Outcomes stay dormant; B keeps its opportunity. Rejection installs
nothing; uncertainty closes authority; recovery is silent.
Native: `build/logs/m5-relocation-return-{state,host-02}.log`;
V89/V88 regressions: `build/logs/m5-relocation-return-{v89-state,enable-regression}.log`.
Rendered: `build/m5-relocation-return-{desktop,restart}-01/result.json`.

**V91:** foreign death persists a dependency/paused clock after bank rollback;
newer lives resume once. Rejection/restart/stale-life/continuation:
`build/logs/m5-source-wait-{state,host-09}.log`.

**V92–93:** fresh images/capability 41. Stock OnDeath consumes attributed source-life
history only with bank/gameplay durability. Own/pre-quest credit, foreign/helper
exclusion, stale-life rejection and retry/restart pass. V93 retains the oldest
wait's next canonical opportunity until eligible credit; repeated foreign attacks
cannot consume B's life. No new loot/lives. Summons remain closed.
Native: `build/logs/m5-life-event-{state,host-03,stock}.log`,
`build/logs/m5-opportunity-{state,host-03}.log`.

**Health/wait UI:** staged health reads preserve bank/cursor/clock rollback.
Atomic retry/restart: `build/logs/m5-health-{state-final,host-03}.log`;
client receipts: `build/logs/m5-wait-adapter.log`.
Synthetic two-desktop V92–93 waiting/reserved/earned recovery:
`build/m5-wait-v{92,93}-restart-credit-01/result.json`.

**V94–96 & Lua (#5):** stock escort/travel; A/B independently arrive. V95 (`native-inventory-95`)
unifies V85–95, binds placed `npc->mScript` into personal/shared banks
(`fargoth`, `vodunius nuccius`, `TR_m1_T_Eldale`), supports multi-cell/actor `EncounterBinding`,
and admits source-bank GetJournalIndex/CellChanged/MenuMode/GetPCSleep/OnActivate/Activate/Disable,
foreign-NPC ModDisposition and provenance GetItemCount/AddItem/RemoveItem/Function_Item
so private NPC banks/timers/reads/items/receipts, departures, safe returns and life-cursors
coexist across deaths/respawns. V96 (`native-inventory-96`) adds persistent opt-in parties/enrollment:
compatible/helper enrollment, late-join checks, delegated choices with
dissent/physical split, per-character costs/stock rewards (`RewardReceipt`s), helper/party OnDeath
credit/reservations, offline/leader-loss handoff, leave/kick/rejoin, and wire v4 `DialogueEncounterStatus`.
M5 #5 scopes `MWLua` (`world.mwscript`, `sendEvent`, `LuaEvents`) and `LuaUtil` (`LuaStorage`,
`ScriptsContainer` timers/callbacks, deterministic serialization, `pcall`/`xpcall` waits,
`SafePtr` lifetimes) with character/story/shared isolation, rollback/retry, and `LUAM`/storage recovery.
Native: `build/logs/m5-{activity-host,departure-host,activity-state}-final.log`,
`build/logs/m5-npc-bank-{state,script}.log`, `build/logs/m5-{npc-state,dialogue-stock,quest-audit,multi-cell-build}.log`,
`build/logs/m5-parties-{adapter,dialogue,all-dialogue}.log`,
`build/logs/m5-{lua-build,script-context-lua-{context,rollback,stock}}.log`.

**Next:** run unchanged Seyda Neen and TR (Duel of Riddles) real-content quest verification.
Source/result preflights: `build/logs/m5-{seyda,tr}-quest-preflight.log`;
routines: `build/logs/m5-seyda-content-preflight-final.log`.
Package replacement/cross-cell AI remain closed; M5 stays open.
