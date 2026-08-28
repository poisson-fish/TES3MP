# ADR-0030: Phase 5 bounded idempotency, canonical checksum, and resync boundary

Status: **Accepted**

Date opened: 2026-08-28

Date approved: 2026-08-28

Decision owner: project owner

Needed by: Phase 5 Slice 5.5

## Decision questions

What exact state and lifetime define the first cross-batch `CommandId`
idempotency window; what happens when a retained ID is reused under a new exact-
next sequence; which canonical fields, tick, rules version, and safety state are
encoded for checksums; which dependency-free versioned checksum algorithm is
appropriate; what may an explicit resync request contain and return; and when is
the pre-Slice-5.5 reducer safety gate considered complete without silently
adding a network runtime or gameplay presentation policy?

These are architecture, authority, state-scope, compatibility, resource-bound,
and recovery decisions. Production idempotency/checksum/resync code must not
land until the project owner approves or amends this ADR. This record proposes
no session lifecycle, interest filtering, target-session wire checksum,
transport rate limit, client correction presentation, persistence format,
authority transfer, or online transport composition.

## Decision summary

The project owner approved Option A for Decisions 1 through 5 on 2026-08-28:

1. retain an immutable trailing window of at most 1,024 finalized command
   records per active session generation as reducer safety state, published and
   checksummed with that session's progress;
2. check the retained window after exact-next sequence admission, reject a
   retained duplicate ID with a final acknowledgement-only disposition, append
   every final disposition, and evict oldest sequence records deterministically;
3. define canonical checksum encoding V1 over the complete Phase 5 publication,
   its state version, checkpoint tick, rules version, authority epochs, and
   idempotency state, using dependency-free CRC-64/ECMA-182 V1 as a divergence
   checksum with exact-byte evidence;
4. add a typed current-session resync request that carries only observed
   version/reason metadata and returns the latest immutable canonical
   publication for later projection, never client-authored state; and
5. retain mandatory revision/current-epoch equality and best-effort
   observability, remove only the explicit missing-idempotency blocker after
   tests pass, and leave all real protocol/transport/runtime composition gated.

## Existing constraints

1. ADR-0003 requires bounded idempotency windows against replay/duplication and
   rejects an unbounded resource commitment.
2. ADR-0006 requires session, revision, idempotency, and authority-epoch
   validation before mutation. A checksum/revision mismatch may trigger only a
   bounded server snapshot; a client cannot upload canonical repair state.
3. ADR-0013 requires stable explicit byte encoding, rules version, tick,
   revisions, deterministic state, and versioned checksum algorithm. Full byte
   comparison remains test evidence so checksum collision is never the only
   proof.
4. ADR-0015 requires checked strong counters and stable identity ordering.
5. ADR-0017 reserves the production canonical checksum selection to Slice 5.5;
   its test-trace FNV digest is explicitly not a canonical checksum.
6. ADR-0024 defines `CommandId` as one logical application identity and
   `CommandSequence` as active-generation contiguous order. Retrying one
   operation retains both; a different logical operation requires a different
   ID and sequence.
7. ADR-0027 supplies bounded immutable player and active-session state. Session
   generation and acknowledgement are separate from durable player state.
8. ADR-0028 already checks session, exact-next sequence, entity binding,
   revision, and current authority epoch. It intentionally leaves cross-batch
   ID history, checksum, and resync to this slice.
9. ADR-0029 publishes complete immutable Phase 5 state and contiguous change
   versions. Its local feed-gap snapshot replacement is not a network resync.
10. No production network/runtime composition exists. Phase 6 still owns secure
    transport/session mapping, and Phase 7/GDR-0003 own real interest and client
    resync presentation.

## Representative scenarios

1. Sequence 1 with command ID 100 commits. A later batch submits exact-next
   sequence 2 with ID 100. It is finalized as `DuplicateCommandId`, advances
   acknowledgement and canonical version, changes no player, and enters the
   retained history as a rejected final command.
