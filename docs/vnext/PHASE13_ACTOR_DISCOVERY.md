# Phase 13 actor lifecycle and AI discovery

Date: 2026-09-07

Status: **Complete; owner package decision required**

This pass traces the current player-only canonical and presentation path and
defines the smallest production actor foundation. It changes no gameplay,
protocol, authority, persistence, interest, movement, or rendering behavior.

## Current boundary

| Area | Repository-backed state | Actor gap |
|---|---|---|
| Canonical state | `CanonicalServerState` owns bounded player entities and active sessions. Player spatial state already has entity/revision/epoch, content appearance, transform, velocity, locomotion, and tick. | There is no actor type, actor catalog, AI state, lifecycle transition, or actor capacity. Actors must not acquire fake player/session identity. |
| Commands and simulation | Session-bound player locomotion enters one validated reducer and the server-owned movement/collision kernel. | No server tick producer exists for autonomous actor intent. A client cannot submit an actor result under current authority rules. |
| Interest and resync | Exact-cell projection enumerates players and sends reliable player membership plus latest-wins player spatial views. | Actor membership and spatial views need additive typed fields or messages. Absence due to interest must remain distinct from canonical deletion. |
| Protocol | Version 1.3 snapshots and reliable observation records are explicitly player-shaped. Older 1.2 clients retain bounded player movement. | Generalizing existing player records would widen compatibility risk. Actor support needs an optional negotiated capability and additive actor records. |
| Content | The manifest owns cell, appearance, movement, and collision identities; OpenMW maps one player appearance to a local NPC record. | Stable placed-actor identity, prototype mapping, initial transform, and bounded AI package data are absent. Local record names must remain outside protocol. |
| Presentation | `ReplicatedActor` already renders bounded remote player roots and canonical locomotion without gameplay authority. | Actor presentation can reuse renderer mechanics only after a typed actor mapping; renderer lifetime cannot create or delete canonical actors. |
| Persistence | Durable persistence is intentionally Phase 20. Player identities have a narrow atomic registry only. | Phase 13 may define stable actor identity and canonical lifetime, but must not invent a general persistence store or claim restart durability. |

## Recommended production seam

1. Add a bounded manifest-scoped actor catalog with stable `ActorId`, opaque
   `ActorPrototypeId`, initial cell/root, and a small AI package. OpenMW keeps
   the prototype-to-record mapping locally.
2. Add separate `CanonicalActorEntityState` storage. Actors are global canonical
   world state with their own entity/revision/authority epoch and no player or
   session binding.
3. Run a deterministic server-owned actor scheduler. The first package supports
   idle plus bounded waypoint travel/wander through the existing movement and
   collision seam. It does not implement combat, dialogue, inventory, scripts,
   needs, or full OpenMW AI.
4. Freeze actor simulation while no active player observes the exact cell, then
   resume from the retained canonical state. Interest changes visibility, never
   existence. No client lease exists in the first package.
5. Add negotiated actor capability records: reliable actor membership/lifecycle
   and latest-wins actor spatial/AI presentation. Preserve player 1.2/1.3
   decoding and do not overload `PlayerId` or player observation records.
6. Creation comes only from the validated server content catalog in this phase.
   Canonical removal requires a future typed domain command; interest leave,
   disconnect, and cell inactivity never delete or reset an actor.

## Owner package choices

These choices materially affect architecture and gameplay. Select one package
before implementation:

- **A — server-owned bounded foundation (recommended):** content-catalog actors,
  separate additive actor protocol/state, idle plus bounded waypoint travel/
  wander, exact-cell inactive freeze, and no delegation. This follows ADR-0006,
  resembles unloaded OpenMW behavior, and creates a safe base for combat and
  scripting without duplicating the full engine.
- **B — active-cell proposal leases:** the server owns lifecycle and commit, but
  leases OpenMW actor simulation to one observing client and validates proposed
  results. This can reuse more engine AI but requires lease selection, expiry,
  handoff snapshots, coarse plausibility rules, malicious/stalled owner handling,
  and a server fallback before the first actor appears.
- **C — full always-running server simulation:** implement broad OpenMW AI and
  all-cell ticking on the engine-independent server now. This offers the closest
  continuous-world model but greatly expands CPU/content parity work and crosses
  into combat, dialogue, inventory, scripting, and persistence prematurely.

Package A is the recommendation. Delegation stays available as a later measured
optimization; adopting it now would make handoff correctness part of the first
actor vertical slice without evidence that server-owned bounded AI is inadequate.

## Named proof scenarios

1. `actor_catalog_is_manifest_bound_bounded_and_unique`
2. `actor_identity_never_uses_player_or_session_identity`
3. `server_tick_is_the_only_actor_simulation_commit_source`
4. `actor_movement_uses_server_collision_and_checked_integration`
5. `inactive_exact_cell_freezes_and_resumes_canonical_actor_state`
6. `interest_leave_does_not_delete_or_reset_actor`
7. `actor_membership_precedes_latest_wins_actor_snapshot`
8. `late_join_and_resync_receive_complete_current_actor_view`
9. `legacy_client_ignores_unnegotiated_actor_capability`
10. `renderer_animation_cannot_mutate_actor_ai_or_root`
11. `stale_actor_revision_or_epoch_cannot_commit`
12. `disconnect_cannot_transfer_actor_authority`

If Package B is selected, also require atomic complete handoff, finite expiry,
revocation, stale-epoch rejection, no-eligible-client fallback, and delayed old-
owner traffic tests before any client proposal can reach the reducer.
