# ADR-0027: Phase 5 canonical player and session state

Status: **Proposed**

Date opened: 2026-08-27

Date approved: pending

Decision owner: project owner

Needed by: Phase 5 Slice 5.2

## Decision questions

How should the first authoritative server state separate per-player entity facts
from session-lifetime progress; which stable keys, ordering, and collection
limits apply; which revision/tick/epoch invariants make a spatial value safe for
the later reducer; how is a complete state value constructed without exposing a
bypass mutation API; and where do session binding and contiguous-finalized
acknowledgement progress live?

These are architecture, authority, state-scope, lifetime, and state-invariant
decisions. Production canonical-state code must not land until the project owner
approves or amends this record. This record does not decide identity creation,
character generation, spawn, readiness, connection replacement, persistence
lifetime, cell-transition eligibility, movement validation, reducer outcomes,
interest, resynchronization, or gameplay presentation.

## Decision summary

No option is accepted yet. The recommendation is Option A for Decisions 1
through 5:

1. use separate writer-owned player-entity and active-session-progress
   partitions, with per-player canonical spatial scope and explicitly
   session-lifetime acknowledgement state;
2. store both partitions in stable identity order with hard Slice 5 ceilings of
   256 player entities and 256 active sessions, unique player/entity/session
   identities, and explicit session-to-player/entity bindings;
3. give each player entity one atomic spatial revision covering cell/root
   transform and velocity, one last-change server tick, and one unchanged
   authority epoch, with a pure checked spatial-advance value operation;
4. construct an immutable owned server-state value only after the complete
   bounded input passes ordering, uniqueness, cross-binding, and invariant
   checks, while exposing no mutable references or general set-by-key API; and
5. store optional highest-contiguous-finalized command progress on the active
   session record, not on the player entity, and give it no success/failure or
   persistence meaning.

## Existing constraints

1. ADR-0006 makes the server the only canonical writer, requires explicit state
   scope/lifetime, and classifies canonical cell/root/velocity as per-player
   entity state while session generation and acknowledgement are connection/
   session lifetime rather than gameplay state.
2. ADR-0006 defers player identity lifetime, spawn/reconnect behavior, cell
   transitions, and movement rules to GDR-0001 through GDR-0004. Slice 5.2
   cannot settle those behaviors by choosing a convenient constructor.
3. ADR-0016 fixes engine-independent `CellId`, `Transform`, `LinearVelocity3`,
   `EntityRevision`, `AuthorityEpoch`, and `SpatialEntitySnapshot` meanings. It
   does not approve a store, lifecycle, mutation rule, or snapshot publication.
4. ADR-0013 requires stable identity ordering, checked counters, deterministic
   canonical values, and no hash-iteration, wall-time, or client-time influence.
5. ADR-0024 defines acknowledgement as optional highest contiguous command
   disposition, including rejected commands, and explicitly denies it success,
   durability, or entity-revision meaning.
6. ADR-0025 and GDR-0011 keep physical entity state one server-authored reality
   while allowing different session-targeted selected views. Interest is not
   canonical state scope.
7. ADR-0026 provides ordered stamped proposals only. It deliberately performs
   no validation, deduplication, state lookup, revision check, or mutation.
8. Slice 5.3 owns validation and atomic reducer application; Slice 5.4 owns
   immutable publication/change events; Slice 5.5 owns checksums and resync;
   Slice 5.6 owns sink interfaces.
9. The negotiated content context interprets `CellId` but remains outside each
   record until Phase 10 approves content identity. No ambient engine lookup may
   enter this state boundary.

## Representative scenarios

1. Two player entity records exist with distinct `PlayerId` and `EntityId`
   values. Each has one canonical cell/root/velocity reality independent of
   which sessions currently observe it.
2. One player exists without an active session. The player record remains
   representable; this does not decide disconnect grace or persistence.
3. Two active sessions bind explicitly to two different player/entity pairs and
   carry independent acknowledgement progress. No binding is inferred from
   insertion order, principal identity, or matching numeric values.