2. Two active sessions use the same command ID. Their generation-scoped windows
   are independent and neither session can poison the other.
3. One session finalizes 1,025 commands. Its window retains exactly the newest
   1,024 sequence-ordered records. Eviction is deterministic and does not alter
   player state, acknowledgement, or other sessions.
4. A command ID falls outside the retained window and later appears under a new
   exact-next sequence. The bounded V1 guarantee no longer recognizes the old
   use; the record and observability make the retry horizon explicit rather
   than claiming unbounded apply-once memory.
5. A stale or future authority epoch never mutates player state. Current epoch
   is included in canonical bytes, so an epoch difference changes the checksum.
6. Two equivalent canonical publications produce identical V1 bytes and
   checksum regardless of allocation address or observability behavior. A one-
   field difference changes the exact bytes and normally the checksum; tests
   compare bytes as the primary evidence.
7. A fake complete-state reader misses a publication or detects a checksum
   mismatch and submits a typed request for its current session generation. The
   server returns the latest immutable publication without changing canonical
   state or accepting reader state.
8. An unknown session or old generation requests resync. No publication handle
   is returned. The request cannot discover or mutate another session's state.
9. A future protocol adapter attempts to send the complete internal checksum as
   an interest-filtered client checksum. The domain-only API and later GDR gate
   reject that assumption.
10. Slice 5.5 tests pass. The server core no longer carries the explicit
    "missing cross-batch window" blocker, but no socket, transport action,
    session delivery, target projection, or runtime loop is added.

## Decision 1: idempotency state scope, capacity, and lifetime

### Option A: per-active-generation immutable trailing 1,024 window (recommended)

Extend active-session progress with a bounded immutable trailing sequence of
finalized command records. Each record contains `CommandSequence`, `CommandId`,
and the closed final `CommandDisposition`. Records are strictly sequence-
ordered, contain at most 1,024 entries, and when non-empty end at the session's
highest contiguous finalized sequence. They may begin after sequence 1 because
older records can be evicted or an accepted state can start with earlier
acknowledgement progress.

The window belongs to one `SessionId` plus `SessionGeneration`; a replacement
generation starts empty. It is reducer safety state rather than player gameplay
state, but it affects future command disposition, so it participates in the
complete immutable canonical state, publication, checksum, and later replay
input. Internally shared immutable storage may keep bounded full-state copies
cheap without exposing mutation.

The 1,024-record bound matches the maximum sealed tick batch, covers eight full
128-command per-session pending ceilings, and caps the worst-case active set at
262,144 records across 256 sessions. It is a declared retry horizon, not an
unbounded mathematical uniqueness proof.

### Option B: per-generation 128-record window

Match only the per-session pending limit. This is smaller, but a delayed retry
can fall outside history after one fully queued wave and the global/tick limits
allow substantially more intervening work.

### Option C: retain every command ID for the generation

This gives exact generation-lifetime membership, but an authenticated long-
lived client can grow canonical memory without bound. A large fixed session
command cap would merely move the denial-of-service and lifecycle decision.

## Decision 2: duplicate disposition, insertion, and eviction

### Option A: exact-next membership check, final rejection, append, FIFO eviction (recommended)

Preserve ADR-0028's closed order through session generation and sequence. For an
exact-next command, test its ID against the retained active-generation window
before entity binding/revision/epoch/domain validation. A retained match returns
`DuplicateCommandId`, constructs the same acknowledgement-only candidate as any
other final rejection, and increments canonical version once.

Append one finalized history record for every installed exact-next candidate,
accepted or rejected. If appending record 1,025, remove the lowest sequence and
retain the newest 1,024. This unifies same-batch and cross-batch detection because
each earlier command's immutable candidate is installed before the next command
validates. Repeated rejected duplicates remain visible while their newest
occurrence is retained.

