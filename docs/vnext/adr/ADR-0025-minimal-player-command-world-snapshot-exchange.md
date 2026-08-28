# ADR-0025: Minimal player command and world-snapshot exchange

Status: **Accepted**

Date opened: 2026-08-27

Date approved: 2026-08-27

Decision owner: project owner

Needed by: Phase 4 Slice 4.5

Companion gameplay record:
[`GDR-0011`](../gdr/GDR-0011-phase4-minimal-player-exchange-semantics.md)

## Decision questions

How should the first concrete `T3RO` and `T3LS` roots compose the accepted
ADR-0024 headers with typed bodies; which collection and semantic limits apply;
where should established-session and snapshot-timeline validation live; and how
should a fake peer exercise negotiation, typed authentication, and framed state
exchange over the existing in-memory link without introducing sockets,
transport-library behavior, a reducer, or OpenMW?

These are protocol architecture, state-boundary, evolution, and test-harness
decisions. The command meaning, authority, and snapshot state scope are proposed
separately in GDR-0011. Production schemas, codecs, session exchange APIs, and
fake-peer behavior must not land until both records are approved or amended by
the project owner.

## Decision summary

The project owner approved Option A for Decisions 1 through 5 on 2026-08-27:

1. add closed typed body unions to complete `T3RO` and `T3LS`, with no opaque
   bytes, dynamic map, nested FlatBuffer, or generated view in public APIs;
2. allow exactly one motion-intent body and require its entity precondition,
   while bounding each snapshot to at most 256 fully initialized spatial
   entries;
3. keep byte validation pure, put established-session context guards at the
   role-specific session boundaries, and make client snapshot replacement an
   atomic latest-tick/ack/body update;
4. compose a reusable deterministic fake peer in `tes3mp_test_support` from the
   existing sessions, codecs, and `InMemoryDuplexLink`, using typed Phase 4
   authentication events and no simulated credential wire protocol; and
5. land current/previous-minor golden fixtures, complete truncation/mutation/
   lifetime tests, and decoder registration now, while leaving sanitizer fuzz
   expansion and the complete decoder/corpus audit to Slice 4.6.

The recommendation completes the Phase 4 exchange without allocating session
or player identities in authentication, admitting commands into a writer,
simulating movement, selecting real interest sets, or creating a transport
channel policy.

## Existing constraints

1. ADR-0004 requires separately size-prefixed, file-identified FlatBuffer roots,
   verifier-first decoding, schema-specific limits before domain allocation,
   and immediate conversion to owned values.
2. ADR-0021 fixes `ReliableOperation` at one 16 KiB frame and
   `LatestWinsSnapshot` at one 64 KiB frame, with closed class/kind mapping and
   exact frame-length validation.
3. ADR-0024 fixes one reliable header, zero or one entity precondition, and one
   typed command body; snapshot headers contain target session, generation,
   server tick, and optional contiguous-finalized command progress.
4. Session/generation and snapshot-timeline checks do not belong in the byte
   decoder. Command deduplication, sequence admission, writer ordering,
   revision/epoch admission, and canonical mutation remain Phase 5 work.
5. Authentication produces only an opaque routing principal. It does not
   allocate `SessionId`, `PlayerId`, or `EntityId`, bind a player entity, or
   authorize a command.
6. Client-authored canonical snapshots, absolute position writes, and cell
   transitions are forbidden. The server remains the only canonical writer.
7. Generated schema types stay private to `tes3mp_protocol`; session and test
   targets consume only owned project values and structured errors.
8. Phase 4 uses typed in-memory authentication events. Credential and
   authentication-result wire schemas, encryption, resumption, and channel
   mapping remain Phase 6 work.

## Representative scenarios

1. A fake client completes framed hello negotiation, succeeds through the fake
   authentication provider, sends one framed reliable operation, and receives
   one framed latest-wins snapshot using only the in-memory link.
2. The input buffer is overwritten immediately after either decode. The owned
   operation or snapshot and every contained entry remain unchanged.
