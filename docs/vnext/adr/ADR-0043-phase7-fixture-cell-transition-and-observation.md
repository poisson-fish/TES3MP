# ADR-0043: Phase 7 fixture-cell transition and observation

Status: **Accepted**

Date opened: 2026-09-01

Date approved: 2026-09-01

Decision owner: project owner

Needed by: Phase 7 Slice 7.4

## Decision summary

The project owner approved Option A for all four decisions on 2026-09-01:

1. extend the reliable apply-once operation and canonical writer reducer with a
   typed fixture-cell transition carrying the normal session, command,
   entity-revision, and authority preconditions;
2. emit reliable typed enter/leave observation changes and targeted complete
   latest-wins spatial views rather than requiring clients to infer lifecycle
   changes from snapshots alone;
3. use fixed versioned Phase 7 fixtures: interior cell space `7` and exterior
   worldspace `8` at grid `0,0`; and
4. one writer commit changes canonical cell/revision and derives every affected
   observation result, so state and visibility cannot publish partially.

App-local session-control mutation, snapshot-only inference, configuration-
loaded fixtures, and delayed visibility recomputation were rejected. This is
the narrow Phase 7 fixture behavior approved by GDR-0001, not the general cell,
interest, or resynchronization design owned by Phase 11.

## Acceptance tests and demo

1. unknown fixture, wrong session/player/entity, stale revision, and duplicate
   commands fail or finalize according to existing apply-once rules without
   unintended mutation;
2. a same-cell request is deterministic and idempotent;
3. two ready players receive explicit enter and leave changes exactly when
   canonical fixture-cell equality changes;
4. each targeted complete view contains exactly the ready players in the
   target session's canonical fixture cell;
5. interior-to-exterior-to-interior transitions preserve deterministic change
   and snapshot ordering; and
6. injected encoding, queue, or publication failure cannot expose a partial
   canonical transition or partial visibility result.

The owner implementation demo must run two real headless clients through the
interior/exterior flow and show exact enter/leave evidence and converged
targeted views.

## Review triggers

Reopen before adding more fixtures, configurable fixture identity, client-
selected cells, cross-cell visibility, snapshot-inferred lifecycle, partial
publication, or general Phase 11 cell/interest behavior.

## Owner approval

Approved by the project owner on 2026-09-01: Option A for Decisions 1 through 4
and the acceptance/demo boundary above.

### Observation composition clarification

Approved by the project owner on 2026-09-01: each affected target receives one
bounded reliable typed enter/leave observation batch paired through one atomic
queue-admission operation with that target's complete latest-wins spatial view.
Encoding or capacity failure admits neither frame. Separate independently
admitted lifecycle messages and snapshot-inferred lifecycle remain rejected.

### Observation wire-root clarification

Approved by the project owner on 2026-09-01: use a distinct bounded
server-to-client reliable observation schema/root and frame kind. Do not mix
server lifecycle results into the client-command `ReliableOperation` root.

### Client observation application clarification

Approved by the project owner on 2026-09-01: the reusable client session owns a
bounded observed-player set. Reliable observation batches validate the bound
session and generation, reject stale or contradictory input without partial
mutation, and treat an identical duplicate as harmless. Reliable observation
state and the latest-wins spatial view apply independently so either transport
lane may arrive first; spatial snapshots do not synthesize lifecycle changes.

### Runtime writer composition clarification

Approved by the project owner on 2026-09-01: `ServerApplication` owns the
writer-confined transition sequence and `ConnectionSessionCoordinator` supplies
bounded session-to-connection routing. The canonical reducer exposes one
move-only prepare/commit transaction: preparation validates and derives the
exact candidate without mutation or observability, queue admission occurs from
that candidate, and commit succeeds only against the unchanged base version.
Cancellation or queue failure leaves canonical state and publication unchanged.

The project owner further approved one shared canonical writer for authenticated
joins and fixture transitions. A successful join publishes one typed
`SessionJoined` lifecycle change; it is not represented as a synthetic player
command. Rejected or cancelled joins publish no state or lifecycle change.

The project owner also approved a bounded multi-connection, multi-frame atomic
queue transaction. Authenticated join uses it to admit authentication, the
target's explicit initial `Enter` batch and complete view, plus every affected
existing target's observation/view pair before the shared canonical commit.
Any missing/full target or encoding failure admits nothing and cancels the join.
