# ADR-0016: Canonical spatial, command, and snapshot primitives

Status: **Accepted**

Date opened: 2026-08-26

Date approved: 2026-08-26

Decision owner: project owner

Needed by: Phase 3 Slice 3.3

## Decision questions

What engine-independent shape should identify an interior or exterior cell; in
which coordinate frame do fixed-point positions and rotations live; which
velocity components enter the first canonical API; and how much command and
snapshot structure should Slice 3.3 define before the protocol and gameplay
phases own their semantics?

These choices are required before Slice 3.3 adds public headers. The primitive
layer must be useful to the headless harness without silently deciding content
manifest identity, cell transitions, movement rules, authority, interest,
transport delivery, or schema layout.

## Decision summary

The project owner approved Option A for Decisions 1 through 5 on 2026-08-26:

1. use a tagged interior/exterior `CellId` whose opaque `CellSpaceId` resolves
   inside an external content context;
2. use contextual world coordinates with OpenMW's spatial axes, exterior
   worldspace-absolute X/Y, and no automatic cell-local renormalization;
3. use three fixed-point position components and three normalized 32-bit turn
   components with a documented right-handed `Rz * Ry * Rx` composition;
4. define linear XYZ velocity in position quanta per server tick and defer
   angular velocity; and
5. define only value-only command/order headers and per-entity snapshot values,
   leaving semantic payloads, collections, codecs, budgets, and delivery
   envelopes to their owning phases.

This approval authorizes only the Slice 3.3 value API and independent tests. It
does not authorize a content-ID algorithm, gameplay or anti-cheat rules, cell
transitions, movement simulation, prediction thresholds, protocol schema,
snapshot transport, persistence, or OpenMW adapter behavior.

## Required boundaries

Every option must preserve ADR-0004, ADR-0006, ADR-0013, ADR-0014, and ADR-0015:

1. Canonical spatial values use checked integer/fixed-point representations;
   floats exist only at adapters/codecs and convert with the approved finite,
   range, and ties-to-even rules.
2. The types compile without OpenMW, OpenSceneGraph, Bullet, renderer, VR,
   FlatBuffers generated code/runtime, selected transport, or platform types.
3. Client input is a command or an explicitly transient sample, never a
   client-authored canonical snapshot. The server remains the sole canonical
   writer.
4. Construction performs structural and representational checks only. Ordinary
   friends-server domain validation is sufficient; collision, speed,
   acceleration, transition, reach, and anti-cheat policy remain GDR-0003/
   GDR-0004 work.
5. Phase 10 owns content manifests and canonical record identity. GDR-0003 and
   Phase 11 own final cell normalization, limits, collision handling, interest,
   and transitions.
6. Phase 4 owns schema layout, decode budgets, dynamic snapshot collections,
   delivery classes, and compatibility behavior. Phase 5 owns storage,
   reducers, mutation, and immutable snapshot publication.
7. All composite values are fully initialized, compare by semantic fields, and
   neither allocate identity nor normalize by consulting ambient engine state.

## Repository evidence

OpenMW 0.51 currently represents legacy positions as three `float` positions
and three radian rotations in `ESM::Position`. Exterior lookup floors absolute
X/Y by a worldspace-dependent cell size: 8192 units for the default Morrowind
worldspace and 4096 for ESM4 worldspaces. `ESM::ExteriorCellLocation` combines
signed grid X/Y with an `ESM::RefId` worldspace, while interior cells use record
identity. OpenMW object orientation converts the stored components through
engine-specific negative-axis quaternion composition, and actors use a narrower
subset in several paths.

Those are adapter facts, not suitable shared types. In particular, copying
`ESM::RefId`, `osg::Vec3f`, `osg::Quat`, cell-size lookup, or actor/object
rotation special cases into the independent protocol layer would violate the
approved boundary and prematurely make engine behavior canonical.

## Scenarios evaluated

1. A headless fixture creates one interior cell and adjacent exterior cells
   without OpenMW content or renderer types.
2. Two different content contexts assign the same numeric cell-space value.
3. An exterior position crosses a grid boundary while retaining continuous
   world coordinates.
4. A negative exterior coordinate maps consistently when Phase 11 later checks
   membership.