3. A reliable operation has the expected session and generation but no entity
   precondition. Structural decoding succeeds only far enough to return the
   closed missing-precondition error; no server-session delivery occurs.
4. A frame claims 257 snapshot entries. The decoder rejects the count before
   allocating the owned entry vector and preserves the client's prior confirmed
   snapshot.
5. A valid operation from an old session generation is structurally decoded but
   rejected at the server-session boundary before delivery.
6. A valid snapshot for an old session generation, an older tick, a regressing
   acknowledgement, or contradictory content at the same tick cannot replace
   the client's confirmed state.
7. A newer snapshot may advance tick and contiguous-finalized acknowledgement
   together, including when the acknowledged command was rejected. The protocol
   does not label the outcome accepted or durable.
8. A malformed body discriminator, zero strong identity/counter, invalid cell
   discriminator, length mismatch, truncation, trailing bytes, or class/kind
   mismatch yields a structured error and no partial value.
9. Authentication succeeds but the test composition has not supplied an
   explicit session binding. State exchange remains unavailable; the principal
   is not silently reused as a session or player identity.
10. A future gameplay decision changes locomotion or interest semantics. A new
    minor/capability/body discriminator can replace the first body without
    changing the established envelope metadata or exposing generic payloads.

## Decision 1: complete-root body composition

### Option A: closed typed body unions in the existing roots (recommended)

Complete `T3RO` with its accepted reliable header, optional precondition, and a
required closed command-body union whose first member is the GDR-0011 motion
intent. Complete `T3LS` with its accepted snapshot header and a required closed
snapshot-body union whose first member is a bounded spatial-world view.

The public API owns a closed project variant and domain values. Unknown or
missing body discriminants fail semantic conversion. No raw payload vector,
generic key/value field, reflection object, nested FlatBuffer, or generated
accessor crosses the codec boundary.

This follows ADR-0024's typed-composition contract and gives later evolution an
explicit discriminator plus minor/capability review point.

### Option B: make each first body a direct required table

Put the motion body and entry vector directly on their roots without a union.
This is smaller and simpler for one body, but changing the command or snapshot
domain later requires a new root/message kind or parallel optional tables and
makes the first provisional shape harder to retire cleanly.

### Option C: carry a numeric body kind and opaque bytes

This is superficially flexible but creates a nested decoder, second size limit,
and generic dispatch boundary. It conflicts with ADR-0004 and ADR-0024 and is
not viable without reopening both records.

## Decision 2: body cardinality and structural limits

### Option A: one required command target and at most 256 snapshot entries (recommended)

The first motion command requires exactly one `EntityPrecondition`; the target
entity is not duplicated in its body. Absence is a structured semantic error,
and a mismatched duplicate identity is impossible by construction.

The first world-snapshot body contains zero through 256
`SpatialEntitySnapshot` entries, strictly sorted by `EntityId` with no
duplicates. Empty is valid because a session may currently observe no spatial
entities. Each entry is fully initialized and repeats no root `ServerTick`;
the root tick is the publication tick while each entry retains the canonical
tick at which that entity state was confirmed, as required by ADR-0016.

The 256-entry bound is deliberately below what fits in the 64 KiB class budget,
leaving verifier/table-work and future additive-field headroom. Real interest,
partitioning, and resynchronization may lower the operational count or add
distinct messages later; they cannot raise this decoder bound silently.

### Option B: exactly one snapshot entry

This minimizes Phase 4 but cannot demonstrate an empty view, deterministic
ordering, duplicate rejection, or a minimal multi-entity world view. The first
golden root would also become an awkward compatibility promise for Phase 7.

### Option C: rely only on the 64 KiB frame budget

This avoids choosing a count but violates the schema-specific allocation/work
limit required by ADR-0004 and the program-wide bounded-collection rule.

## Decision 3: exchange validation and atomic state ownership

### Option A: pure codec plus role-specific exchange guards (recommended)

