# ADR-0026: Phase 5 writer command intake and ordering

Status: **Proposed**

Date opened: 2026-08-27

Date approved: pending

Decision owner: project owner

Needed by: Phase 5 Slice 5.1

## Decision questions

What value crosses from an established session into the authoritative server
core; where is the single-writer cutoff; how are eligible ticks and ingress
ordinals assigned; which pending and per-tick limits apply; what happens when a
limit is reached; and which component composes the existing fixed-tick scheduler
with bounded command intake?

These are architecture, authority-boundary, resource-safety, and deterministic-
ordering decisions. Production intake, ordering, or coordinator code must not
land until the project owner approves or amends this record. This record does
not decide player movement validation, cell transitions, canonical state,
reducer behavior, command outcomes, or reconnect gameplay; those remain in
their owning slices and GDRs.

## Decision summary

No option is accepted yet. The recommendation is Option A for Decisions 1
through 5:

1. convert a validated session operation into an owned, typed server-core
   command proposal instead of retaining a wire root or adding a generic
   payload;
2. require writer-context submission, seal the pending prefix at each tick
   cutoff, and assign eligible tick plus global ingress ordinal only while the
   writer drains that prefix;
3. start with hard v1 ceilings of 4,096 pending commands globally, 128 pending
   commands per `(SessionId, SessionGeneration)`, and 1,024 admitted commands
   per tick;
4. reject a full pending queue immediately with a typed result, defer an
   accepted FIFO suffix to later ticks, and never drop, overwrite, coalesce, or
   disconnect silently; and
5. add a dedicated server-core intake/tick coordinator that composes the
   existing scheduler and produces ordered tick batches without introducing
   canonical state or a reducer in Slice 5.1.

## Existing constraints

1. ADR-0013 fixes a 30 Hz logical tick, at most four due ticks per scheduler
   pump, no skipped or variable-delta tick, writer-owned eligible ticks and
   ingress ordinals, and `(eligible_tick, ingress_ordinal)` reducer order.
2. ADR-0013 prohibits client timestamps, callback races, wall time, hash
   iteration, and worker scheduling from becoming canonical tie-breakers.
3. ADR-0006 keeps the server as the only canonical writer. A client supplies
   semantic intent, not canonical state, and cannot write another player.
4. ADR-0024 separates reliable apply-once operations from latest-wins samples.
   Reliable operations carry command identity, sequence, session context, and
   an entity precondition but no writer-assigned ordering fields.
5. ADR-0025 and GDR-0011 permit only a narrow velocity intent in Phase 4 and
   leave command admission, deduplication, revision/epoch validation, and
   canonical mutation to Phase 5.
6. Slice 5.1 owns scheduling, bounded intake, and total order. Slice 5.2 owns
   canonical state, Slice 5.3 owns validation and atomic reduction, and Slice
   5.5 owns the production canonical checksum.
7. Every queue and per-peer unit of work must be bounded and observable. A
   failed admission cannot partially mutate state or manufacture an
   acknowledgement.

## Representative scenarios

1. Two established sessions submit interleaved commands before one tick. The
   writer seals one prefix and emits the commands in exactly its observed order
   with one eligible tick and strictly increasing ingress ordinals.
2. A command carries an earlier observed server tick than another command but
   reaches the writer later. It remains later; the client observation cannot
   backdate or reorder it.
3. A command is submitted after the current cutoff. It cannot enter the sealed
   batch and becomes eligible no earlier than the next tick.
4. A host stall makes many ticks due. One pump yields at most four sequential
   tick batches, never skips a tick, and applies the per-tick intake ceiling to
   each batch independently.
5. More than 1,024 accepted commands await a tick. The first 1,024 receive that
   tick and ordinals; the remaining FIFO suffix stays bounded and receives a
   later tick when drained.
6. One session generation already has 128 commands pending. Its next submission
   receives a typed per-session-limit result while other sessions can continue
   submitting if the global queue has space.
7. The global queue contains 4,096 accepted commands. A further submission
   receives a typed global-limit result; no accepted command is removed and no
   ordinal is consumed by the rejected attempt.
8. An ingress ordinal cannot advance. The coordinator returns a terminal typed
   error without emitting a partially stamped batch or wrapping to a reusable
   ordinal.
9. The same clock observations and writer submission sequence run twice. The
   complete tick/ordinal/command trace is byte-for-byte identical without
   relying on unordered-container iteration.
10. A later Phase 6 transport uses worker callbacks. It owns any concurrent
    mailbox and marshals decoded, owned proposals into the writer context; it
    cannot call a concurrent mutation or assign canonical order itself.

## Decision 1: session-to-core command boundary

### Option A: owned typed server-core proposal (recommended)

Convert an established, context-guarded `ReliableOperation` at the composition
boundary into a server-core-owned value containing the source `SessionId` and
`SessionGeneration`, command sequence and ID, observed server tick, entity
precondition, and typed player motion intent. The server core never stores a
frame, FlatBuffer view, generated type, encoded byte vector, or generic payload.

