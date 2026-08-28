# ADR-0029: Phase 5 immutable canonical publication and versioned change feed

Status: **Proposed**

Date opened: 2026-08-28

Decision owner: project owner

Needed by: Phase 5 Slice 5.4

## Decision questions

Which component owns canonical publication; how is one reader-visible snapshot
coupled to reducer commits without adding a second mutation authority; what
version advances for player-plus-session and acknowledgement-only commits; what
does one domain state-change record contain; how much history is retained; and
what explicit policy prevents a slow, failed, or absent reader from blocking
the writer?

These are architecture, state-scope, ordering, ownership, concurrency, and
failure-policy decisions. Production snapshot/event publication code must not
land until the project owner approves or amends this ADR. This record proposes
no gameplay behavior, interest selection, protocol snapshot encoding,
persistence/replay/script sink, checksum, resynchronization request, or online
runtime composition.

## Decision summary

Pending owner approval. The recommendation is Option A for Decisions 1 through
5:

1. integrate one immutable latest-publication slot with the writer-confined
   reducer, sharing its immutable canonical state and installing one completed
   publication after each batch that commits state;
2. add a checked canonical state version beginning at zero and increment it
   once per installed canonical candidate, including acknowledgement-only
   commits, with conservative batch preflight for exhaustion;
3. publish a complete Phase 5 canonical snapshot plus ordered replacement-
   shaped domain change records, without filtering or wire types;
4. retain only the latest immutable batch publication, use contiguous versions
   to detect a missed feed, and recover from the included complete snapshot;
   and
5. expose read-only shared publication handles with no reader acknowledgement,
   callback, queue wait, or backpressure path into the writer, while retaining
   the Slice 5.5 online gate.

## Existing constraints

1. ADR-0006 requires server-only canonical commit authority, immutable results
   for readers/sinks, explicit state scope, and no client snapshot as input.
2. ADR-0013 requires checked counters, stable ordering, reproducible results,
   and no wall-clock or thread-race influence on canonical outcomes.
3. ADR-0015 requires semantically distinct counters to use strong checked
   values and to fail explicitly at exhaustion.
4. ADR-0020 makes observability best-effort, bounded, low-cardinality, and
   unable to influence state or control flow. It is not a domain change feed.
5. ADR-0024 uses `ServerTick` rather than a wire snapshot sequence and allows at
   most one target-session protocol publication per tick unless reopened.
6. ADR-0025 defines a bounded target-session wire snapshot, but real interest,
   canonical publication selection, and resynchronization remain later work.
7. ADR-0027 supplies a complete immutable, deterministically ordered, bounded
   Phase 5 canonical state with separate player and active-session partitions.
8. ADR-0028 permits only the reducer to install canonical candidates. It
   reserves snapshots and domain state-change records for Slice 5.4 and future
   sink interfaces for Slice 5.6.
9. Slice 5.5 still owns the cross-batch command-ID window, state checksum, and
   explicit resync request. Online reducer composition remains prohibited until
   that gate is complete.

## Representative scenarios

1. The reducer is constructed with valid canonical state. A reader obtains an
   immutable version-zero publication containing the complete initial state and
   no changes.
2. One accepted command replaces player velocity and advances its session
   acknowledgement. Exactly one state version identifies both replacements;
   readers see either the prior publication or the complete post-batch one.
3. One exact-next command is rejected for revision or authority epoch. The
   acknowledgement-only candidate advances the state version and publishes
   only the session replacement; player state remains identical.
4. Unknown-session, old-generation, already-finalized, and sequence-gap
   commands install no candidate, consume no version, and create no state-
   change record.
5. Several commands commit in one ordered tick batch. Their records have
   contiguous versions in reducer order and the publication snapshot is the
   exact state at the last record's version.
6. A reader holds an old publication while the writer completes many later
   batches. The old handle remains immutable and valid; the writer never waits
   for that reader and the latest slot continues advancing.
7. A reader misses one or more batch publications. A version gap is explicit,
   and the reader replaces its view from the latest complete snapshot instead
   of guessing at missing changes.
