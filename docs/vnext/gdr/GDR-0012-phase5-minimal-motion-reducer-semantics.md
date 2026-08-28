# GDR-0012: Phase 5 minimal motion reducer semantics

Status: **Proposed**

Date opened: 2026-08-28

Date approved: pending

Decision owner: project owner

Needed by: Phase 5 Slice 5.3

Companion architecture record:
[`ADR-0028`](../adr/ADR-0028-phase5-command-validation-and-atomic-reducer.md)

## Decision questions

When the first valid player-root motion intent reaches the Phase 5 reducer,
should it change canonical state at all; if so, does it replace velocity,
integrate position, clamp, or reject representable values; whose entity may it
affect; and how should ordered contention and rejection appear before
production locomotion, collision, prediction, and presentation are designed?

This is a gameplay-behavior and authority decision. GDR-0011 approved only the
meaning and transport of desired velocity and explicitly withheld reducer
behavior. Production reduction must not begin until this record and ADR-0028
are approved or amended.

## Decision summary

No option is accepted yet. The recommendation is Option A for Decisions 1
through 4:

1. an accepted Phase 5 motion command exactly replaces canonical linear
   velocity, preserves the complete transform, and increments the one spatial
   revision once;
2. do not integrate position, clamp velocity, infer cell transitions, or apply
   acceleration/collision rules in this slice; all representable velocity
   components remain structurally valid;
3. authorize only the current active session explicitly bound to that
   player/entity, with matching revision and stable authority epoch; and
4. resolve same-revision contention in writer order, make later stale commands
   final rejections, and keep acknowledgement distinct from success.

The recommendation proves a real canonical reducer while minimizing the rule
that GDR-0004 may later replace. It is not a claim that the accepted velocity is
production movement, a speed entitlement, or a position prediction.

## Existing constraints

1. ADR-0006 makes the server the sole canonical writer and requires the
   friends-oriented structural, authority, revision, epoch, and basic-domain
   validation profile without elaborate anti-cheat simulation.
2. ADR-0016 defines `LinearVelocity3` as signed position quanta per server tick,
   makes every representable component structurally valid, and defers speed,
   acceleration, collision, and teleport rules to GDR-0004.
3. GDR-0011 defines the command as desired linear-velocity intent only. It has
   no position, cell, orientation, duration, grounded state, or canonical claim.
4. ADR-0027 makes cell/root transform and velocity one per-player spatial value
   with one revision and provides a checked atomic replacement operation.
5. Player creation, join/readiness, reconnect/replacement, cell transitions,
   interest, production movement, prediction, correction, persistence, and
   presentation remain later gated work.

## Representative scenarios

1. A bound player at velocity zero submits desired velocity `(10, 0, 0)`. On
   acceptance, velocity becomes exactly `(10, 0, 0)` while cell, position, and
   orientation remain identical and revision increments once.
2. The player then submits zero velocity at the next revision. Canonical
   velocity becomes zero; this means only a stored velocity replacement, not a
   promise about engine braking or animation.
3. A representable negative, vertical, or extreme component is preserved. No
   arithmetic uses it in this slice, and no speed or locomotion permission is
   inferred.
4. Two different players submit valid commands in one tick. Each changes only
   its own bound per-player entity state.
5. Two commands target the same revision. Writer order accepts the first; the
   second is stale and cannot overwrite it.
6. A current session targets another player's entity, an old generation targets
   its former entity, or the revision/epoch is stale. No player spatial value
   changes.
7. A rejected exact-next command advances contiguous-finalized progress under
   ADR-0028 but does not make its requested velocity canonical.
8. A client later receives state publication. Slice 5.3 itself defines no
   visibility, interpolation, engine update, or correction behavior.
9. Disconnect, reconnect, server restart, and persistence do not gain behavior
   from velocity replacement; their owning GDRs/phases remain required.

## Decision 1: accepted motion effect

### Option A: exact velocity replacement with unchanged transform (recommended)

For a command that passes ADR-0028 validation, replace the bound player's
canonical `LinearVelocity3` with the requested value. Preserve cell, position,
orientation, player/entity identity, and authority epoch. Advance the one
spatial revision and last-change tick exactly once using the accepted checked
value operation.

This creates an observable canonical result without treating a client as the
position writer or inventing a timestep duration for each reliable command.

### Option B: integrate position by one tick and replace velocity

Add desired velocity to position once per accepted command. This produces
visible movement, but command rate would become movement distance, multiple
same-tick commands could move multiple times, and cell/collision/overflow rules
would be decided before GDR-0004 and GDR-0003.

### Option C: accept and acknowledge without changing player state

Treat the command as a Phase 5 no-op. This avoids temporary movement behavior,
but does not demonstrate the planned atomic canonical reducer application and
makes acceptance indistinguishable from a protocol probe.

## Decision 2: numeric and locomotion limits

### Option A: preserve every representable velocity; perform no integration (recommended)

All signed 64-bit components are structurally valid as ADR-0016 already states.
Store the exact value and perform no position arithmetic, magnitude calculation,
clamping, axis restriction, acceleration, collision, or grounded check.

GDR-0004 must later select production limits before a runtime integrates or
presents this value. It may deprecate this command behind a new capability.

### Option B: reject above a provisional speed ceiling

This provides plausible values but requires choosing a magnitude norm, axes,
units, locomotion modes, and threshold without representative gameplay evidence.

### Option C: clamp to a provisional ceiling

Clamping always yields a mutation, but silently changes player intent and makes
the temporary threshold a user-visible movement rule.

## Decision 3: authority, state scope, and visibility

### Option A: current explicit session binding, per-player entity state (recommended)