5. An adapter receives a finite position exactly halfway between two canonical
   quanta.
6. A transform uses all three rotation axes even though the first player-root
   behavior may permit fewer axes.
7. A velocity component is negative or reaches a checked-arithmetic boundary.
8. A client command carries source identity and ordering observations but no
   canonical state claim.
9. The writer records eligible tick and ingress ordinal after admission.
10. A per-entity snapshot carries confirmed spatial state without selecting a
    vector budget, wire channel, or gameplay command.

## Decision 1: cell identity shape and content context

### Option A: tagged cell plus context-scoped opaque cell-space identity (approved)

Add a nonzero `CellSpaceId` strong `u64` whose uniqueness is scoped to one
negotiated content context. Define `CellId` as a closed tagged value with:

- interior: one `CellSpaceId` naming the interior cell record; or
- exterior: one `CellSpaceId` naming the worldspace plus signed 32-bit grid X
  and grid Y.

The content context is carried by the session/replay/world boundary rather than
repeated in every `CellId`. It is part of interpreting the value and must match
before cells from different contexts are compared operationally. Phase 10
defines how content records receive `CellSpaceId` values and how collisions or
mismatches fail. Phase 11 defines coordinate limits, normalization, membership,
and transition validity.

The tag participates in equality and canonical ordering. Interior values have
no hidden grid coordinates. Exterior ordering is tag, worldspace, grid X, then
grid Y. There is no magic default-worldspace string, display name, file path,
or OpenMW reference in the type.

This preserves the semantic distinction and exterior adjacency needed by later
interest work while postponing content mapping to its owner. It adds one new
semantic scalar and requires callers to retain the external content context.

### Option B: one opaque `u64` `CellId`

Assign every interior and exterior cell one flat numeric identity. This is the
smallest primitive and makes content mapping wholly external, but exterior
adjacency, worldspace, and grid identity require a second lookup everywhere.

### Option C: embed a canonical content-record key in every cell

Store a bounded normalized record key or content hash directly in `CellId`,
plus exterior coordinates. Values become self-describing, but Slice 3.3 would
choose Phase 10's record-key width, normalization, collision, and manifest
semantics prematurely and enlarge every spatial value.

### Option D: reuse OpenMW cell and reference types

Use `ESM::RefId`, `ESM::CellId`, or `ESM::ExteriorCellLocation`. This minimizes
adapter conversion but imports engine representation, strings, default
worldspace conventions, and content-version behavior into shared targets.

## Decision 2: position frame and exterior cell relationship

### Option A: contextual world coordinates with absolute exterior X/Y (approved)

Use a right-handed spatial frame with +X east, +Y north, and +Z up. A position
is interpreted with its `CellId`:

- exterior X/Y are continuous absolute coordinates in that worldspace, and Z
  uses that worldspace's vertical datum; and
- interior XYZ are coordinates in the content-defined frame of that interior.

Each component is the ADR-0013 signed 64-bit `1/1024` OpenMW-unit value.
Crossing an exterior grid boundary changes cell membership but does not
renormalize the position to a cell-local origin. Cell size and membership are
not embedded in the primitive; Phase 10/11 resolve the content context and
validate that `CellId` and position agree.

This matches OpenMW's existing exterior position model, avoids a discontinuity
at cell borders, and keeps the adapter mechanical. The contextual interior and
exterior frames mean positions from different cells cannot be subtracted
without an owning rule.

### Option B: cell-local XYZ for every cell

Store exterior X/Y relative to the cell origin and renormalize both cell and
position at boundaries. Values stay locally bounded, but every exterior move
needs checked cross-cell normalization and adapter conversion despite the
64-bit range already providing ample headroom.

### Option C: one universal global frame for interiors and exteriors

Assign every interior a location in a global coordinate system. Cross-cell
math becomes uniform, but Morrowind interiors do not have a reliable canonical
global placement and multiple doors may lead to the same interior.

### Option D: preserve OpenMW binary floats

Copy engine float positions and infer cell membership from them. This is easy
at the adapter but violates ADR-0013's exact canonical numeric decision.

## Decision 3: orientation and transform shape

### Option A: fixed-point XYZ Euler components with fixed composition (approved)

Define small semantic components:

