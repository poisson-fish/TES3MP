# ADR-0042: Phase 7 authenticated join and identity allocation

Status: **Accepted**

Date opened: 2026-09-01

Date approved: 2026-09-01

Decision owner: project owner

Needed by: Phase 7 Slice 7.3

## Decision summary

The project owner approved Option A for all six decisions on 2026-09-01:

1. an engine-independent bounded join coordinator in `server_core` owns join
   state; the server app only pumps and wires it;
2. checked monotonic process-lifetime session, player, and entity identifiers
   reject exhaustion rather than wrapping;
3. a fresh authenticated join atomically installs its player and active session
   or installs neither;
4. one authenticated principal maps to one live session/player, and a duplicate
   live binding is rejected;
5. the server installs the accepted fixed interior fixture spawn with zero
   velocity and sends a complete session-targeted initial snapshot; readiness
   follows client application of that snapshot; and
6. capacity, identifier, token, canonical-validation, encoding, and send
failures reject or close without partial canonical state and expose only a
bounded stable reason.

On 2026-09-01 the project owner approved the composition clarification Option
A: the coordinator prepares one bounded provisional join without mutation,
composition issues the token and encodes/enqueues all responses, and the writer
commits only after every prerequisite succeeds. Any earlier failure cancels the
preparation. No rollback path or app-owned canonical mutation is introduced.

On 2026-09-01 the project owner also approved transport-enqueue Option A: the
repository-owned outbound queue exposes domain-neutral atomic pair admission,
and the server binds the authentication and snapshot frames to one connection.
Capacity or connection failure admits neither frame and leaves canonical state
unchanged.

Rejected alternatives were app-owned join logic, random identifiers, staged
install with rollback, multiple live players per principal, deferring the
initial snapshot, and retaining incomplete joins. Identifiers are not restart-
durable in Phase 7. Later lifecycle and persistence phases may replace that
policy.

## Acceptance tests and demo

Focused tests cover distinct stable identities, the fixed authoritative spawn,
snapshot targeting, duplicate-principal rejection, capacity and identifier
exhaustion, and atomic failure. The Slice 7.3 demo must show two authenticated
clients receiving distinct identities, complete initial snapshots, and no
orphan canonical state after one injected failure.

## Review triggers

Reopen before adding random or restart-durable identifiers, multiple players per
principal, client-selected spawn, partial joins, live replacement, or mutable
state access outside the server-core writer.

## Owner approval

Approved by the project owner on 2026-09-01: Option A for Decisions 1 through 6.