8. Observability accepts, drops, or ignores every attempt. State, version,
   publication snapshot, and change records remain identical.
9. The state version cannot reserve enough possible increments for a sealed
   batch. The batch fails before its first command executes and preserves state
   and publication exactly.
10. A future adapter tries to emit target-session wire snapshots, interest-
    filtered views, persistence calls, or resync protocol from this component.
    The domain-only API and slice gates reject that composition.

## Decision 1: publication ownership and atomic visibility

### Option A: reducer-integrated immutable batch publication (recommended)

Make publication part of the writer-confined reducer transaction without
creating a second state owner. Store canonical state through immutable shared
ownership. Before executing a sealed batch, prepare bounded publication storage
for at most the batch's command count. For each finalizable command, construct
the complete canonical candidate and its domain change record before installing
the new immutable state pointer and checked version.

After the batch completes, or after an error that follows earlier successful
per-command commits, freeze one publication containing the final shared state
and all committed records from that batch and replace the latest-publication
slot once. A batch with no canonical commit leaves the prior publication
unchanged. Readers therefore observe either the prior complete publication or
the final complete batch publication, never a partially populated object.

This preserves ADR-0028's per-command commit semantics for the writer while
making the reader-visible unit one complete ordered batch. All storage that
could grow with the sealed batch is reserved before the first command executes;
a preparation failure changes neither state nor publication.

### Option B: separate publisher pulls reducer state after `apply`

Let a composition root call the reducer and then copy `state()` into a publisher
with independently constructed events. This keeps classes small but creates a
failure window where canonical state advanced and publication did not, and it
requires a caller to reconstruct changes that only the reducer authoritatively
knows.

### Option C: invoke publication callbacks during each commit

Call registered readers or sinks synchronously after each candidate installs.
This provides immediate delivery but lets arbitrary latency, reentrancy,
failure, and lock ordering enter the only canonical mutation path.

## Decision 2: state version and exhaustion semantics

### Option A: checked global version per installed candidate (recommended)

Add a `CanonicalStateVersion` counter beginning at zero. Increment it exactly
once whenever the reducer installs a valid complete canonical candidate:

- accepted player-plus-session replacement: one increment;
- rejected exact-next acknowledgement-only replacement: one increment; and
- no installed candidate: no increment.

Every change record carries its resulting version. Records within a publication
are strictly contiguous and the snapshot carries the last installed version.
The version is internal domain ordering, not ADR-0024's target-session wire
snapshot sequence and not a replacement for entity revision, authority epoch,
command sequence, or server tick.

Before executing a batch, conservatively prove that the current version has
headroom for the batch's full command count. If not, return a closed batch error
and execute no command. This may reject a near-exhaustion batch containing
commands that would not commit, but it avoids mid-batch ambiguity and makes
exhaustion deterministic and testable.

### Option B: increment once per batch with any change

Give all commits in a batch one version. This maps naturally to publication but
cannot order multiple state-change records, cannot identify an acknowledgement-
only commit independently, and weakens future replay/sink evidence.

### Option C: reuse entity revision and session acknowledgement

Avoid a global counter and infer freshness from existing values. There is no
single ordering across players, and acknowledgement-only versus player changes
cannot be compared without reconstructing reducer history.

## Decision 3: canonical snapshot and change-record scope

### Option A: complete domain snapshot plus replacement records (recommended)

The publication snapshot contains the complete current Phase 5
`CanonicalServerState`: all bounded, ordered player entities and active-session
progress records. It is not filtered by connection, interest, cell, or
presentation visibility.

Each committed change record contains:

- resulting canonical state version and commit server tick;
- writer admission stamp and command identity/order metadata;
- the closed final command disposition;
- the complete replacement active-session progress; and
- the complete replacement player entity when player state changed.

The record contains domain values, not before-images, serialized bytes,
generated schema views, metric payloads, callbacks, or mutable references. The
complete replacement values let future Slice 5.6 consumers apply the same
committed fact without consulting mutable reducer internals. A missed consumer
must use a snapshot rather than infer absent before-images.

