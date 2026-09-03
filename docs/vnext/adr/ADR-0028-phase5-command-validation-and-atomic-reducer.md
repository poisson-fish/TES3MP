# ADR-0028: Phase 5 command validation and atomic reducer

Status: **Accepted**

Date opened: 2026-08-28

Date approved: 2026-08-28

Decision owner: project owner

Needed by: Phase 5 Slice 5.3

Companion gameplay record:
[`GDR-0012`](../gdr/GDR-0012-phase5-minimal-motion-reducer-semantics.md)

## Decision questions

What production boundary may own and replace the immutable canonical state;
what is the atomic commit unit; in which deterministic order are batch shape,
session, sequence, binding, revision, epoch, and domain rules checked; when may
contiguous-finalized acknowledgement progress advance; and which duplicate and
authority checks belong in Slice 5.3 without silently implementing the broader
Slice 5.5 idempotency/resynchronization design?

These are architecture, authority, ordering, state-mutation, and slice-boundary
decisions. Production reducer code must not land until the project owner
approves or amends this ADR and GDR-0012. This record does not decide player
lifecycle, reconnect/replacement, persistence, snapshot publication, command
result delivery, interest, production movement, collision, or resynchronization.

## Decision summary

The project owner approved Option A for Decisions 1 through 5 on 2026-08-28:

1. add one writer-confined reducer that owns an immutable
   `CanonicalServerState`, verifies one sealed ordered tick batch, and installs one
   complete replacement per finalizable command;
2. use a closed deterministic validation order and closed typed dispositions,
   with no callback, exception, or sink result able to alter the outcome;
3. finalize only the exact next command sequence, advance its session
   acknowledgement for accepted or rejected outcomes, and classify older or
   gapped sequences without mutation;
4. stage player and session replacements together, validate the complete
   candidate state, and publish no partial state or domain change record; and
5. check the current stable authority epoch now, bound same-batch duplicates,
   and leave cross-batch `CommandId` windows, checksums, and resync to Slice 5.5
   while keeping the reducer disconnected from any online runtime until then.

## Existing constraints

1. ADR-0006 requires one server command/reducer path, server-only commit
   authority, explicit proposal authorization, revision/epoch checks, and no
   partial mutation.
2. ADR-0013 requires checked counters, stable order, deterministic results, and
   no wall-clock, hash-order, or client-time influence on reduction.
3. ADR-0016 defines velocity in position quanta per server tick and requires
   checked reducers, while deferring speed, acceleration, collision, teleport,
   and production movement rules.
4. ADR-0024 makes `CommandId` the apply-once identity, `CommandSequence` the
   session-generation order, and acknowledgement the highest contiguous final
   disposition, including rejection.
5. ADR-0026 supplies at most 1,024 commands per ordered tick batch. Intake does
   not validate, deduplicate, authorize, or mutate state.
6. ADR-0027 supplies immutable sorted player/session partitions, explicit
   bindings, a pure checked spatial advance, and optional acknowledgement
   progress. It withholds a general mutation API.
7. Slice 5.4 owns immutable snapshot and versioned state-change publication.
   Slice 5.5 owns persistent cross-batch idempotency windows, state checksums,
   and explicit resync. Slice 5.6 owns persistence/replay/script/metrics domain
   sink interfaces.
8. No production multiplayer runtime exists, so Slice 5.3 must not connect this
   incomplete pre-Slice-5.5 reducer to network or session delivery.

## Representative scenarios

1. One current session submits sequence 1 for its bound player/entity with the
   current revision and epoch. The approved GDR rule produces one complete
   player/session replacement and acknowledgement 1.
2. A current command fails its revision, epoch, target-binding, or domain rule.
   Player state remains identical. If the command was the exact next sequence,
   its rejection and acknowledgement advance together as one complete session
   progress replacement.
3. An old session generation, unknown session, or unbound entity cannot mutate
   player state or current-session acknowledgement.
4. A command at or below the represented contiguous acknowledgement is already
   finalized and changes nothing. A command beyond the next sequence is a gap,
   changes nothing, and is not falsely acknowledged.