- `Position3` holds three signed 64-bit position quanta;
- `Turn32` holds one unsigned 32-bit turn, where all bit patterns are valid and
  arithmetic normalization is modulo one full turn; and
- `Orientation3` holds X, Y, and Z turn components.

For a full orientation, components are right-handed rotations around the
canonical positive axes and compose as `Rz(z) * Ry(y) * Rx(x)`. `Transform`
contains `CellId`, `Position3`, and `Orientation3` and nothing else. OpenMW's
negative-axis/radian and actor-specific conventions are adapter conversions,
not canonical rules.

Keeping all three axes makes the value reusable for player roots, actors, and
objects. GDR-0004 later decides which axes a movement command may affect and
any pitch limits; the primitive does not grant that behavior.

### Option B: player-root yaw and pitch only

Model only the axes expected in the first movement slice. This is compact but
turns a general transform primitive into a gameplay decision and requires a
second transform family for objects or future actors.

### Option C: normalized fixed-point quaternion

Avoid Euler composition and singularities with four signed fixed components.
This requires a normalization scale, checked normalization algorithm, sign
canonicalization, and conversion proof not selected by ADR-0013.

### Option D: mirror `ESM::Position` rotation components

Store radian floats or reproduce OpenMW's negative-axis composition. This
couples the canonical API to an engine convention and its actor/object special
cases.

## Decision 4: velocity contents and units

### Option A: linear XYZ velocity only (approved)

Define `LinearVelocity3` as three signed 64-bit position quanta per 30 Hz server
tick, exactly as ADR-0013 approved. All representable components are
structurally valid; checked reducers and later GDR-0004 rules own speed,
acceleration, collision, and teleport limits. The type performs no clamping,
integration, or conversion from per-second units.

Angular velocity is absent until a demonstrated gameplay or interpolation need
owns its semantics. Orientation may still appear in each confirmed snapshot.

### Option B: linear and angular velocity together

Add three signed turn-delta components per tick. This anticipates rotating
objects and extrapolation but selects wrap, shortest-path, and per-axis behavior
without a current consumer.

### Option C: position delta instead of velocity

Represent displacement between two snapshots. This avoids the word velocity
but makes meaning depend on an external interval and complicates missed ticks.

### Option D: scalar speed and direction

Store magnitude plus heading. This is compact for simple locomotion but loses
vertical and arbitrary vector motion and introduces deterministic vector math.

## Decision 5: command and snapshot primitive depth

### Option A: value-only metadata and per-entity snapshot records (approved)

Define no payload variant, dynamic collection, codec, or delivery envelope in
Slice 3.3. Provide only fully initialized value records:

- a client command header containing `SessionId`, `SessionGeneration`,
  `CommandSequence`, `CommandId`, and the client's observed `ServerTick`;
- an optional entity precondition value containing `EntityId`, expected
  `EntityRevision`, and expected `AuthorityEpoch`, used only by commands whose
  owning domain requires it;
- a writer admission stamp containing server-assigned eligible `ServerTick` and
  `IngressOrdinal`; and
- a spatial entity snapshot value containing authoritative `ServerTick`,
  `EntityId`, `EntityRevision`, `AuthorityEpoch`, `Transform`, and
  `LinearVelocity3`.

These records express ADR-0006/ADR-0013 provenance and confirmed state without
claiming that every future command targets an entity. Optionality belongs in
the containing semantic command, not as invalid sentinel fields. Phase 4 adds
bounded message bodies, collections, acknowledgement fields, schema codecs,
and reliable/latest-wins delivery envelopes. Phase 5 creates accepted-command
records, canonical storage, reducers, and published snapshots.

### Option B: define the first movement command and world snapshot now

Add locomotion intent, acknowledgement, entity arrays, and world/cell snapshot
messages. This advances the Phase 4/5/7 work but would decide movement behavior,
collection budgets, and delivery semantics before their gates.

### Option C: generic command and snapshot variants

Create extensible tagged unions or type-erased payload containers. This avoids
specific gameplay choices but creates a second schema/type system, allocation
policy, and unknown-case behavior before FlatBuffers owns those concerns.

### Option D: use generated FlatBuffers tables as the primitives