### Option B: publish only changed field names and scalar deltas

This can reduce record size but creates a generic property protocol, couples
consumers to storage details, and makes reconstruction depend on implicit prior
state.

### Option C: publish target-session protocol snapshots directly

Build `LatestWinsSnapshot` objects in the server core and omit active-session
records not visible on the wire. This conflates canonical state scope with
interest/routing, introduces protocol dependencies, and cannot serve future
persistence/replay/script consumers as one domain record.

## Decision 4: retention, missed changes, and resnapshot policy

### Option A: latest immutable batch only with explicit version-gap recovery (recommended)

The server core retains one latest publication. Version zero has no events.
Every later publication contains at most 1,024 ordered records because it is
derived from one already bounded intake batch. It also contains the complete
final snapshot, so the retained storage is bounded independently of reader
speed.

A reader tracks the last snapshot/change version it consumed. If the latest
publication begins at the exact next version, it may process the records. If it
begins later, the reader missed one or more publications and must atomically
replace its local view from the included snapshot. This local feed-gap recovery
does not implement Slice 5.5's network checksum or explicit resync request.

### Option B: retain a fixed multi-batch ring

Keep several batches so moderately slow consumers can catch up without a full
snapshot. This needs a new memory/time sizing decision before any production
consumer exists and still needs the same gap fallback when the ring wraps.

### Option C: retain until every registered reader acknowledges

This minimizes missed events but makes retention and writer progress depend on
the slowest or failed reader, contradicting the Phase 5 exit gate.

## Decision 5: reader API, failure isolation, and slice boundary

### Option A: read-only latest handles with no writer backpressure (recommended)

Expose only immutable shared publication handles loaded from one latest slot.
A reader never receives `CanonicalCommandReducer&`, mutable state, an event
builder, or a publication write method. Holding or discarding an old handle
does not acquire a writer-owned mutex, reserve queue space, acknowledge
progress, or prevent replacement of the latest slot. There is no reader
registration callback or condition-variable wait in the reducer.

Publication metrics/events, if added, use ADR-0020 best-effort sinks and cannot
change state, version, records, or replacement of the latest slot. The
publication remains a server-core domain API only. Protocol conversion,
target-session interest, command-result delivery, persistence/replay/script
sinks, checksum comparison, and explicit resync stay in their named later
slices. The entire reducer/publication composition remains offline through
Slice 5.5.

### Option B: bounded per-reader mailboxes

Register each consumer and give it a latest-wins or ring mailbox. This makes
overflow visible per consumer but prematurely chooses reader count, lifecycle,
registration, and queue ownership for protocol, persistence, and scripting
components that do not yet exist.

### Option C: shared lock around reducer state and journal

Let readers take a shared lock and the writer take an exclusive lock. The API is
familiar, but a stalled reader can delay the writer and readers can observe an
implementation journal instead of one immutable publication value.

## Proposed acceptance tests and demo

Subject to owner approval or amendment, Slice 5.4 should add tests named for
these contracts:

1. `initial_publication_is_version_zero_complete_and_immutable`
2. `accepted_command_publishes_player_and_session_replacements_at_one_version`
3. `rejected_next_command_publishes_ack_only_replacement`
4. `noncommitting_dispositions_do_not_advance_version_or_publish`
5. `multiple_commits_publish_contiguous_versions_and_exact_final_snapshot`
6. `failed_batch_preflight_preserves_state_version_and_publication`
7. `version_capacity_preflight_fails_before_any_command_executes`
8. `reader_processes_contiguous_batch_or_detects_gap_and_replaces_from_snapshot`
9. `slow_reader_holding_old_publication_cannot_block_or_mutate_writer`
10. `latest_slot_retains_one_bounded_batch_independent_of_reader_speed`
11. `accepted_dropped_and_null_observability_produce_identical_publications`
12. `publication_exposes_no_wire_engine_socket_script_database_or_mutable_surface`
13. `protocol_interest_sinks_checksum_and_online_composition_remain_gated`