5. Two commands in one batch carry the same command ID or sequence. The later
   same-batch occurrence cannot commit a second change. Cross-batch command-ID
   history remains deliberately gated to Slice 5.5.
6. Two valid ordered commands target one revision. The first commits; the
   second observes the replacement state and deterministically fails stale.
7. Intake receives 1,025 commands across eligible sessions. Its sealed batches
   contain 1,024 then one command; the reducer never observes an over-budget
   batch and never partially applies one command.
8. Entity revision exhausts. The player replacement is rejected while the
   exact-next final acknowledgement advances in one valid ack-only candidate.
9. Metrics/events accept, drop, or are no-ops. Canonical state and dispositions
   remain byte-for-byte equal across all three sink behaviors.
10. A future caller tries to connect the reducer to online input before the
    Slice 5.5 cross-batch idempotency gate. The component contract and plan
    status make that composition ineligible.

## Decision 1: reducer ownership and commit unit

### Option A: writer-confined owning reducer with per-command atomic commits (recommended)

Add one server-core reducer object constructed from a validated immutable
`CanonicalServerState` and explicit observability. It is non-copyable,
non-movable, and writer-confined like the intake coordinator. Its only mutation
entry accepts one `ServerTickCommandBatch` and returns an owned bounded result.

The reducer verifies the sealed batch invariants supplied by ADR-0026 before
running it: at most 1,024 commands, every eligible tick equals the batch tick,
and ingress ordinals increase strictly. The private batch constructor keeps
untrusted callers from creating alternate shapes. Commands then execute in
accepted tick/ingress order. Each command is its own application transaction: validate
against the state produced by earlier commands, construct a complete candidate,
and replace the owned state only after candidate validation succeeds. A later
command failure does not roll back an earlier committed command.

This matches one reliable operation per application/idempotency unit and makes
same-revision contention deterministic without exposing mutable state.

### Option B: one all-or-nothing tick transaction

Validate and stage every command, then install the entire tick or none of it.
This is strongly atomic at the batch level, but one unrelated bad command would
roll back valid work from other sessions and make the intake batch an accidental
gameplay transaction that the protocol never declared.

### Option C: public pure reducer functions plus caller-owned installation

Return replacement values from free functions and let the composition root
choose when to install them. This is easy to test but creates multiple possible
commit authorities and lets adapters, scripts, or future admin code bypass the
single writer boundary.

## Decision 2: validation order and disposition model

### Option A: closed preflight and per-command validation pipeline (recommended)

Before any mutation, verify the sealed ADR-0026 invariant: no more than 1,024
commands, matching batch and eligible ticks, and strictly increasing ingress
ordinals. Fail closed with no per-command execution if an internal producer
defect is ever observed; do not add a public malformed-batch constructor merely
to exercise an unreachable state.

For each command, evaluate this fixed order:

1. active session exists;
2. session generation matches;
3. sequence is already-finalized, next-contiguous, or a gap;
4. same-batch command ID/sequence has not already appeared;
5. the precondition entity is the session's explicit bound entity;
6. the bound player/entity record exists by the state invariant;
7. expected entity revision matches;
8. expected authority epoch matches; and
9. the approved GDR-0012 domain rule succeeds.

Return a closed typed disposition for the first failing rule. Do not use text,
exceptions, protocol errors, transport actions, disconnects, or callback return
values as reducer outcomes. `observedServerTick` remains diagnostic proposal
metadata and never affects eligibility, validation, or ordering.

### Option B: run domain validation before authority/preconditions

This may produce a more specific gameplay error, but leaks domain information
to unauthorized proposals and wastes work that binding/revision checks could
reject first.

### Option C: collect every validation error

Returning all failures is rich for diagnostics, but adds allocation and makes
the result depend on validation implementation details. One deterministic
first-failure category is sufficient for the initial boundary.

## Decision 3: contiguous finalization and acknowledgement

### Option A: exact-next finalization with atomic acknowledgement (recommended)

When acknowledgement is absent, only sequence 1 is finalizable. Otherwise only
`ack.next()` is finalizable. A sequence at or below the acknowledgement returns
`AlreadyFinalized` without mutation. A greater non-next sequence returns
`SequenceGap` without mutation or acknowledgement.