4. A session binding names a missing player, the wrong entity for a player, or a
   player/entity already bound by another active session. Complete construction
   fails and exposes no partial state.
5. Player/session inputs are unsorted, duplicate an identity, exceed 256, or
   reuse an `EntityId` under another player. Construction rejects before copying
   the owned collections.
6. A validated future reducer advances transform and velocity together at one
   server tick. The entity revision increments exactly once, the authority epoch
   is unchanged, and the original immutable value remains unchanged.
7. Two ordered commands update one player in the same logical tick. Two spatial
   advances may share that tick but produce two strictly increasing revisions.
8. A proposed advance regresses the last-change tick or cannot increment the
   revision. It returns a typed error without a replacement value.
9. A session acknowledgement advances while its player spatial value does not.
   Entity revision and spatial state remain byte-for-byte equal.
10. A later snapshot selects one or both player records for different sessions.
    Selection cannot alter the underlying player partition or session binding.

## Decision 1: state partitions, scope, and lifetime

### Option A: separate player-entity and active-session partitions (recommended)

Define one owned `CanonicalServerState` with two explicit partitions:

- `CanonicalPlayerEntityState`, keyed by `PlayerId` and carrying a distinct
  `EntityId`, canonical spatial fields, entity revision, authority epoch, and
  last-change server tick; and
- `CanonicalSessionProgress`, keyed by `SessionId`, carrying the current
  `SessionGeneration`, an explicit `PlayerId`/`EntityId` binding, and optional
  highest-contiguous-finalized command sequence.

The player record has explicit per-player entity scope. This record does not
decide whether or how long that state persists across disconnect or restart;
GDR-0001/GDR-0002 and Phase 20 retain that decision. The session record is
already approved as active-session lifetime and is excluded from player entity
revision. A player may exist without an active session, but every session in
this minimal state must bind to an existing matching player/entity pair.

### Option B: one player record embedding session and acknowledgement

Store session ID, generation, binding, and ack alongside cell/root/velocity.
This reduces joins and mirrors one online player, but reconnect or replacement
would rewrite a player record and risk incrementing a durable entity revision
for transport progress. Offline player state would also become awkward.

### Option C: key all player state by active session

Treat `(SessionId, SessionGeneration)` as the canonical player key. This is easy
for network routing but makes reconnect create a new canonical identity and
implicitly deletes or replaces world state when a connection ends. It conflicts
with stable player/entity identity and explicit lifetime rules.

## Decision 2: identity indexing, ordering, binding, and bounds

### Option A: sorted owned vectors with 256/256 hard ceilings (recommended)

Own at most 256 player records strictly sorted by `PlayerId` and at most 256
active-session records strictly sorted by `SessionId`. Lookups use deterministic
binary search; iteration and equality use stored identity order. These are hard
Slice 5 safety ceilings, not a release player-count promise.

Require every `PlayerId`, `EntityId`, and active `SessionId` to be unique. A
session record's generation is explicit, but only one active generation of a
given session ID may appear in this state. Each session binds both `PlayerId`
and `EntityId`; the pair must match one player record, and no two active sessions
may bind the same player or entity. This represents one unambiguous active
controller without deciding how a replacement is admitted or old state is
retired.

The 256 values align with the already reviewed Phase 4 selected-view ceiling and
keep initial validation/copy work small, but state and view limits remain
separate contracts. Phase 10 or measured load may reopen the state ceilings;
raising them requires updated memory/work and multi-client evidence.

### Option B: ordered maps with a configurable capacity

Use `std::map` and operator-supplied limits beneath a larger maximum. Ordered
iteration is deterministic and insertion is convenient, but runtime capacity
becomes a configuration/replay input before Phase 21 owns configuration or load
evidence exists. Node allocation also makes the initial immutable value less
compact.

### Option C: unordered maps plus sort-on-publication

Use hash lookup internally and sort only when snapshotting or checksumming.
Average lookup is convenient, but every state consumer must remember to
canonicalize, construction and diagnostics can still observe hash order, and
cross-platform behavior depends on more hidden discipline.