Expose schema types directly. This reduces conversion code but couples domain
and test APIs to generated views, buffer lifetimes, and the codec runtime and
violates ADR-0004's owned-value conversion boundary.

## Approved acceptance tests

Slice 3.3 must add tests named for these contracts:

1. `interior_and_exterior_cell_ids_are_distinct_and_totally_ordered`
2. `cell_id_has_no_openmw_string_path_or_transport_dependency`
3. `same_cell_space_value_in_different_content_contexts_is_not_interchangeable`
4. `position_uses_signed_i64_one_over_1024_unit_components`
5. `exterior_boundary_crossing_does_not_renormalize_world_position`
6. `turn32_covers_exactly_one_modular_turn`
7. `orientation_composition_and_axis_sign_match_canonical_vectors`
8. `transform_contains_only_cell_position_and_orientation_values`
9. `linear_velocity_uses_signed_position_quanta_per_server_tick`
10. `spatial_values_round_trip_through_owned_test_encoding`
11. `client_command_header_contains_no_canonical_snapshot_payload`
12. `writer_admission_stamp_is_separate_from_client_observation`
13. `entity_precondition_is_explicit_and_optional_at_the_command_boundary`
14. `spatial_entity_snapshot_is_value_only_and_fully_initialized`
15. `primitives_compile_without_openmw_osg_bullet_flatbuffers_transport_or_vr_headers`

The owned test encoding is a deterministic byte fixture in test support, not a
released wire schema. The implementation demo must show interior/exterior and
negative-grid cases, exact numeric boundaries, an orientation basis-vector
case, command/admission separation, snapshot round-trip, and an independent
target build.

## Consequences of the approved decision

- The headless layers gain concrete spatial and metadata values without engine
  or wire dependencies.
- Cell values preserve the distinction and grid information needed later, but
  remain uninterpretable without the external content context.
- Exterior movement stays continuous across grid boundaries; Phase 11 still
  validates membership and coordinate limits.
- Full three-axis orientation is general enough for later domains, while GDRs
  retain control over which axes commands may change.
- The first velocity type is deliberately small and does not invent angular
  extrapolation behavior.
- Command and snapshot records establish provenance and state vocabulary but do
  not become a generic mutation API or premature protocol model.

## Failure modes and mitigations

- **Numeric cell-space ID mistaken for global content identity:** document its
  content-context scope and require context equality at session/replay/world
  boundaries; Phase 10 owns mapping and collision proof.
- **Cell tag and position disagree:** keep construction structural here and
  require Phase 11 validation before canonical mutation.
- **Exterior position gets converted to cell-local coordinates implicitly:**
  name the absolute frame and test a boundary crossing with unchanged position.
- **Rotation signs follow whichever engine API is nearby:** test canonical
  basis vectors and keep OpenMW conversion in the app-local adapter.
- **Modulo angle arithmetic hides gameplay-invalid pitch:** distinguish
  representational normalization from GDR-0004 domain validation.
- **Velocity overflow during integration:** use checked arithmetic in the
  Phase 5 reducer and reject before mutation.
- **Primitive record becomes a client write path:** expose server snapshots as
  output values only and accept typed commands through the reducer boundary.
- **Snapshot record grows into an unbounded world vector:** leave collections
  and budgets to Phase 4's bounded schema/envelope work.

## Review and replacement triggers

Reopen this ADR if:

- Phase 10 content-identity evidence cannot provide a stable context-scoped
  `CellSpaceId` mapping;
- Phase 11 needs a cell representation that cannot express normalization,
  worldspaces, or collision-safe identity;
- representative coordinate ranges exceed signed 64-bit quanta or require a
  different exterior frame;
- supported adapters cannot implement the canonical orientation mapping
  without ambiguous state;
- deterministic interpolation requires angular velocity in canonical state;
- Phase 4's bounded codec cannot represent the records without semantic loss;
  or
- a value record begins performing allocation, mutation, engine lookup, or
  transport delivery.

## Owner approval

Approved by the project owner in the 2026-08-26 working session: Option A for
Decisions 1 through 5.

Approval fixes the engine-independent value contracts only. Content mapping,
gameplay rules, schemas, canonical storage/reducers, delivery semantics,
persistence, and OpenMW adapter conversion remain gated by their owning phases.
