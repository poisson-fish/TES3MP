# GDR-0011: Phase 4 minimal player-exchange semantics

Status: **Proposed**

Date opened: 2026-08-27

Date approved: pending

Decision owner: project owner

Needed by: Phase 4 Slice 4.5

Companion architecture record:
[`ADR-0025`](../adr/ADR-0025-minimal-player-command-world-snapshot-exchange.md)

## Decision questions

What may the first typed player command propose without becoming a
client-authored canonical-state write; who may validate and commit it; what
canonical reality does the first session-targeted world snapshot represent;
and what may a Phase 4 client do with that snapshot before movement, interest,
join, and reconnect behavior are approved in later GDRs?

These are gameplay, authority, state-scope, and presentation-boundary choices.
Production bodies or exchange behavior must not land until the project owner
approves or amends them independently from ADR-0025.

## Recommendation summary

Recommend Option A for Decisions 1 through 4:

1. make the first command a player-root motion intent containing desired linear
   velocity only, with no absolute position, cell, orientation, elapsed time, or
   client-authored canonical values;
2. let an authenticated established client propose the intent only for the
   entity named by its required precondition, while reserving controlled-entity
   authorization, validation, simulation, and commit to the server command path;
3. define the first world snapshot as a server-selected canonical spatial view
   targeted to one session, not necessarily the whole world and not inferred
   from the requesting client; and
4. allow Phase 4 to replace only owned confirmed protocol state after complete
   validation, with no prediction, interpolation, rendering, or gameplay
   simulation.

This is the narrowest reusable semantic body found during repository inspection.
It demonstrates client proposal and server canonical output without deciding
position/cell writes, locomotion acceleration, collision, orientation axes,
interest selection, visibility, correction thresholds, or reconnect behavior.

## Existing constraints

1. The server is the only writer of canonical state. Clients submit bounded
   semantic commands, never canonical snapshots.
2. ADR-0006 permits reversible client prediction but does not require or define
   it in Phase 4. Presentation samples cannot mutate durable player roots.
3. ADR-0016 defines fixed integer `LinearVelocity3`, `Transform`, and
   `SpatialEntitySnapshot` values, but deliberately left movement and snapshot
   behavior to later decisions.
4. Authentication establishes a routing principal only. Player creation,
   player/entity binding, join, and replacement-connection behavior remain
   GDR-0001 and later phase work.
5. Physical-world state defaults to shared canonical reality under ADR-0006,
   but visibility is a separate presentation/interest question. Server
   authority does not imply every client observes every entity.
6. GDR-0001, GDR-0003, and GDR-0004 retain authority over vertical-slice join,
   interest/cell/resync, and production movement/prediction behavior. This
   record must not settle those later scenarios by implication.

## Scenarios the decision must cover

1. A client proposes positive planar velocity for its eventual controlled
   player entity. Phase 4 transports the intent but performs no movement or
   canonical commit.
2. A client proposes vertical or extreme representable velocity. The Phase 4
   codec preserves the bounded integer value; a later approved reducer may
   reject or clamp it. Transport success is not gameplay acceptance.
3. A client includes an absolute position, cell, revision, authority epoch, or
   writer tick in the motion body. The schema has no such fields.
4. A structurally valid precondition names an entity the principal will not
   control. Phase 4 does not infer authority; the future server admission path
   rejects it before mutation.
5. Two target sessions receive different selected canonical views for the same
   server tick. This is permitted and demonstrates that state visibility is
   explicitly session-scoped even though physical entity state is canonical.
6. A target session receives an empty view. The client atomically confirms that
   protocol result without interpreting it as destruction, leaving a cell, or
   loss of global canonical state.
7. A snapshot includes two spatial entities. Their canonical IDs/revisions/
   epochs and transforms are server-authored; entry order is deterministic and
   conveys no gameplay priority.
8. A snapshot is old, for another generation, malformed, or contradicts an
   already accepted same-tick view. Confirmed state remains unchanged.

## Decision 1: first player-command meaning

### Option A: desired linear-velocity intent only (recommended)

The first body is `PlayerMotionIntent` containing one `LinearVelocity3`
`desiredVelocity`. It expresses what motion the client requests for the entity
targeted by the reliable operation's required precondition.

It contains no player/entity identity, position, cell, orientation, duration,
client timestamp, collision claim, grounded flag, animation, pose, revision,
epoch, or writer ordering. The existing command header supplies observed server
tick and application identity/order. Phase 4 defines only its wire meaning:
transporting this desired velocity proposal does not accept, clamp, simulate,
or commit it.

GDR-0001/GDR-0004 later decide legal axes, quantization-to-game controls,
speed/acceleration budgets, jumping/flying/swimming, collision, facing,
prediction, correction, and whether this initial body remains production
movement input or is deprecated behind a new minor/capability.

### Option B: desired velocity plus orientation

This allows a more complete first movement exchange but prematurely decides
that facing is client-proposed with every movement command and which root axes
belong together. GDR-0004 explicitly owns those choices.

### Option C: desired absolute transform and velocity

This is easy to echo in a snapshot but makes an absolute player position/cell
look like a normal client write and weakens the semantic-command boundary. It is
not recommended.

### Option D: a phase-only no-op or protocol-probe command

This avoids movement semantics, but creates a meaningless production body and
golden compatibility artifact solely to satisfy a test. It should be chosen only
with an explicit Slice 4.5 plan amendment and removal/deprecation rule.

## Decision 2: authority and target binding

### Option A: proposal with required entity precondition; server remains sole writer (recommended)

The reliable operation must include `EntityPrecondition`, whose `EntityId`
names the proposed target and whose revision/epoch state what the client
observed. The body does not repeat the identity.