## Decision 3: spatial revision, tick, and authority-epoch invariants

### Option A: one atomic spatial revision and checked value advance (recommended)

One `EntityRevision` covers the complete cell/root transform and linear velocity
tuple for one player entity. Store the `ServerTick` of its latest committed
spatial change and its current `AuthorityEpoch`. Initial values are supplied
explicitly for deterministic seeds, restore, and tests; this slice does not
allocate them or choose spawn values.

Provide a pure value operation that accepts a non-regressing commit tick plus a
complete replacement transform and velocity, checks `EntityRevision::next()`,
and returns a new player value with exactly one revision increment. Multiple
ordered mutations in one logical tick may share the tick and increment the
revision separately. Tick regression or revision exhaustion returns a typed
error and no value. The operation cannot change player/entity identity or
authority epoch; later approved delegation owns epoch changes.

This operation is not a gameplay reducer. It does not authorize the caller,
validate movement/cell rules, decide whether a change should happen, or install
the value in a running state root.

### Option B: separate cell, transform, and velocity revisions

Independent revisions can reduce false conflicts, but commands and snapshots
need compound preconditions and atomic multi-field commits. The initial protocol
already carries one entity revision, and partial spatial versioning would make
reconciliation ambiguous.

### Option C: one global server-state revision

Increment one revision for any entity or session change. This is easy to
checksum but makes unrelated players contend on the same precondition and gives
session acknowledgements accidental world-state significance.

## Decision 4: construction and mutation surface

### Option A: validate complete input, then own immutable values (recommended)

Expose a fallible factory taking bounded spans of player and session values.
Before allocating/copying owned vectors, validate count ceilings and required
sort order. Before returning a state, validate identity uniqueness, entity
uniqueness, explicit binding consistency, and duplicate active bindings. Any
failure returns a closed error with bounded numeric context and no partial state.

The resulting state exposes only const spans and optional const lookups. It has
no public mutable record, iterator, `operator[]`, insert/erase, arbitrary
set-by-key, packet import, or script/admin mutation API. Slice 5.3 may compose
the pure player-value advance behind the one reducer path; later restore and
replay boundaries must construct or command state through separately reviewed
interfaces.

### Option B: mutable store with insert/update/erase methods

This is straightforward for the upcoming reducer, but it creates multiple
public mutation operations before validation and atomic commit are implemented.
Future scripts or adapters could accidentally retain the bypass surface.

### Option C: expose only protocol snapshot import/export

Decode a `LatestWinsSnapshot` into state and encode state back. This reuses an
existing value, but makes a target-session selected publication the canonical
store format and risks accepting client/network state as server authority.

## Decision 5: active-session binding and acknowledgement meaning

### Option A: explicit binding plus optional disposition progress (recommended)

Each active-session record contains `SessionId`, `SessionGeneration`, explicit
`PlayerId`, explicit `EntityId`, and optional `CommandSequence` representing the
highest contiguous command with a final disposition. Absence means no final
contiguous progress is represented. Presence does not say the named command was
accepted, mutated state, or became durable.

The acknowledgement belongs only to the active session generation. It does not
increment the bound entity revision, alter spatial state, or silently carry to a
new generation. GDR-0001/GDR-0002 and the resumption design later decide what a
resumed/replaced connection observes and whether any progress is transferred.
Slice 5.3 owns monotonic advancement and command disposition; Slice 5.2 only
stores and validates the value shape.

### Option B: store acknowledgement on the player entity

This survives reconnect naturally, but makes connection delivery progress look
like per-player world state and couples a command disposition to entity
revision, persistence, and snapshots.

### Option C: acknowledge only successfully mutating commands

This gives the field a simple success meaning, but breaks the accepted
highest-contiguous-finalized contract: a rejected command would leave a
permanent gap and reliable retry/idempotency state could not advance cleanly.

## Proposed acceptance tests and demo