Once an exact-next command reaches a final accepted or rejected disposition,
stage its acknowledgement advance in the same candidate as any player change.
An accepted command cannot update the player without its acknowledgement, and a
rejected final command cannot acknowledge without preserving the player exactly.
Unknown/old-generation work has no current session record to advance.

### Option B: finalize writer order regardless of sequence gaps

This maximizes throughput, but contiguous acknowledgement cannot cross a gap
without sparse finalized state, which belongs to the Slice 5.5 window design.

### Option C: acknowledge accepted mutations only

This gives acknowledgement a success meaning but directly contradicts ADR-0024
and would make a rejected command permanently block contiguous progress.

## Decision 4: candidate construction and publication boundary

### Option A: copy, replace, validate, then swap one complete state (recommended)

Stage bounded sorted player and session vectors from const views, replace at
most one player and one session entry, and call the accepted complete-state
factory. Install only the successful `CanonicalServerState`. Revision,
acknowledgement, allocation, or invariant failure leaves the prior state intact.

Return dispositions and immutable post-batch state access only. Do not emit
domain state-change records or snapshots in this slice; Slice 5.4 will adapt
from committed reducer results without becoming another mutation path.

### Option B: mutate vector entries in place and roll back on error

This avoids copies but exposes an intermediate state and makes rollback itself
part of correctness. A missed field or exception could leak a partial commit.

### Option C: add public set-player and set-session methods

This makes reducer implementation convenient but creates the bypass mutation
surface ADR-0027 explicitly rejected.

## Decision 5: Slice 5.3 versus Slice 5.5 safety boundary

### Option A: current-epoch and same-batch checks now; online gate remains closed (recommended)

Revision and current stable authority epoch are indivisible entity
preconditions, so Slice 5.3 checks both before any player mutation. Within one
batch, reject repeated command IDs and sequences. Across batches, the accepted
contiguous acknowledgement prevents an already-finalized sequence from running
again, but it does not detect a reused `CommandId` under a new sequence.

Keep the reducer unavailable to online composition until Slice 5.5 adds the
bounded cross-batch command-ID window. Slice 5.5 retains the explicit stale-
epoch regression gate alongside that window, checksum, and resync work; it does
not defer the first mandatory epoch equality check.

This is a clarification of the planned boundary, not a claim of complete
apply-once behavior in Slice 5.3.

### Option B: pull the full idempotency window into Slice 5.3

This closes apply-once behavior immediately, but expands and reopens the
accepted Slice 5.2 state model and consumes a named Slice 5.5 deliverable before
its combined checksum/resync review.

### Option C: defer authority epoch and all duplicate checks to Slice 5.5

This follows the table wording literally but would allow a stale authority
proposal or same-batch retry to mutate state, violating ADR-0006 and ADR-0024.

## Proposed acceptance tests and demo

Subject to owner approval or amendment, Slice 5.3 should add tests named for
these contracts:

1. `valid_bound_next_command_atomically_replaces_player_and_ack`
2. `unknown_or_old_generation_session_cannot_mutate_or_ack_current_state`
3. `unbound_entity_revision_and_epoch_fail_in_closed_validation_order`
4. `rejected_next_command_advances_ack_while_preserving_player_exactly`
5. `already_finalized_and_sequence_gap_commands_change_no_state`
6. `same_batch_duplicate_id_or_sequence_cannot_commit_twice`
7. `two_same_revision_commands_commit_first_and_reject_second_stale`
8. `sealed_intake_batches_preserve_1025_as_1024_then_1_without_partial_command_state`
9. `revision_exhaustion_rejects_player_change_and_atomically_finalizes_ack`
10. `accepted_dropped_and_null_observability_produce_identical_state_and_dispositions`
11. `reducer_exposes_no_mutable_state_wire_engine_socket_script_or_database_surface`
12. `cross_batch_command_id_reuse_remains_an_explicit_slice55_online_gate`

The owner demo should show two sessions, one accepted command, one rejected
next command with acknowledgement-only progress, old-generation and wrong-
entity attempts, first-wins same-revision contention, duplicate/gap behavior,
the 1,024/1,025 boundary, revision exhaustion preserving player state,
and identical results with accepting/dropping/no-op observability.