Phase 4 server-session handling checks only established session context and
delivers the owned proposal. It cannot claim the principal controls the entity
and cannot commit movement. Phase 5 creates the single-writer admission/reducer
path; Phase 7 binds a player to an entity and proves the authenticated session
may propose its commands. A structurally valid but unauthorized target must
eventually fail atomically.

### Option B: infer target from the authenticated principal

This reduces wire metadata but incorrectly equates routing identity with
player/entity identity and prevents explicit revision/epoch preconditions.

### Option C: let the client include `PlayerId` and `EntityId` in the body

This makes targeting explicit but duplicates the entity identity and invites a
false assumption that a client-claimed player/entity relationship grants
authority.

## Decision 3: world-snapshot state scope and visibility

### Option A: target-session selected canonical spatial view (recommended)

The snapshot body is a server-authored, session-targeted selected view of zero
or more canonical spatial entity entries. Each entry describes shared physical
world state for its `EntityId`; the set of entries visible in this publication
is scoped to the target session.

The set is not named or treated as the whole world, a cell, a party, or the
sender's personal reality. Phase 4 fake-server fixtures select entries
explicitly. GDR-0001/GDR-0003 later define join barriers, cells, interest,
enter/leave, initial state, and resynchronization. Two clients may receive
different views at the same tick without creating two canonical versions of an
entity.

### Option B: one global world snapshot broadcast identically to all clients

This is simple for a fake peer but silently chooses global visibility and does
not scale to interest-filtered cells.

### Option C: only the target client's own player entity

This avoids interest decisions but cannot demonstrate observation of another
entity and makes the `world snapshot` name misleading for the vertical slice.

## Decision 4: Phase 4 client behavior

### Option A: atomically retain confirmed protocol state only (recommended)

After complete decode and session/timeline validation, the headless client
atomically replaces its owned confirmed snapshot header and selected spatial
view. Failed input preserves the prior value. The client does not predict,
interpolate, extrapolate, render, enter/leave cells, spawn/despawn entities, or
convert the view into OpenMW state.

An empty selected view is an observed protocol state, not a durable deletion or
gameplay event. Acknowledgement progress means only contiguous final command
disposition under ADR-0024, not acceptance of the motion intent.

### Option B: immediately update simulated player positions from the intent

This would demonstrate motion but invents a reducer, validation rules, and
prediction/correction policy before Phases 5 and 7/12.

### Option C: decode snapshots without retaining confirmed state

This proves codec round-trip but cannot demonstrate atomic state exchange or
stale/contradictory snapshot protection at the client-session boundary.

## Proposed behavior acceptance tests and demo

Subject to owner approval or amendment:

1. `motion_intent_contains_velocity_but_no_position_cell_or_canonical_metadata`
2. `motion_intent_transport_does_not_commit_or_imply_acceptance`
3. `motion_target_requires_explicit_entity_revision_and_epoch_precondition`
4. `principal_session_player_and_entity_identity_remain_distinct`
5. `two_sessions_may_receive_different_views_of_one_canonical_tick`
6. `empty_view_is_not_interpreted_as_global_deletion`
7. `snapshot_entry_order_has_no_gameplay_priority`
8. `failed_snapshot_preserves_the_complete_prior_confirmed_view`
9. `phase4_client_performs_no_prediction_interpolation_or_engine_mutation`

The owner demo should compare two fake target sessions at one tick, show the
motion proposal without absolute transform/cell fields, show that no canonical
state changes when the command arrives, and show an atomic confirmed-view
replacement plus one rejected stale/contradictory case.

## Consequences if the recommendation is approved

- Phase 4 obtains a real semantic command without granting clients canonical
  root writes or pretending a codec round-trip is gameplay acceptance.
- The initial command is intentionally small and may be superseded when the
  full movement GDR establishes production locomotion semantics.
- Physical entity state remains one server-authored canonical reality while
  visibility of a snapshot publication is explicitly per target session.
- Phase 4 client state is confirmed protocol data only; all presentation and
  movement simulation remain later work.
- Player creation, session/player/entity binding, and interest selection remain
  unresolved and cannot be inferred from the fixture identities.

## Failure modes and mitigations

- **Desired velocity is treated as accepted velocity:** use proposal/intent
  naming, expose no accepted result in the reliable envelope, and test that the
  fake server performs no commit.
- **Representable means gameplay-valid:** Phase 4 checks scalar structure only;
  later reducers apply owner-approved domain limits before mutation.
- **Precondition grants authority:** document it as observed target state and
  separately test future controlled-entity authorization.
- **Selected view becomes global scope:** test different same-tick views for two
  sessions and reserve interest semantics for GDR-0001/0003.
- **Empty view destroys entities:** retain it only as a confirmed publication;
  future enter/leave/resync rules define durable presentation effects.
- **Fixture identity becomes lifecycle policy:** allocate fixed test values
  outside authentication and prohibit player creation or binding APIs in this
  slice.

## Review and replacement triggers

Reopen this GDR if:

- the owner prefers to pause Slice 4.5 until the full GDR-0001 movement/join
  review rather than accept a narrow initial intent;
- production movement cannot use or safely deprecate desired velocity;
- player target binding cannot use the existing entity precondition;
- a selected spatial view cannot represent the Phase 7 headless exchange;
- later interest semantics require snapshot absence to carry durable meaning;
  or
- the Phase 4 client must perform any prediction, interpolation, or gameplay
  mutation to satisfy an approved scenario.

## Owner approval

Pending explicit project-owner approval or amendment of Decisions 1 through 4.

Approval of this GDR does not approve join, player creation, cell transition,
interest selection, reconnect, movement validation/simulation, prediction,
correction, animation, VR pose, or OpenMW presentation behavior.