The value is still a proposal. Its presence in the intake queue proves neither
player ownership nor revision, epoch, idempotency, sequence, magnitude, or
gameplay validity. Slice 5.3 performs those checks through the one reducer path.

This gives the writer an owned lifetime and keeps packet evolution outside the
canonical mutation boundary. It temporarily has one closed command body because
only GDR-0011 is approved; later commands extend a reviewed typed variant rather
than adding opaque dispatch.

### Option B: queue the protocol `ReliableOperation`

Pass the owned Phase 4 protocol value directly into server-core intake. This is
the smallest implementation today and the value already owns its fields, but it
makes the protocol envelope the server domain command API. Later replay,
scripting, and administration would either manufacture protocol objects or need
a second command path, conflicting with the Phase 5.6 boundary.

### Option C: generic command template, callback, or opaque payload

Build a type-erased or templated intake that accepts arbitrary bodies. This can
make scheduling tests independent of the first command, but it postpones the
actual server command boundary and risks unbounded callbacks, packet-shaped
payloads, or a speculative abstraction with no canonical validation contract.

## Decision 2: writer cutoff and total-order assignment

### Option A: writer-context submit, sealed prefix, drain-time stamps (recommended)

Only the authoritative writer context may call `submit`. Each scheduled tick
seals the pending prefix visible at its cutoff. The writer drains up to the
per-tick budget from that prefix in FIFO observation order and atomically assigns
each drained command a `WriterAdmissionStamp` containing that tick and the next
global `IngressOrdinal`.

Commands remaining because of the tick budget retain FIFO order but have no
stamp yet. A command observed after the cutoff is outside the sealed prefix. No
submission or callback may re-enter a tick while its batch is being produced.
Future asynchronous adapters marshal values to this writer context and record
the resulting stamps for replay rather than trying to replay operating-system
arrival races.

### Option B: assign an ordinal when any producer enqueues

A mutex or atomic counter can stamp commands on network threads. This makes a
concurrent API convenient, but lock acquisition or atomic interleaving becomes
the observed order and canonical fields are assigned outside the writer. It
also adds concurrency before Phase 6 has selected the adapter execution model.

### Option C: sort a tick by stable source metadata

Sort by session/player identity, sequence, or command ID at the cutoff. This is
independent of submission interleaving but creates stable identity bias and
contradicts ADR-0013's approved writer-observed ingress order.

## Decision 3: initial queue and per-tick ceilings

### Option A: fixed hard v1 ceilings (recommended)

Use all three independent limits:

- 4,096 accepted commands pending globally;
- 128 pending commands for one `(SessionId, SessionGeneration)` source; and
- 1,024 commands stamped and emitted for one logical tick.

Check the per-source limit before the global limit so a submission that exceeds
both has one stable primary result. Counts include accepted but not yet stamped
commands; rejected attempts consume neither space nor ordinals. The values are
safety ceilings, not promised throughput or gameplay rate limits. They are high
enough for the small friends-server milestone while bounding memory, one source,
and one reducer batch independently. Representative load measurements may lower
them or place configurable operational limits beneath them, but raising a hard
ceiling requires review and updated stress evidence.

### Option B: configurable limits beneath larger hard maxima

Expose queue and per-tick settings now. Operators gain tuning control, but the
configuration becomes a versioned deterministic/replay input before there is
load evidence or a production server configuration surface. Misconfiguration
also creates avoidable behavior variance during early reducer work.

### Option C: one global pending and tick limit

Bound only total pending work and work per tick. This is simpler, but one source
can occupy the entire queue and reject unrelated sessions before validation or
rate policy has a chance to act.

## Decision 4: overload and rejection policy

### Option A: typed immediate rejection plus FIFO deferral (recommended)

If a pending ceiling is reached, reject the new submission immediately with a
typed `PerSessionPendingLimit` or `GlobalPendingLimit` result and emit bounded
observability through the accepted ADR-0020 interface when it lands with the
behavior. Preserve every previously accepted command.

If only the per-tick work ceiling is reached, retain the accepted FIFO suffix
for later ticks. Do not assign its ordinal early. Reliable commands are never
silently dropped, overwritten, coalesced, or converted into latest-wins data.
Overflow alone does not disconnect a peer; Phase 6 security/backpressure policy
may respond to repeated abuse outside the server-core mutation boundary.

Ordinal exhaustion, scheduler terminal errors, or an impossible queue invariant
fail the coordinator closed. A batch is published only after every stamp in it
can be assigned, so failure cannot expose a partial order.

### Option B: drop oldest or coalesce motion commands

Keeping the newest motion intent can reduce latency, but the message is a
reliable apply-once command with identity and acknowledgement semantics. Silent
replacement would merge the reliable and latest-wins classes and make replay
and outcome reporting ambiguous.

### Option C: reject and disconnect immediately

Disconnect the source when either pending limit is reached. This aggressively
protects resources, but turns a core queue condition into transport and abuse
policy before Phase 6 defines backpressure, close reasons, and rate handling.

## Decision 5: Slice 5.1 component and evidence boundary

### Option A: dedicated intake/tick coordinator with ordered batches (recommended)