## Cross-cutting consequences

- **Gameplay and state scope:** reducer architecture does not select the motion
  effect; GDR-0012 separately owns the one per-player entity change. Session
  acknowledgement remains active-generation state, not gameplay state.
- **Security and operations:** closed authorization/precondition failures and
  bounded low-cardinality observations aid diagnosis without identifiers or
  user text. The missing cross-batch ID window is an explicit online blocker,
  not a tolerated runtime risk.
- **Protocol and compatibility:** no schema, frame, capability, or golden message
  changes. Dispositions are server-core domain values and do not become a wire
  result until a later reviewed slice.
- **Scripting, persistence, and replay:** no sink or command-source integration
  lands here. They must later use this same reducer transaction and may not
  install a replacement directly. No persisted format or migration is created.
- **Resync and visibility:** the reducer publishes neither snapshots nor change
  records. Slices 5.4 and 5.5 retain publication, checksum, divergence, and
  resync behavior.
- **Desktop and VR:** both remain identical command producers outside the core;
  no control, pose, engine, or presentation type enters the reducer.

## Consequences if the recommendation is approved

- One writer-confined component becomes the only online-capable canonical
  mutation boundary, though online composition remains gated through Slice 5.5.
- Per-command atomicity preserves unrelated valid work and makes contention
  follow the already accepted writer order.
- Rejected exact-next commands can advance protocol progress without partially
  changing player state or mislabeling acknowledgement as success.
- The first epoch equality check lands with authority validation; complete
  cross-batch apply-once history remains visibly incomplete until Slice 5.5.
- Immutable full-state copies are bounded by 256/256 and favor auditability over
  premature optimization. Measured cost may later justify an internal persistent
  structure without changing the public mutation boundary.

## Failure modes and mitigations

- **A bad batch partially runs:** preflight the complete envelope before command
  evaluation.
- **A rejection accidentally mutates gameplay state:** construct an explicit
  acknowledgement-only candidate and compare the player partition exactly.
- **A stale command commits after an earlier command:** validate every command
  against the latest installed state, not the batch-start snapshot.
- **Acknowledgement becomes success:** use final-disposition naming and test
  rejected-next advancement.
- **Observability changes gameplay:** make attempts bounded/noexcept and ignore
  sink return values for reduction.
- **Public helpers become bypass APIs:** keep replacement construction private
  to the owning reducer and expose immutable state only.
- **Slice 5.3 is mistaken for complete idempotency:** retain the explicit online
  gate and named cross-batch reuse test until Slice 5.5.
- **Reducer settles movement by convenience:** require separate approval of
  GDR-0012 and keep production movement deferred to GDR-0004.

## Review and replacement triggers

Reopen this ADR if:

- command outcomes need all-or-nothing multi-command transactions;
- session sequence gaps must finalize without a bounded sparse window;
- Slice 5.5 cannot provide cross-batch `CommandId` apply-once behavior without
  changing the mutation transaction;
- authority epochs become mutable in this phase;
- full-state bounded copies fail measured writer budgets;
- publication requires mutable reducer internals rather than committed results;
- restore/replay needs a second canonical installation path; or
- GDR-0012 selects behavior incompatible with one player/session replacement.

## Owner approval

Approved by the project owner in the 2026-08-28 working session: Option A for
Decisions 1 through 5, the Slice 5.5 boundary clarification, and the proposed
acceptance tests.

The owner independently approved GDR-0012 Decisions 1 through 4 and its behavior
tests. This approval authorizes only the Slice 5.3 reducer, validation,
disposition, acknowledgement, observability, and focused tests described here.
Online composition remains prohibited until Slice 5.5 adds the bounded
cross-batch `CommandId` window.

## 2026-09-03 Phase 8 amendment

ADR-0051 replaces exact entity-revision equality only for the two provisional
Phase 7 spatial intent bodies. They remain session/entity/epoch authorized and
reliable-sequence ordered. Future command bodies retain the original exact
revision rule unless separately approved.
