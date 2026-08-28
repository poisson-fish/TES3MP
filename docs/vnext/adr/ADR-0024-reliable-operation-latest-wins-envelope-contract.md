# ADR-0024: Reliable-operation and latest-wins envelope contract

Status: **Proposed**

Date opened: 2026-08-27

Date approved: pending

Decision owner: project owner

Needed by: Phase 4 Slice 4.4

## Decision questions

Which direction and typed boundary does each non-control message class serve;
which session, command, acknowledgement, tick, revision, and authority values
belong in its envelope; where are cross-message ordering and context checks
performed; and how can Slice 4.4 define stable envelope metadata without
inventing a generic byte payload or prematurely deciding the Slice 4.5 player
command and world-snapshot bodies?

These are protocol architecture, session-context, idempotency, ordering, and
evolution decisions. Production envelope APIs or schemas must not land until
the project owner approves the options below.

## Recommendation summary

Recommend Option A for Decisions 1 through 5:

1. use separate directional typed compositions: client-to-server reliable
   operations and server-to-client latest-wins canonical snapshots, with no
   generic opaque body;
2. give each reliable operation one required `ClientCommandHeader`, at most one
   optional `EntityPrecondition`, one future typed body, and no batch or
   server-assigned admission data;
3. give each snapshot a required `SessionId`, `SessionGeneration`, and
   `ServerTick`, plus an optional highest-contiguous-finalized
   `CommandSequence` acknowledgement, while keeping revision and authority
   epoch on each future snapshot entry;
4. keep structural envelope validation in protocol code, session/timeline
   validation in the role-specific session boundaries, and command
   idempotency/revision/epoch admission in the server core; and
5. implement only owned envelope-header values and composition contracts in
   Slice 4.4, then add the `T3RO`/`T3LS` file-identified FlatBuffer roots with
   their first concrete typed bodies in Slice 4.5 under its separate behavior
   review.

This recommendation preserves ADR-0004's typed verifier-first boundary without
adding a `none` operation, opaque nested bytes, a generic variant, or a
player-movement rule merely to make a metadata-only message encodable. It does
not define gameplay command contents, movement semantics, snapshot interest or
collection limits, canonical reducers, transport channels, resumption behavior,
or presentation policy.

## Existing constraints

1. Reliable apply-once operations and latest-wins sampled state are different
   message classes with different queue and delivery behavior.
2. A reliable operation retains `CommandId` independently from its ordered
   `CommandSequence`; transport retransmission does not provide application
   idempotency.
3. Client command context includes `SessionId`, `SessionGeneration`,
   `CommandSequence`, `CommandId`, and the observed `ServerTick`.
4. Entity preconditions, where the owning domain requires one, include
   `EntityId`, expected `EntityRevision`, and expected `AuthorityEpoch`.
5. Writer-assigned eligible tick and `IngressOrdinal` are server admission
   results. A client cannot place a `WriterAdmissionStamp` on the wire.
6. A canonical spatial entry includes authoritative `ServerTick`, `EntityId`,
   `EntityRevision`, `AuthorityEpoch`, transform, and linear velocity. Clients
   do not submit canonical snapshots.
7. Authentication establishes only a routing principal. It does not create a
   player, entity, command authority, or canonical state.
8. Every FlatBuffer message root has its own file identifier, explicit field
   IDs, project-owned semantic limits, and an owned conversion boundary.
   Generated views and generic packet bytes do not cross that boundary.
9. The existing frame budgets remain 16 KiB for one reliable-operation frame
   and 64 KiB for one latest-wins-snapshot frame. Transport mapping and queue
   budgets remain Phase 6 work.
10. Gameplay movement, cell visibility, reconciliation, and reconnect behavior
    remain GDR-0001/GDR-0003/GDR-0004 work. Slice 4.4 cannot decide them through
    an expedient payload layout.

## Representative scenarios

1. A client retries one reliable operation after a connection interruption.
   Its command ID and command sequence remain explicit application metadata;
   transport reliability alone cannot cause a second commit.
2. Two reliable operations arrive in order but the second targets a stale
   entity revision or authority epoch. The envelope decodes, while server-core
   admission rejects the stale precondition without partially committing either
   operation.