The owner demo should show the immutable version-zero state; one accepted
player-plus-session record; one rejected acknowledgement-only record; several
same-batch commits with contiguous versions; dispositions that publish nothing;
a reader consuming the exact next batch; a reader missing a batch and replacing
from the latest snapshot; a slow reader retaining an old immutable handle while
the writer advances; version-capacity failure preserving all state; and
identical output with accepting, dropping, and no-op observability.

## Cross-cutting consequences

- **Gameplay and state scope:** publication exposes already committed complete
  Phase 5 domain state. It adds no visibility, interest, movement, correction,
  or presentation rule and therefore needs no new GDR.
- **Security and operations:** immutable handles cannot submit state. Internal
  events may contain identities, so they are not observability labels or public
  logs. External adapters retain their own authorization/redaction duties.
- **Protocol and compatibility:** no schema, message kind, capability, golden
  message, or wire snapshot sequence changes. A later adapter may derive at
  most one target-session snapshot per tick under ADR-0024.
- **Scripting, persistence, and replay:** Slice 5.6 may consume these exact
  records only after commit. It may not receive a reducer or publication writer.
- **Resync and divergence:** a local reader can recover a missed in-process feed
  from the included snapshot. Checksums and network resync remain Slice 5.5.
- **Desktop and VR:** publication contains platform-neutral canonical roots;
  neither platform-specific pose nor presentation state enters the feed.

## Consequences if the recommendation is approved

- Readers gain immutable complete state and ordered committed changes without a
  second canonical mutation path.
- One global checked version makes cross-player and acknowledgement-only commit
  order explicit while preserving entity/session-specific counters.
- Latest-only retention fixes core memory independently of reader speed and
  turns missed history into an explicit snapshot replacement.
- Sharing immutable canonical state avoids another full-state copy solely for
  publication, but changes the reducer's private state ownership to an immutable
  shared pointer.
- Publication happens once per committed batch, so readers do not observe
  intermediate same-batch states even though the writer retains ADR-0028's
  per-command commit semantics.
- Future sinks can reuse one replacement-shaped domain record rather than
  translating reducer internals independently.

## Failure modes and mitigations

- **State commits without its record:** reserve the batch builder before
  execution and construct each record before installing its candidate.
- **Reader sees a partially filled publication:** expose the builder only after
  it is frozen and replace the latest immutable handle once.
- **Slow reader blocks simulation:** use no reader acknowledgement, callback,
  queue wait, or shared reader/writer lock.
- **Missed events are silently ignored:** require exact-next version checks and
  snapshot replacement on a gap.
- **Version exhaustion occurs mid-batch:** conservatively preflight headroom for
  the entire sealed command count.
- **Acknowledgement-only commit is invisible:** increment the global version and
  publish the complete session replacement even when no player changes.
- **Internal version leaks onto the wire:** keep publication domain-only and
  retain ADR-0024's server-tick snapshot timeline.
- **Publication becomes a bypass mutation API:** expose immutable handles only;
  keep all replacement construction inside the reducer.
- **Latest-only is mistaken for network resync:** name local feed-gap recovery
  separately and retain the Slice 5.5 checksum/resync gate.
- **Events become generic property bags:** use closed typed replacement values
  and stable reducer metadata.

## Review and replacement triggers

Reopen this ADR if:

- a consumer must receive every event without accepting snapshot replacement;
- measured full-state shared ownership or batch publication misses writer
  budgets;
- one logical transaction must span multiple commands or batches;
- target-session snapshots must publish more than once per server tick;
- a canonical commit can occur outside the command reducer;
- state version must persist across restore/restart before persistence design;
- event before-images are required by an approved sink;
- atomic latest-handle replacement cannot meet a supported toolchain's
  concurrency requirements; or
- Slice 5.5 resynchronization cannot consume the complete canonical snapshot
  without changing its state scope.

## Owner approval

Pending explicit project-owner approval or amendment of Decisions 1 through 5
and the proposed acceptance tests. No production publication implementation is
authorized by this proposal.