An older/already-finalized sequence still returns `AlreadyFinalized` before ID
membership, and a gap still returns `SequenceGap`; neither changes history.
Reuse of an ID whose last retained occurrence was evicted is outside the
declared window and is treated as a new ID. Metrics distinguish retained
duplicates but do not attach IDs as labels.

### Option B: reject a duplicate without acknowledgement or history insertion

This avoids adding another occurrence, but permanently blocks contiguous
progress at the duplicate sequence and allows the same ID to become eligible
immediately after its original occurrence is evicted.

### Option C: disconnect or replace the session on any duplicate

This is strict misuse handling, but decides transport/session lifecycle and
friends-server policy beyond the reducer. A closed final disposition is enough
for this slice; later composition may separately rate-limit or close abuse.

## Decision 3: canonical encoding and checksum algorithm

### Option A: explicit V1 bytes plus CRC-64/ECMA-182 V1 (recommended)

Define an owned canonical byte encoder with no generated schema or native-
layout serialization. V1 uses fixed little-endian fields and explicit presence,
discriminator, and collection counts in this order:

1. four-byte `T3CS` domain tag, encoding version 1, checksum algorithm version
   1, and canonical rules version 1;
2. canonical state version and checkpoint `ServerTick` (zero for the initial
   publication; the sealed batch tick for a committed publication);
3. strictly ordered player count and complete player/entity fields: IDs, cell
   discriminator and coordinates/space, transform, velocity, entity revision,
   authority epoch, and last spatial-change tick;
4. strictly ordered active-session count and complete session/generation/
   binding/acknowledgement fields; and
5. each session's ordered retained idempotency records including sequence, ID,
   and final disposition.

There are currently no canonical RNG streams in `CanonicalServerState`; V1
encodes a zero stream count so later durable RNG state requires an explicit
encoding/rules-version extension. Transport/session queues, publication change
records, allocation addresses, metrics/events, credentials, wall time, and
presentation state are excluded.

Compute CRC-64/ECMA-182 with polynomial `0x42F0E1EBA9EA3693`, initial value zero,
no reflection, and final XOR zero. The check value for ASCII `123456789` is
`0x6C40DF5F0B497347`. Name the result and algorithm version explicitly.

CRC-64 is a dependency-free deterministic divergence checksum, not a
cryptographic authenticator. A client report is never trusted for authority or
state mutation. Tests retain canonical bytes and semantic comparisons, so the
64-bit result is not collision-proof evidence by itself.

### Option B: project-owned SHA-256 canonical checksum

Use the same canonical bytes with an in-tree SHA-256 implementation and standard
vectors. Collision resistance is stronger, but the feature does not establish
trust from client checksums, and maintaining security-sensitive hash code adds
cost without replacing exact-byte/replay evidence.

### Option C: reuse the test-only FNV-1a trace digest

This is already implemented and simple, but ADR-0017 explicitly excludes it as
the canonical checksum and its trace bytes do not equal the declared canonical
state encoding.

## Decision 4: explicit resync request and response semantics

### Option A: current-session metadata-only request returning latest immutable publication (recommended)

Add closed server-core values:

- `CanonicalResyncReason`: local feed gap, entity revision mismatch, or checksum
  mismatch;
- request: current `SessionId`, `SessionGeneration`, reason, and the reader's
  last observed canonical state version; and
- result: `SnapshotRequired`, `UnknownSession`, or
  `SessionGenerationMismatch`, with a latest immutable publication handle only
  for `SnapshotRequired`.

Validate session existence and generation against the latest canonical state.
For a current session, every explicit request returns the latest complete
publication for a later trusted adapter to project. It does not condition
repair on a client-supplied checksum and does not accept entities, transforms,
revisions, acknowledgements, command history, or arbitrary bytes from the
request. State, version, checksum, and publication remain unchanged.

This is a bounded server-core recovery decision, not a wire message or client
interest rule. Phase 6/7 may add an authenticated/rate-limited request schema
and target-session projection under GDR-0003 without changing the no-upload
authority boundary. The complete internal checksum cannot be assumed equal to
a future interest-filtered client-view checksum.

