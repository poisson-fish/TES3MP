# GDR-0001: Phase 7 headless vertical-slice behavior

Status: **Accepted**

Date opened: 2026-09-01

Date approved: 2026-09-01

Decision owner: project owner

Needed by: Phase 7

## Decision questions

For the first real two-client headless flow, who selects spawn and cell state,
when do players become ready and visible, how does provisional movement advance,
what does a client do with authoritative snapshots, and what survives disconnect
and resume?

These choices govern gameplay behavior, authority, state scope, and reconnect.
Phase 7 production composition must implement only the approved behavior below.

## Existing constraints

1. ADR-0006 makes the server the sole canonical writer, permits only reversible
   client presentation, and forbids delegated gameplay authority in Phase 7.
2. GDR-0011 permits a controlling client to propose desired velocity only.
3. GDR-0012 provisionally stores accepted velocity without integrating position
   and requires reopening that boundary before runtime integration.
4. Phase 7 is a deliberately tiny fixture, not the final cell, movement,
   prediction, player-lifecycle, or persistence design owned by later phases.

## Decision summary

The project owner approved Option A for Decisions 1 through 6 on 2026-09-01:

1. the server creates a player at a fixed fixture spawn after authentication;
   readiness follows receipt of a complete initial snapshot;
2. scripted clients request only known fixture-cell transitions, which the
   server validates and commits atomically;
3. ready players observe one another only while in the same canonical fixture
   cell, with explicit enter and leave results;
4. fixed server ticks integrate approved canonical velocity with checked
   arithmetic and no collision, acceleration, terrain, or speed rule;
5. headless clients immediately replace confirmed state with each newer
   authoritative snapshot and reject stale snapshot sequences, without local
   prediction; and
6. disconnect hides retained player state for a fixed test-configured grace
   period; valid resume preserves identity, revision, and acknowledgement
   progress, while expiry removes the session/player and requires fresh
   creation. A second live connection is rejected.

## Approved options and rejected alternatives

### Decision 1: spawn and readiness

**Option A (approved):** server-owned fixed fixture spawn and a complete initial
snapshot barrier. **Option B:** client-selected spawn or readiness before the
snapshot. Option B weakens authority and creates a partial initial view.

### Decision 2: fixture-cell entry

**Option A (approved):** a bounded request names one known interior or exterior
fixture; the server validates and atomically commits it. **Option B:** accept a
client declaration as the canonical cell. Option B is a direct state write.

### Decision 3: visibility

**Option A (approved):** readiness plus equal canonical fixture cell determines
mutual observation and explicit enter/leave results. **Option B:** make all
players always visible. Option B cannot demonstrate cell-scoped observation.

### Decision 4: provisional movement

**Option A (approved):** each fixed server tick integrates current canonical
velocity into position with checked arithmetic. Overflow or invalid state fails
closed without partial mutation. No collision, acceleration, terrain, speed,
grounding, or teleport rule is inferred. **Option B:** publish velocity without
moving position. Option B cannot complete the required observable flow.

This narrowly reopens GDR-0012 only for Phase 7 tick integration. GDR-0004 must
still replace or ratify all production locomotion rules before engine use.

### Decision 5: headless correction

**Option A (approved):** accept only a newer server snapshot sequence and replace
confirmed state immediately; do not predict. **Option B:** add prediction and
threshold-based reconciliation now. Option B prematurely selects presentation
tuning owned by Phase 12.

### Decision 6: disconnect and resume

**Option A (approved):** hide retained state during a fixed test-configured
grace period; reject a second live connection; valid resume preserves player
identity, spatial revision, and finalized acknowledgements; expiry removes the
session/player and forces fresh creation. **Option B:** retain visibility or
allow implicit live replacement. Option B makes ownership and observation
ambiguous.

The grace duration is configuration, not durable gameplay state. Phase 7 must
bound and test it; later lifecycle policy may replace its value.

## Approved scenarios and acceptance tests

1. `authenticated_client_spawns_only_at_server_fixture`
2. `readiness_waits_for_complete_initial_snapshot`
3. `two_ready_players_observe_only_in_same_fixture_cell`
4. `interior_exterior_transition_emits_exact_enter_leave_results`
5. `fixed_tick_integrates_velocity_with_checked_atomic_revision`
6. `simultaneous_players_move_only_their_own_roots`
7. `overflowing_integration_fails_closed_without_partial_commit`
8. `headless_client_replaces_newer_snapshot_and_rejects_stale_sequence`
9. `disconnect_hides_player_during_bounded_grace`
10. `valid_resume_preserves_identity_revision_and_acknowledgements`
11. `expired_resume_requires_fresh_player_and_session`
12. `second_live_connection_is_rejected`
13. `malformed_or_unauthorized_requests_never_mutate_state`
14. `same_seed_and_command_trace_replay_identically`

The owner implementation demo must show the complete two-client interior and
exterior flow, simultaneous movement and observation, stale-snapshot rejection,
disconnect/resume within grace, expiry, and one fail-closed malformed or
overflow case. Queue, convergence, resume, and soak thresholds belong in test
configuration and evidence, not this behavior record.

## Consequences and boundaries

- Player cell, root, velocity, and revision remain one server-owned per-player
  canonical reality. Interest affects delivery only.
- Initial spawn and cell fixtures are test/product-slice constants, not content
  identity or general world-transition rules.
- Position integration is deterministic provisional headless behavior. It does
  not authorize OpenMW collision, prediction, speed, or teleport behavior.
- Hidden grace state is resumable session state, not durable persistence. Server
  restart may discard it in Phase 7.
- No client-authored snapshot, direct canonical mutation, delegated authority,
  scripting behavior, or TES3MP 0.8 compatibility is introduced.

## Review and replacement triggers

Reopen this record if Phase 7 needs client-selected spawn, more than the two
fixture cells, cross-cell visibility, prediction, collision, configurable
gameplay speed, live connection replacement, retained disconnected visibility,
restart persistence, or any authority beyond the controlling client's bounded
semantic proposals. GDR-0002 through GDR-0004 must ratify or replace the
provisional lifecycle, cell, interest, resync, and movement rules in their
owning phases.

## Owner approval

Approved by the project owner on 2026-09-01: Option A for Decisions 1 through 6
and all named acceptance scenarios. The approval authorizes Phase 7 production
composition within these bounds; it does not approve later production gameplay
rules or bypass any slice-specific architecture decision.
