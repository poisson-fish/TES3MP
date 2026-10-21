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
`build/logs/m5-dialogue-operator-restart-01.log`. Rendered substitutions/disposition/journal UI:
`build/m5-dialogue-substitutions-{desktop-05,restart-02}/result.json`,
`build/m5-dialogue-disposition-{desktop-03,restart-02}/result.json`. NPC relationships/atomic recovery:
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
grounded bodies beyond original/destination NPC/peer hulls against the living
original life. Outcomes stay dormant; B keeps its opportunity; recovery is silent.
Native: `build/logs/m5-relocation-return-{state,host-02,v89-state,enable-regression}.log`.
Rendered: `build/m5-relocation-return-{desktop,restart}-01/result.json`.

**V91–93:** foreign death persists a dependency/paused clock after bank rollback;
newer lives resume once (`build/logs/m5-source-wait-{state,host-09}.log`).
Stock OnDeath consumes attributed source-life history with bank/gameplay durability;
own/pre-quest credit, foreign/helper exclusion, stale-life rejection and retry/restart pass.
V93 retains the oldest wait's next canonical opportunity until eligible credit without
new loot/lives; summons remain closed (`build/logs/m5-{life-event-{state,host-03,stock},opportunity-{state,host-03}}.log`).
Staged health reads/wait UI: `build/logs/m5-{health-{state-final,host-03},wait-adapter}.log`,
`build/m5-wait-v{92,93}-restart-credit-01/result.json`.

**V94–96, Lua (#5) & Content (#6):** stock escort/travel; A/B arrive independently.
V95 (`native-inventory-95`) unifies V85–95, binds `npc->mScript` into personal/shared banks,
supports multi-cell/actor `EncounterBinding`, and admits source-bank
GetJournalIndex/Journal/SetJournalIndex/AddTopic/CellChanged/MenuMode/GetPCSleep/OnActivate/Activate/Disable,
GetSpell/AddSpell/RemoveSpell, StartCombat/StopCombat/SetFight/Cast, foreign-NPC ModDisposition,
cross-cell AI packages (`AiWander`/`AiTravel`/`AiEscort`/`AiFollow`/`AiEscortCell`/`AiFollowCell`/`GetAIPackageDone`/`GetCurrentAIPackage`/`GetPCCell`/`GetDistance`/`ForceGreeting`/`Goodbye`) with multi-cell escort continuation (e.g., Pemenie),
and provenance GetItemCount/AddItem/RemoveItem/Function_Item with quest-item drop/transfer locks and non-depleting world/container pickup rights (`prepareQuestItemAcquisition`) across deaths/respawns.
V96 (`native-inventory-96`) adds persistent opt-in parties/helpers, delegated choices with
dissent/physical split, per-character costs/rewards (`RewardReceipt`s), party kill XP/credit sharing (`combatMemberIds`), helper/party OnDeath
credit/reservations, offline/leader-loss handoff, leave/kick/rejoin, and wire v4 `DialogueEncounterStatus`.
M5 #5 scopes `MWLua` (`world.mwscript`, `sendEvent`, `LuaEvents`) and `LuaUtil` (`LuaStorage`,
`ScriptsContainer` timers/callbacks, serialization, `pcall`/`xpcall` waits, `SafePtr` lifetimes)
with character/story/shared isolation, rollback/retry, and `LUAM`/storage recovery.
M5 #6 proves unchanged Seyda Neen (`MS_FargothRing`, `MS_Nuccius`/`voduniusScript`,
`MS_Lookout`/`treestumpScript`, `MV_DeadTaxman`/`processusScript`), *Boots of Blinding Speed* (`EB_Unrequited`/`pemenieScript`), and TR *Duel of Riddles*
(`TR_M1_GS_MQ_4`, `TR_m1_T_EldaleSc`, `TR_m0_TelvanniHortDeathSc`) across A/B with
concurrent dialogue, failed-write rollback, waiting/reserved restart and attributed `OnDeath`.
Native: `build/logs/m5-{activity-host,departure-host,activity-state}-final.log`,
`build/logs/m5-npc-bank-{state,script}.log`, `build/logs/m5-{npc-state,dialogue-stock,quest-audit,multi-cell-build}.log`,
`build/logs/m5-dialogue-{activities,npc-banks,parties,stock}.log`, `build/logs/m5-parties-{adapter,dialogue,all-dialogue}.log`,
`build/logs/m5-closeout-story-{activities,npc-banks,parties}.log`,
`build/logs/m5-{lua-build,script-context-lua-{context,rollback,stock}}.log`,
`build/logs/m5-{world-script-quest-audit,seyda-quest-preflight,tr-quest-preflight,seyda-content-preflight-final}.log`.
Rendered: `build/m5-quest-banks-{desktop,restart}-02/result.json`,
`build/logs/m5-quest-banks-{fixture,desktop,restart}-02.log`.

**Next:** M5 closeout complete; proceed to M5 operator sign-off and M6 ([PLAN.md](PLAN.md)).