Add a server-core coordinator that owns the pending queue and next ingress
ordinal, composes the existing `FixedTickScheduler`, and returns up to four
ordered tick batches per pump. A batch contains its `ScheduledTick` and owned
stamped proposals. It contains no canonical player store, reducer callback,
snapshot, persistence record, or script/database interface.

Slice 5.1 proves deterministic scheduling and ordering with exact trace equality
and a stable test-only trace digest. It does not introduce the production
canonical-state checksum reserved for Slice 5.5. Slices 5.2 and 5.3 then attach
state and reduction to the already bounded ordered-batch boundary.

### Option B: put command queues into `FixedTickScheduler`

Extend the clock facility to own commands and admission. This is compact, but it
couples a reusable deterministic time source to session identities, resource
policy, and command lifetime. Scheduler tests would no longer isolate time math.

### Option C: implement the first canonical reducer in the same slice

Run motion validation and state mutation directly from the coordinator. This
would demonstrate a visible result sooner, but it bypasses the planned state and
atomic-reducer decisions in Slices 5.2 and 5.3 and could settle gameplay rules
without their GDR.

## Proposed acceptance tests and demo

Subject to owner approval or amendment, Slice 5.1 should add tests named for
these contracts:

1. `same_clock_and_submission_stream_produces_identical_ordered_trace`
2. `writer_observation_order_wins_ties_not_client_observed_tick`
3. `tick_cutoff_seals_prefix_and_late_submission_waits`
4. `scheduler_stall_emits_at_most_four_sequential_bounded_batches`
5. `tick_budget_stamps_1024_and_defers_fifo_suffix_without_loss`
6. `session_generation_pending_limit_rejects_129th_without_consuming_ordinal`
7. `global_pending_limit_rejects_4097th_without_removing_accepted_work`
8. `one_full_session_queue_does_not_reserve_other_session_capacity`
9. `ordinal_exhaustion_emits_no_partial_batch_and_never_wraps`
10. `queued_proposal_owns_values_and_contains_no_wire_or_generated_view`
11. `intake_does_not_deduplicate_validate_or_mutate_canonical_state`
12. `server_core_intake_has_no_openmw_socket_platform_script_or_database_dependency`

The owner demo should show two interleaved sessions, the exact emitted
tick/ordinal trace, a client-observed-tick attempt that cannot reorder, the
1,024/1,025 per-tick boundary, the 128/129 per-session boundary, the
4,096/4,097 global boundary, a multi-tick catch-up, and ordinal exhaustion with
no partial output. It should also show include/dependency evidence that no wire,
generated, OpenMW, transport-library, script-runtime, or database type enters
the new public intake API.

## Consequences if the recommendation is approved

- The authoritative writer gains one explicit bounded ingress point and total
  order before canonical state exists.
- Session and protocol layers must convert to an owned domain proposal and
  marshal it into the writer context; they cannot stamp or mutate state.
- Replay can record writer stamps as the accepted observed order without
  claiming deterministic operating-system packet arrival.
- Reliable overload is explicit and lossless for already accepted work, while
  abuse-driven disconnect remains a Phase 6 concern.
- Exact hard ceilings become reviewed safety contracts and test boundaries;
  release tuning remains evidence-driven and cannot silently raise them.
- Slice 5.1 remains free of player-state and gameplay decisions, allowing Slices
  5.2 and 5.3 to review those boundaries independently.

## Failure modes and mitigations

- **Protocol types become domain APIs:** convert once to an owned typed proposal
  and forbid frame, generated, and generic payload types in the public header.
- **Callback races decide canonical ties:** submit only on the writer context and
  record drain-time stamps; concurrency stays outside the core.
- **Client time gains authority:** retain observed tick only as input metadata
  and exclude it from eligibility and ordering.
- **One source consumes all pending work:** apply the 128-command source ceiling
  independently of the global ceiling.
- **Tick cap silently loses commands:** retain the accepted FIFO suffix and test
  exact conservation across later ticks.
- **Overflow changes transport behavior:** return typed core results; do not
  disconnect or choose a wire reply in this slice.
- **Ordinal failure exposes half an order:** preflight available ordinal range
  before publishing a batch and fail closed.
- **Coordinator becomes a reducer:** return owned ordered batches only; canonical
  storage, validation, mutation, events, and checksums remain later slices.

## Review and replacement triggers

Reopen this ADR if:

- Phase 6 cannot marshal owned proposals into one writer context without making
  a concurrent core API necessary;
- representative load shows any hard ceiling is unsafe or insufficient;
- a new command source cannot use the same writer cutoff and recorded ordinal;
- a reliable command legitimately requires coalescing or priority classes;
- admission must reserve capacity rather than use the proposed shared global
  ceiling;
- command eligibility needs a rule other than the next writer cutoff; or
- Slice 5.2 or 5.3 requires packet-shaped state, a second mutation path, or a
  different meaning of admission.

## Owner approval

Pending explicit owner approval or amendment of Decisions 1 through 5 and the
proposed acceptance tests. Phase 5 production implementation remains gated.