### Option B: request carries a client checksum and server returns only on mismatch

This can avoid redundant snapshots, but the complete internal checksum includes
active-session safety state that a target client must not receive, and real
interest scope is not approved. Comparing it now would create a false wire
contract.

### Option C: client uploads its local snapshot for merge or repair

This could diagnose differences directly but violates the server-only canonical
writer rule and creates an unbounded hostile state-import path.

## Decision 5: epoch regression evidence and remaining composition gate

### Option A: retain current equality, checksum epoch, close only the named core blocker (recommended)

Keep authority epoch equality exactly where ADR-0028 placed it, after binding
and entity revision and before domain behavior. Both lower stale and higher
fabricated epochs return the same closed `AuthorityEpochMismatch`, finalize an
exact-next command, enter idempotency history, and mutate no player state.
Authority epoch itself remains immutable because handoff/lifecycle is not part
of this slice. Include it in canonical bytes and checksum.

After the bounded cross-batch window, checksum, resync values, observability,
and focused tests pass, remove comments and tests that identify the missing
Slice 5.5 window as an unfinished reducer-core blocker. The component becomes
eligible for a later reviewed composition, but this slice adds no connection,
socket, transport channel, protocol request/result, target projection,
background loop, or automatic delivery. Phase gates still prohibit calling
that absence a production online runtime.

### Option B: keep the reducer categorically offline until Phase 7

This is conservative, but leaves the explicit Slice 5.5 acceptance boundary
meaningless and prevents later Phase 6 in-memory/transport composition tests
from using a completed server core.

### Option C: connect the reducer directly to the Phase 4 fake peer now

This would demonstrate end-to-end motion early, but prematurely decides secure
transport/session mapping, target snapshot projection, result delivery,
interest, and gameplay reconciliation.

## Proposed acceptance tests and demo

Subject to owner approval or amendment, Slice 5.5 should add tests named for
these contracts:

1. `cross_batch_reused_command_id_is_finalized_once_without_player_change`
2. `idempotency_windows_are_isolated_by_active_session_generation`
3. `finalized_acceptance_and_rejection_both_enter_ordered_history`
4. `window_1025_evicts_only_oldest_and_retains_exactly_1024`
5. `evicted_id_reuse_documents_the_bounded_retry_horizon`
6. `same_batch_and_cross_batch_duplicates_use_one_membership_rule`
7. `stale_and_future_authority_epochs_finalize_without_player_mutation`
8. `canonical_v1_bytes_are_explicit_stable_and_cover_complete_phase5_state`
9. `crc64_ecma_check_vector_and_canonical_checksum_are_stable`
10. `identity_revision_epoch_ack_history_version_or_tick_change_changes_bytes`
11. `observability_allocation_and_publication_handle_do_not_change_checksum`
12. `current_session_resync_returns_latest_immutable_publication_without_mutation`
13. `unknown_or_old_generation_resync_returns_no_publication`
14. `resync_request_cannot_upload_client_state_or_depend_on_wire_interest_or_transport`
15. `slice55_closes_only_the_core_idempotency_blocker_without_runtime_composition`

The owner demo should show an accepted command followed in another batch by the
same ID under an exact-next sequence; independent sessions; accepted and
rejected history records; the 1,024/1,025 eviction boundary and documented
out-of-window reuse; stale/future epoch rejection; exact canonical V1 bytes,
the CRC-64 standard vector, and field-sensitivity evidence; matching checksums
across equivalent states and observability modes; current/unknown/old-generation
resync outcomes; unchanged state around resync; and compile-time evidence that
no wire, interest, client-state upload, socket, or runtime path landed.

## Cross-cutting consequences

- **Gameplay and state scope:** idempotency history is session-generation safety
  state, not player gameplay. Resync returns already committed canonical state
  without choosing interest or presentation, so no new GDR is proposed.