3. A non-entity operation has no `EntityPrecondition`. Absence is explicit and
   cannot be represented by zero-valued entity, revision, or epoch sentinels.
4. A malicious client supplies a server tick or ingress ordinal as if it were
   writer-assigned. The reliable wire composition has no such fields.
5. Before any client command is finalized, a server snapshot has no command
   acknowledgement. After commands 1 and 2 have final accepted-or-rejected
   dispositions, the acknowledgement is 2 even if command 2 was rejected.
6. A snapshot for an old session generation arrives after a replacement
   connection is active. The client-session boundary rejects it before
   changing confirmed or presentation state.
7. Snapshots for ticks 20 and 22 arrive before tick 21. The client may accept 22
   as newer latest-wins state and discard 20/21; it does not need an unrelated
   snapshot sequence counter.
8. Two encodings for the same session and server tick arrive. They must be
   equivalent at the envelope-ordering level; a contradictory same-tick body is
   a producer defect, not a later-arrival winner.
9. Slice 4.5 needs a first player command. It adds a reviewed typed body and a
   complete root rather than placing nested FlatBuffer bytes or an unbounded
   extension map inside the Slice 4.4 metadata.
10. A future presentation-only pose sample needs latest-wins delivery. It uses
    its own typed message kind and sample metadata; it is not mislabeled as a
    canonical server world snapshot.

## Decision 1: direction and typed payload boundary

### Option A: directional typed compositions without an opaque body (recommended)

Treat `ReliableOperation` as the client-to-server apply-once command class and
`LatestWinsSnapshot` as the server-to-client canonical snapshot class for the
initial milestone. Each eventual message root contains one closed typed body
defined by its owning slice. Slice 4.4 defines their shared header/composition
contract but does not add a raw byte vector, nested FlatBuffer, dynamic map, or
open type registry.

Future server-to-client reliable results, resync records, client-to-server
presentation samples, or other directions receive distinct typed message kinds
when their owning slices define them. They do not overload these first two
message meanings.

### Option B: bidirectional generic envelopes with a numeric payload kind and bytes

Use the same two roots in both directions and carry a numeric subtype plus an
opaque byte vector. This appears extensible, but it creates a second dispatch
and bounds boundary, permits nested payload parsing, weakens generated schema
verification, and lets direction mistakes reach later handlers.

### Option C: one central union for every reliable and sampled message

Put every future command, result, canonical snapshot, and presentation sample
in one or two global FlatBuffer unions. This remains typed, but turns unrelated
domain additions into central schema edits and makes direction, capability,
and evolution policy harder to review independently.

## Decision 2: reliable-operation metadata and atomic unit

### Option A: one header, optional precondition, one typed body (recommended)

One reliable operation contains:

- one required `ClientCommandHeader` with session ID, session generation,
  command sequence, command ID, and observed server tick;
- zero or one `EntityPrecondition`, present only when the owning semantic
  command requires an entity revision and authority epoch; and
- exactly one concrete typed command body once the owning slice defines it.

The envelope contains no `PrincipalId`, `PlayerId`, transport/connection handle,
writer admission stamp, server-assigned eligible tick, ingress ordinal,
canonical result, or generic metadata. One frame is one application idempotency
and validation unit. Retrying it retains the command header; a different logical
operation requires a different command ID and command sequence.

### Option B: bounded batch of operations in one reliable frame

Carry a vector of command headers, preconditions, and bodies. This reduces
framing overhead, but requires partial-versus-atomic batch semantics, per-entry
errors, mixed authority handling, replay ordering inside the batch, and another
collection bound before the first reducer exists.

### Option C: infer session and order from the connection

Carry only a command ID or body and let the active connection supply session,
generation, and order. This is compact, but delayed old-connection work,
resumption, replay, idempotency, and cross-layer tests can no longer prove which
logical session generation originated the operation.

## Decision 3: snapshot context, recency, and acknowledgement

### Option A: session/generation/tick plus contiguous finalized acknowledgement (recommended)

One canonical snapshot header contains:

- the target `SessionId` and `SessionGeneration`;
- the authoritative `ServerTick` at which the snapshot was published; and
- an optional `CommandSequence` meaning the highest contiguous command sequence
  in that same generation for which the server has reached a final accepted or
  rejected disposition.

Absence means no command has reached a final disposition. An acknowledgement
does not claim acceptance, durability, or a particular entity revision; the
typed operation result and authoritative snapshot body provide those facts.
Within one session generation, published acknowledgements cannot regress.

`ServerTick` is the latest-wins ordering key for this canonical snapshot kind.
The server publishes at most one canonical snapshot per target session per
tick. A duplicate for the same tick must be envelope-equivalent; contradictory
same-tick content is rejected/observed as a producer defect rather than ordered
by arrival. No `SnapshotSequence` type is added without a demonstrated need.

Each future canonical entity entry retains its own entity ID, revision, and
authority epoch. Those values are not hoisted into the message header or
treated as one global world revision.

### Option B: add a dedicated snapshot sequence and acknowledgement list

Add a new per-session `SnapshotSequence` and a bounded vector of acknowledged
command IDs/results. This can distinguish multiple publications per tick and
report sparse progress, but adds new allocation, counter lifetime, overflow,
resumption, list truncation, and reconciliation semantics without an initial
consumer.

### Option C: rely on connection context and acknowledge only by entity revision

Omit session/generation and command progress, using the active connection plus
entity revisions. This reduces bytes, but cannot reject a valid delayed snapshot
from a replaced generation or correlate client prediction with finalized
command processing.

## Decision 4: validation ownership and atomic delivery

### Option A: layered pure validation before any state change (recommended)

The protocol target owns representational construction and, once roots exist,
complete structural verification and owned semantic conversion. It does not
retain previous sequence, tick, revision, or epoch state.

The server session/admission boundary compares reliable session and generation
context before queuing work. The client-session boundary compares snapshot
session/generation, server tick, and acknowledgement progress before replacing
confirmed state. Phase 5 server-core admission owns command ID deduplication,
sequence progression, entity revision, authority epoch, writer tick, and
ingress ordinal.

Each layer returns a closed enum/numeric error and commits no partial state on
failure. Protocol decode success is never itself command acceptance or snapshot
application.

### Option B: make the protocol decoder stateful

Give the decoder expected session, sequence, revision, and tick history. This
can return one combined result, but couples byte parsing to lifecycle/canonical
state and makes replay, fuzzing, and buffer-lifetime tests depend on mutable
session context.

### Option C: decode directly inside reducers and presentation state

Let the server reducer or client presentation path consume generated views and
validate while applying. This reduces intermediate values but violates the
owned decode boundary and makes partial mutation on malformed or stale input
substantially harder to exclude.

## Decision 5: Slice 4.4/4.5 schema and evolution boundary

### Option A: header values now, complete typed roots with the first bodies (recommended)

Slice 4.4 adds project-owned, fully initialized reliable-operation and
latest-wins-snapshot header values plus composition and boundary tests. It adds
no encodable empty operation, no generated schema, and no new decoder.

Slice 4.5, after its separate behavior review, adds the first concrete typed
command and snapshot bodies and completes separately size-prefixed,
file-identified FlatBuffer roots using `T3RO` and `T3LS`. The roots use explicit
field IDs, remain private to `tes3mp_protocol`, and map to owned values. Their
body collection and field limits land with the body schemas that can justify
them. The existing 16/64 KiB class budgets remain hard upper bounds.

Later body additions use a new message kind or a negotiated minor/capability as
required by compatibility semantics. Unknown typed body discriminants fail;
optional fields alone never activate behavior. Golden fixtures begin with the
first complete roots, so no meaningless empty-message fixture becomes a
compatibility promise.

### Option B: add empty encodable roots in Slice 4.4

Create `T3RO` and `T3LS` roots containing only headers, then extend them with
optional bodies in Slice 4.5. This yields immediate codecs, but makes an
operation with no operation and a snapshot with no state valid initial wire
messages. Later rejecting those old forms would contradict the compatibility
promise unless no-op semantics or a version gate is invented now.

### Option C: add roots with generic body bytes in Slice 4.4