Only the current `SessionId`/`SessionGeneration` explicitly bound in canonical
session progress may propose the bound entity's command. Matching revision and
stable authority epoch are required. The server validates and commits.

The changed velocity is part of that player's one canonical entity reality.
It is not session-scoped, group-scoped, or a different reality per observer.
Slice 5.3 publishes nothing; later interest/publication decisions determine who
observes it. Session acknowledgement remains session-generation state.

### Option B: any authenticated session may target any player entity

This would make preconditions the authority grant and permit one client to move
another player's root, contrary to ADR-0006 and GDR-0011.

### Option C: store requested velocity on the session only

This avoids changing player state but creates different motion realities across
reconnects and observers and makes connection lifetime control canonical scope.

## Decision 4: contention, failure, and acknowledgement presentation

### Option A: writer-order first commit, stale later rejection (recommended)

Validate each ordered command against the latest committed revision. The first
valid same-revision command replaces velocity. Later commands retaining the old
precondition are rejected without a player change. A final rejection may
advance contiguous acknowledgement, which means processed only, not accepted.

No retry, correction, user message, animation, or disconnect behavior is
defined in this slice. Later result/publication layers expose typed outcomes and
canonical state.

### Option B: last command in the tick wins regardless of revision

This reduces perceived input latency but discards the required precondition and
makes a stale command overwrite an already committed canonical result.

### Option C: merge or average same-tick velocities

This is deterministic but invents a gameplay rule not requested by either
client and obscures apply-once command outcomes.

## Proposed behavior acceptance tests and demo

Subject to owner approval or amendment:

1. `accepted_motion_replaces_velocity_but_not_cell_position_or_orientation`
2. `accepted_motion_increments_spatial_revision_exactly_once`
3. `zero_negative_vertical_and_extreme_representable_velocity_are_preserved`
4. `two_bound_players_change_only_their_own_entity_state`
5. `wrong_entity_old_generation_revision_or_epoch_cannot_change_velocity`
6. `writer_order_accepts_first_same_revision_command_and_rejects_second`
7. `rejected_final_command_acknowledges_processing_not_motion_success`
8. `motion_reducer_performs_no_position_integration_clamp_collision_or_cell_transition`
9. `slice53_defines_no_interest_prediction_presentation_reconnect_or_persistence_behavior`

The owner demo should show before/after values for one accepted velocity
replacement, an exact unchanged transform, two-player isolation, same-revision
contention, an extreme value stored without arithmetic, and a rejected command
whose acknowledgement progresses while velocity remains unchanged.

## Cross-cutting consequences

- **Authority, scope, and lifetime:** the current bound session proposes; the
  server validates and commits one per-player entity velocity. Session progress
  is active-generation state. This record sets no disconnect, reset, restart, or
  persistence lifetime for player state.
- **Visibility and resync:** Slice 5.3 publishes nothing and selects no interest
  set. Later snapshots may expose the one canonical value; absence cannot create
  a private reality. Checksum/resync remains Slice 5.5.
- **Protocol and compatibility:** the existing typed motion body is unchanged.
  GDR-0004 may retain it, constrain it, or replace it through the approved
  minor/capability process; there is no TES3MP 0.8 migration behavior.
- **Security and operations:** binding, revision, and epoch failures are closed
  typed categories. Exact representability is not an anti-cheat promise, and no
  raw value becomes a metric dimension or unfiltered log field.
- **Scripting, persistence, and replay:** no script command, event, persistent
  record, or replay mapping lands here. Later adapters must use the same typed
  command and committed domain result, never direct velocity mutation.
- **Desktop and VR:** both produce the same desired-velocity command. Head/hand
  poses and headset motion remain presentation data and cannot change the root.

## Consequences if the recommendation is approved

- Slice 5.3 obtains a real per-player canonical mutation without accepting a
  client-authored position or deciding collision/cell behavior.
- A desired velocity becomes the current canonical velocity exactly; this is a
  narrow provisional behavior that GDR-0004 must review before runtime movement.
- Multiple valid sequential revisions may replace velocity within one tick,
  with deterministic writer order and one revision per accepted command.
- Extreme representable values may appear in headless canonical fixtures, but no
  integration or engine presentation may consume them before later limits land.
- No protocol schema changes, persistence promise, script behavior, or client
  presentation behavior follows from this decision.

## Failure modes and mitigations

- **Velocity replacement is mistaken for movement integration:** preserve and
  test the complete transform exactly.
- **Temporary behavior becomes permanent accidentally:** name GDR-0004 as a
  mandatory review trigger before end-to-end runtime movement.
- **Representable extreme values reach unsafe arithmetic:** perform no arithmetic
  here and prohibit runtime integration before numeric limits are approved.
- **Session binding becomes state scope:** keep velocity on the player entity and
  acknowledgement on the session record.
- **Stale input overwrites a newer result:** validate against the state produced
  by every earlier ordered command.
- **Acknowledgement is shown as success:** retain processed/finalized meaning and
  use canonical state or typed future results for acceptance.

## Review and replacement triggers

Reopen this GDR if:

- GDR-0001 requires a different first headless motion result;
- GDR-0004 selects acceleration, control axes, clamping, simulation, or a new
  semantic command incompatible with exact velocity replacement;
- any runtime begins integrating or presenting canonical velocity;
- content/cell rules require velocity validation before it can be stored;
- multiple controllers or authority delegation becomes permitted;
- persistence gives this provisional velocity restart semantics; or
- player-visible result delivery requires behavior beyond typed disposition and
  canonical publication.

## Owner approval

Pending explicit approval or amendment of Decisions 1 through 4 and the
proposed behavior acceptance tests. ADR-0028 requires independent explicit
approval.