- **Security and operations:** the exact bounded retry horizon is explicit.
  CRC-64 detects ordinary divergence but authenticates nothing. IDs and
  checksums remain structured event payloads at most, never metric labels.
- **Protocol and compatibility:** no schema or capability changes. Canonical
  encoding/checksum versions are internal domain contracts; target-view
  checksum and request wire messages require later review.
- **Scripting, persistence, and replay:** history and canonical bytes affect
  future dispositions and replay equivalence. Slice 5.6 sinks receive committed
  records only; Phase 20 decides persisted checkpoint format and migration.
- **Resync and visibility:** the server returns the latest complete immutable
  publication to a trusted adapter. It does not expose all active-session state
  directly to clients or decide who can see which player.
- **Desktop and VR:** both use identical command IDs, canonical bytes, checksum,
  and resync metadata. No platform presentation value participates.

## Consequences if the recommendation is approved

- Cross-batch duplicates inside the declared 1,024-finalization horizon cannot
  repeat player effects and do not block contiguous acknowledgement.
- Session progress becomes larger and loses trivial literal construction, but
  immutable shared history can preserve bounded state-copy costs.
- Complete canonical V1 bytes provide replay/debug evidence and a versioned
  CRC-64 checkpoint without a new dependency.
- Publication gains checkpoint tick and checksum metadata while retaining one
  complete immutable latest value.
- Explicit resync remains server-to-reader replacement only; real network and
  interest semantics stay reviewable later.
- The named pre-Slice-5.5 core blocker closes, but no production multiplayer
  runtime is implied.

## Failure modes and mitigations

- **Window grows without bound:** enforce 1,024 records before constructing
  session progress and evict exactly one oldest record on append.
- **Duplicate rejection blocks sequence progress:** finalize it, append its
  rejected record, and advance acknowledgement atomically.
- **Same-batch path differs from cross-batch:** query only the installed session
  history produced by earlier per-command candidates.
- **Eviction is mistaken for eternal apply-once:** name the retry horizon,
  demonstrate out-of-window reuse, and expose retention in canonical state.
- **Checksum depends on layout or allocation:** encode explicit fixed-width
  little-endian semantic fields in stable order.
- **Checksum omits outcome-affecting history:** include active-session history,
  version, checkpoint tick, rules version, revisions, and epochs.
- **CRC is mistaken for authentication:** document it as divergence evidence and
  never grant authority based on a reported value.
- **Resync uploads or mutates client state:** request carries metadata only and
  response shares an existing immutable publication.
- **Internal checksum leaks private state:** keep it domain-only; later interest
  work defines any target-view checksum independently.
- **Completing 5.5 is called an online runtime:** remove only the named core
  blocker and retain Phase 6/7 integration gates.

## Review and replacement triggers

Reopen this ADR if:

- measured retry/reorder behavior exceeds the 1,024-finalization horizon;
- a session generation must provide exact lifetime command-ID membership;
- session replacement cannot retain or intentionally reset safety history;
- CRC-64 collision risk becomes material for a trusted persistence or security
  use;
- canonical RNG, new durable state, or authority handoff lands without a
  checksum encoding/rules-version extension;
- replay requires a checksum at every command rather than publication
  checkpoints;
- a target client needs a checksum before interest scope is approved;
- resync requires delta repair instead of complete snapshot replacement; or
- a real runtime cannot compose the core without changing command authority,
  history, checksum, or resync semantics.

## Owner approval

Approved by the project owner in the 2026-08-28 working session: Option A for
Decisions 1 through 5, the 1,024-record bounded retry horizon,
CRC-64/ECMA-182 V1, and all 15 proposed acceptance tests.

This approval authorizes only the Slice 5.5 bounded server-core idempotency
history, canonical encoding/checksum, metadata-only resync values,
observability, focused tests, and removal of the named reducer-core blocker.
Protocol schemas, interest filtering, client-state upload, socket/transport
actions, target projection, automatic delivery, and online runtime composition
remain separately gated.