Create complete roots now by embedding a bounded byte vector. This avoids an
empty initial form but permanently adds generic/nested parsing and a second
identifier/bounds system contrary to the restricted FlatBuffers profile.

## Proposed acceptance tests and demo

If Option A is approved, Slice 4.4 should add tests named for these contracts:

1. `reliable_header_retains_session_generation_sequence_id_and_observed_tick`
2. `reliable_precondition_is_explicitly_optional`
3. `one_reliable_envelope_is_one_apply_once_unit`
4. `reliable_envelope_exposes_no_writer_admission_or_canonical_result`
5. `snapshot_header_binds_target_session_generation_and_server_tick`
6. `snapshot_ack_is_absent_before_any_finalized_command`
7. `snapshot_ack_means_contiguous_finalized_progress_not_acceptance`
8. `snapshot_recency_uses_server_tick_without_snapshot_sequence`
9. `entity_revision_and_authority_epoch_remain_entry_scoped`
10. `envelope_headers_have_no_generic_bytes_strings_maps_or_generated_views`
11. `envelope_headers_compile_without_openmw_transport_flatbuffers_or_test_support`
12. `identical_metadata_inputs_produce_identical_owned_values`

The implementation demo should show one reliable header with and without an
entity precondition, explicit separation of command sequence and command ID,
absence of writer-owned fields, snapshot acknowledgement absence/progress, and
compile-time/public-header evidence that no generic payload or generated view
landed. Owner demo acceptance remains required before Slice 4.4 becomes
**Implemented**.

## Consequences of the recommendation

- Slice 4.4 stays behavior-neutral and does not force a placeholder gameplay
  command or empty wire-message compatibility contract.
- The first complete non-control golden messages arrive in Slice 4.5, where
  their typed bodies, bounds, and in-memory exchange can be reviewed together.
- Explicit session generation and command progress cost a small fixed amount of
  metadata but make delayed/replaced connection behavior independently
  testable.
- One reliable operation per frame simplifies idempotency, rejection, replay,
  and later transaction boundaries at the cost of modest framing overhead.
- `ServerTick` supplies canonical snapshot recency without another counter, but
  a demonstrated need for multiple distinct canonical publications per target
  session per tick would reopen this decision.
- Presentation-only samples and server-to-client reliable results remain
  separate future message kinds instead of overloading the initial roots.

## Failure modes and mitigations

- **Envelope is mistaken for authority:** it carries proposal/session context
  only; normal authority, revision, epoch, and reducer validation remain
  mandatory.
- **Transport reliability is mistaken for apply-once behavior:** retain command
  ID and sequence in every operation and deduplicate in server-core admission.
- **Rejected command is reported as accepted:** define acknowledgement as final
  processing progress only; use typed results/canonical state for outcome.
- **Old-generation message mutates current state:** bind both directions to
  session generation and validate it before delivery/application.
- **Client forges writer order:** omit writer admission tick and ingress ordinal
  from the client composition entirely.
- **Same-tick snapshots race:** require one publication per target session/tick
  and treat contradictory duplicates as a producer defect, not arrival order.
- **Empty placeholder becomes protocol debt:** add no complete root until a
  reviewed typed body exists.
- **Generic body bypasses schema limits:** prohibit opaque bytes, nested
  FlatBuffers, dynamic maps, and open registries in these message roots.
- **Future world snapshot exceeds one frame:** owning cell/interest/resync slices
  define bounded partitioning or distinct reliable resync messages; they do not
  silently raise the 64 KiB class budget.

## Review and replacement triggers

Reopen this ADR if:

- one logical reliable operation needs atomic multi-command transaction
  semantics that cannot fit one typed body;
- a canonical server snapshot must publish more than once per target session
  per tick;
- sparse acknowledgements or a dedicated snapshot sequence become necessary;
- command retry/resumption requires a different command ID or sequence scope;
- a future message direction cannot be expressed safely with a distinct typed
  kind;
- `T3RO`/`T3LS` cannot evolve under the current/previous-minor policy; or
- measured payload scale requires a changed frame budget or a common bounded
  partitioning protocol.

## Owner approval

Pending. The project owner must approve or amend Decisions 1 through 5 before
production envelope headers, schemas, codecs, or behavior land.