The protocol codec verifies structure and constructs owned values without
history. A server-session exchange guard accepts a reliable operation only when
the session is established and its `SessionId`/`SessionGeneration` match an
explicit composition-supplied binding. It does not deduplicate, advance command
sequence, validate entity authority, or mutate canonical state.

A client-session exchange guard accepts a snapshot only when the session is
established, target context matches, tick is newer (or an identical duplicate),
and acknowledgement progress does not regress. It validates the entire body
before atomically replacing its owned confirmed snapshot. Contradictory
same-tick content returns a producer-defect result and preserves prior state.

Phase 4 composition supplies `SessionId` explicitly after typed authentication
succeeds. Neither the authentication provider nor `PrincipalId` allocates or
implies that identifier. Phase 6 may carry the binding in an approved encrypted
authentication/session-result message without changing these guards.

### Option B: make the protocol decoder track session and timeline state

This combines errors in one place, but makes malformed-input fuzzing depend on
mutable lifecycle state and violates ADR-0024's validation ownership.

### Option C: validate only inside the fake-peer test

This proves one happy path but leaves production session boundaries unable to
reject old-generation or stale snapshots and would not satisfy the Slice 4.5
deliverable.

## Decision 4: in-memory composition boundary

### Option A: reusable fake peer composed from existing owned boundaries (recommended)

Add a deterministic `tes3mp_test_support` fixture that:

1. frames and sends `ClientHello` through `InMemoryDuplexLink`;
2. decodes, negotiates, frames, and returns `ServerHello`;
3. advances the existing client/server state machines through their typed
   Phase 4 authentication events and fake provider;
4. supplies one explicit common session binding after both sides establish;
5. frames and sends one owned reliable operation; and
6. returns one framed owned snapshot through the client exchange guard.

The fixture exposes the trace and terminal owned values for assertions. It adds
no socket, background thread, wall clock, transport-library type, credential
wire schema, automatic retry, channel policy, or server reducer.

### Option B: exercise codecs directly without the session machines

This is a useful codec test but does not demonstrate the Phase 4 outcome that a
peer negotiates, authenticates, and then exchanges state.

### Option C: add a general transport abstraction and asynchronous peer loop

This could resemble the eventual runtime, but it prematurely decides Phase 6
channel, backpressure, close, and scheduling behavior.

## Decision 5: evolution and Slice 4.5/4.6 verification boundary

### Option A: complete codec evidence now; exhaustive fuzz expansion in 4.6 (recommended)

Slice 4.5 checks in schemas, exact generated sources, current and previous-minor
golden messages, owned codecs, structured semantic errors, every-truncation and
targeted mutation tests, allocation-before-bound evidence, buffer-overwrite
lifetime tests, session-context/timeline tests, and the deterministic fake-peer
exchange. Both new production decoders are registered with corpus seeds and the
runtime-safety inventory in this slice.

Slice 4.6 expands both registered targets with property generation, systematic
mutation, sanitizer-backed fuzz smoke, corpus minimization/retention, and the
phase-wide audit that every production decoder is registered. This split keeps
4.5 independently safe and prevents 4.6 from becoming deferred basic decoder
correctness.

### Option B: defer golden, mutation, and decoder registration to Slice 4.6

This makes 4.5 smaller but would land new untrusted-input production decoders
without the program-required same-slice compatibility and runtime-safety hooks.

### Option C: complete all Phase 4 fuzz/property work in Slice 4.5

This is safe but collapses Slice 4.6 into 4.5, making review and demo evidence
less focused without changing the phase gate.

## Proposed acceptance tests and demo

Subject to owner approval or amendment, Slice 4.5 should add tests named for
these contracts:

1. `reliable_operation_round_trips_one_typed_motion_intent`
2. `reliable_operation_requires_entity_precondition`
3. `snapshot_round_trips_empty_single_and_maximum_sorted_views`
4. `snapshot_rejects_257_entries_and_duplicate_or_unsorted_entities_before_owned_allocation`
5. `unknown_or_missing_body_discriminants_fail_without_partial_values`
6. `zero_identity_counter_and_invalid_cell_values_fail_semantic_conversion`
7. `decoded_operation_and_snapshot_survive_input_buffer_overwrite`
8. `old_generation_operation_is_not_delivered_by_server_session_guard`
9. `old_generation_stale_tick_regressing_ack_and_contradictory_same_tick_preserve_client_state`
10. `newer_snapshot_atomically_replaces_tick_ack_and_owned_body`
11. `authentication_principal_does_not_imply_session_or_player_binding`
12. `fake_peer_negotiates_authenticates_and_exchanges_framed_state_in_memory`
13. `fake_peer_uses_no_socket_openmw_or_transport_library_type`
14. `current_and_previous_minor_golden_roots_follow_schema_policy`
15. `new_decoders_are_registered_with_runtime_safety_and_seed_corpora`

The owner demo should show the framed hello/operation/snapshot trace, command
precondition and absence of client-authored position/cell, an empty and
multi-entity session-targeted view, old-generation and stale-snapshot rejection,
the 256/257 boundary, buffer ownership, and target/include evidence proving the
exchange remains in memory and engine/transport-library independent.

## Consequences if the recommendation is approved

- Phase 4 gains complete typed non-control roots and a reusable deterministic
  peer exchange without claiming a network runtime exists.
- The first reliable body is a semantic proposal, not a canonical-state write;
  its actual validation and simulation remain later owner-gated work.
- Snapshot contents are explicitly selected per target session; no global-world
  broadcast or interest algorithm is inferred from server authority.
- A 256-entry protocol ceiling makes allocation and verifier work testable while
  reserving headroom under the existing 64 KiB frame budget.
- Session exchange code gains minimal owned confirmed-snapshot state, but the
  server still has no canonical store or reducer.
- The first complete roots become vNext golden compatibility artifacts, so any
  incompatible pre-release revision needs an explicit golden explanation and
  minor/capability treatment.

## Failure modes and mitigations

- **Motion intent becomes a client position write:** body contains no position,
  cell, revision, epoch, or writer stamp; server validation remains mandatory.
- **Precondition is treated as authority:** session guard checks context only;
  Phase 5/7 must validate controlled-entity binding, revision, and epoch.
- **Snapshot is mistaken for the whole world:** name and documentation define a
  target-session selected view; GDR-0001/0003 later define actual interest.
- **Count bound is bypassed by frame size:** check count before allocating the
  owned vector, then retain the independent 64 KiB frame cap.
- **Same-tick arrival order changes state:** accept only identical duplicates;
  contradictory bodies preserve prior state and emit a typed producer defect.
- **Authentication allocates gameplay identity:** keep explicit composition
  binding separate from the provider result and test the separation.
- **Test fixture becomes transport:** keep it under test support, synchronous,
  deterministic, and composed only from owned link/session/protocol APIs.
- **Slice 4.6 is used to defer basic safety:** require golden, truncation,
  targeted mutation, lifetime, allocation-bound, and decoder registration in
  4.5 before implementation review.

## Review and replacement triggers

Reopen this ADR if:

- GDR-0011 selects a command or snapshot shape incompatible with the proposed
  typed bodies;
- a 256-entry snapshot cannot stay below the verified 64 KiB budget with the
  accepted schema;
- current/previous-minor evolution cannot represent the first body safely;
- role-specific guards require transport state or canonical reducer knowledge;
- Phase 6 authentication cannot deliver an explicit session binding without
  changing the provider boundary; or
- fuzzing shows the verifier, table-count, allocation, or conversion limits are
  not enforceable before session delivery.

## Owner approval

Approved by the project owner in the 2026-08-27 working session: Option A for
Decisions 1 through 5 without amendment.

The owner independently approved GDR-0011 Decisions 1 through 4. This approval
does not authorize any behavior excluded by either record.