Subject to owner approval or amendment, Slice 5.2 should add tests named for
these contracts:

1. `player_entity_and_active_session_state_have_distinct_scope_and_lifetime`
2. `player_may_exist_without_session_but_session_requires_matching_player_entity`
3. `two_sessions_bind_explicitly_to_two_distinct_player_entity_pairs`
4. `player_and_session_inputs_require_strict_stable_identity_order`
5. `duplicate_player_entity_session_or_active_binding_fails_without_partial_state`
6. `player_and_session_limits_accept_256_and_reject_257_before_owned_copy`
7. `spatial_advance_atomically_replaces_cell_root_velocity_and_increments_revision_once`
8. `two_spatial_advances_in_one_tick_produce_two_monotonic_revisions`
9. `tick_regression_and_revision_exhaustion_preserve_the_original_value`
10. `spatial_advance_cannot_change_identity_or_authority_epoch`
11. `session_ack_is_optional_disposition_progress_not_success_or_entity_revision`
12. `state_exposes_const_ordered_values_without_wire_engine_or_bypass_mutation_surface`

The owner demo should show two players and two explicitly bound sessions in
stable order, one player with no session, 256/257 collection boundaries, each
duplicate/missing/mismatched binding failure, two same-tick spatial advances
with consecutive revisions, a failed regressing/exhausted advance preserving
the original value, and an acknowledgement change that leaves player spatial
state and revision unchanged. Header/dependency evidence should show no mutable
view, protocol root, generated code, OpenMW, socket, script, or database type.

## Consequences if the recommendation is approved

- The server gains one deterministic owned state value with explicit player and
  active-session partitions before a mutation path exists.
- Player/entity identity and per-player spatial scope remain separate from
  connection/session lifetime and acknowledgement progress.
- One atomic spatial revision matches the existing entity precondition and
  snapshot value without making a network snapshot the storage format.
- Explicit full-state construction supports deterministic fixtures while
  withholding a general mutation API from adapters, scripts, and administrators.
- The initial 256/256 ceilings bound validation and memory but must be revisited
  if lifecycle/persistence requirements or measured scale exceed them.
- Later GDRs still own creation, spawn, reconnect/replacement, durability, cell,
  movement, interest, and resync behavior.

## Failure modes and mitigations

- **Session identity becomes player scope:** use separate record types and
  explicit cross-binding; permit a player record without a session.
- **Reconnect rewrites world state:** keep generation/ack in session progress and
  defer transfer/replacement semantics to GDR-0001/GDR-0002.
- **Interest becomes state ownership:** store one player entity reality and no
  recipient/view membership in the canonical value.
- **Partial spatial updates escape:** replace transform plus velocity together
  under one revision and return a new value only after all checks pass.
- **Revision wraps:** use checked advancement and return no value at exhaustion.
- **Hash order affects replay:** require sorted input, store sorted vectors, and
  expose deterministic iteration only.
- **Factory becomes restore bypass:** construct immutable values only; separately
  gate online installation, restore, and replay mutation boundaries.
- **Ack is interpreted as success:** name and test it as contiguous-finalized
  disposition progress and keep it outside entity revision.
- **Provisional bounds become accidental product promises:** document review
  triggers and require measured evidence before raising hard ceilings.

## Review and replacement triggers

Reopen this ADR if:

- GDR-0001/GDR-0002 requires simultaneous active generations or multiple
  controllers for one player;
- player identity, entity identity, or persistence lifetime cannot remain
  separate from an active session;
- measured scale requires more than 256 player records or active sessions;
- a domain needs partial spatial revisions rather than one entity revision;
- authority epoch must change outside a reviewed delegation/handoff operation;
- the reducer cannot install a checked immutable replacement atomically;
- content context must become an explicit field before Phase 10; or
- session acknowledgement acquires gameplay, persistence, or success meaning.

## Owner approval

Pending explicit owner approval or amendment of Decisions 1 through 5 and the
proposed acceptance tests. Slice 5.2 production implementation remains gated.
