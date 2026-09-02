# ADR-0045: Phase 7 disconnect, resume, and expiration composition

Status: **Accepted**

Date opened: 2026-09-02

Date approved: 2026-09-02

Decision owner: project owner

Needed by: Phase 7 Slice 7.6

## Decision question

How should the server hide a disconnected player, retain bounded resumable
state, restore it without losing identity or progress, expire it
deterministically, and keep credential, canonical-state, and delivery failures
atomic?

## Decision

The project owner approved Option A for all four decisions on 2026-09-02:

1. an engine-independent server-core lifecycle coordinator owns disconnect,
   resume, and expiration; the server app only reports connection events and
   pumps monotonic time;
2. disconnect removes the active canonical session while a bounded hidden
   record retains its player and session progress; resume restores the same
   session/player/entity with the checked next generation, and expiration
   removes the player and hidden record;
3. lifecycle state and resume-token rotation use prepare/commit/cancel
   transactions. Required authentication, complete snapshot, and observation
   outputs are admitted atomically before both transactions commit; and
4. the deadline is disconnect time plus configured grace. Resume is valid only
   strictly before it; at `now >= deadline`, expiration wins. One writer orders
   lifecycle events and rejects commands from disconnected or stale generations.

On 2026-09-02 the owner also approved the Option A/A1 composition clarification:
authentication exposes a prepared resume handle to the session/app composition,
and a narrow coordinator commits token rotation before canonical resume, rolls
the token back if canonical commit rejects, then finalizes the token transaction.
This preserves the credential, lifecycle, and application ownership boundaries.

Rejected alternatives were app- or transport-owned lifecycle policy, retaining
an apparently active canonical session, reconstructing deleted progress,
consuming a token or committing canonical state before output admission, exact-
deadline arrival ordering, and tick-count-based grace.

## Scenarios and acceptance tests

- `disconnect_hides_player_during_bounded_grace`
- `valid_resume_preserves_identity_revision_and_acknowledgements`
- `expired_resume_requires_fresh_player_and_session`
- `second_live_connection_is_rejected`
- exact-deadline expiration wins and stale-generation commands are rejected
- token, encoding, queue, and commit failures leave no partial lifecycle state
- repeated disconnect and duplicate resume fail closed
- a resumed client is not ready before applying its complete snapshot

## Consequences and failure modes

Hidden records are bounded, process-local, and non-durable. Server restart may
discard them. A pending token rotation reserves one existing token; cancellation
restores its usability, commit invalidates it and installs the replacement, and
duplicate preparation fails closed. Identifier/deadline/capacity exhaustion,
invalid canonical candidates, encoding failure, queue rejection, stale
preparations, and clock anomalies cannot partially expose a live session.

## Replacement triggers

Reopen before adding live connection replacement, restart-durable grace state,
multiple players per principal, configurable gameplay lifecycle rules, or a
transport-owned player/session policy. Phase 10 must ratify or replace this
provisional lifecycle.

## Owner approval

Approved by the project owner on 2026-09-02: Option A for all four decisions,
Option A for prepared authentication ownership, Option A1 for reversible
cross-transaction commit coordination, and the proposed acceptance tests.
