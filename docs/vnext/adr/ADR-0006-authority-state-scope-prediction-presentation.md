# ADR-0006: Authority, state scope, prediction, and presentation policy

Status: **Accepted**

Date opened: 2026-08-26

Date approved: 2026-08-26

Decision owner: project owner

Needed by: Phase 2

## Decision questions

Which component may propose, validate, and commit each kind of state change;
how is canonical state scoped; what may a client predict; and which values are
presentation-only?

This framework must be decided before Phase 3 fixes the core interfaces and
before the Phase 7 vertical slice hardens session, cell, movement, visibility,
and reconnect behavior. It affects protocol direction, server-core APIs,
client reconciliation, VR composition, scripting, persistence, replay, and the
meaning of later gameplay decisions.

## Decision summary

The project owner approved Option A for Decisions 1 through 5 on 2026-08-26:

1. canonical mutation uses the server command/reducer model;
2. scope and lifetime are explicit, with global physical-world and per-player
   knowledge/progression defaults subject to later domain GDRs;
3. local prediction is reversible presentation reconciled to server results;
4. presentation-only samples are typed, bounded, latest-wins, and non-durable;
   and
5. delegated gameplay authority is absent from the first slice and may later
   use only server-validated, epoch-bound proposal leases.

For Decision 1, the owner explicitly selected a friends-oriented validation
profile. The server remains the canonical writer and retains all structural,
safety, authority, revision, idempotency, and basic domain validation, but the
project will not build elaborate anti-cheat rules or reproduce the complete
OpenMW engine simulation merely to police client inputs.

## Accepted constraints

Every viable option must preserve the direction already accepted in the vNext
README, implementation plan, ADR-0003, ADR-0004, and ADR-0005:

1. The dedicated server is the only writer of durable canonical state.
2. Authenticated clients and every value they send remain untrusted.
3. Durable changes pass through one bounded, validated, atomic server command
   path. Client, script, administrative, persistence, and replay integrations
   receive no bypass mutation API.
4. Apply-once operations use command identities and expected revisions. Sampled
   movement and pose use a separate latest-wins class and cannot acquire durable
   semantics through retransmission.
5. Protocol and server-core types remain independent of OpenMW, rendering, VR,
   operating-system, and transport-library types.
6. Desktop and VR clients express the same semantic player commands. Head and
   hand poses do not silently move the authoritative player root.
7. Any delegated authority is explicit, finite, epoch-bound, revocable, and
   transferred with a complete atomic handoff snapshot.
8. State is completely decoded, authorized, and validated before canonical
   mutation. Rejecting a command cannot expose a partial commit.

These constraints rule out peer-to-peer canonical ownership and direct client
state writes. The decisions below still determine how the accepted server
authority is exposed and how scope, prediction, presentation, and future
delegation are modeled.

## Terms that must remain distinct

- **Proposal authority:** who may submit an intent or observation for
  consideration.
- **Validation authority:** who checks identity, permission, preconditions,
  content, numeric rules, rate, and current canonical context.
- **Commit authority:** the one component that atomically assigns the new
  canonical value and revision.
- **State scope:** which canonical reality changes: global world, one player,
  an explicitly modeled group, or an explicitly modeled world instance.
- **Visibility/interest:** which clients receive a representation of canonical
  state. Hiding a value from a client does not make the value per-player.
- **Lifetime:** durable world/player data, resumable session data, transient
  sampled data, or client-local presentation data. Lifetime is independent of
  scope.
- **Prediction:** a reversible client-local presentation estimate that must
  reconcile to server results.
- **Delegation:** a server-issued, time-bounded and epoch-bound right to propose
  a narrowly defined simulation result. Delegation never transfers commit
  authority.

## Representative scenarios

1. **Single-player movement:** one client supplies semantic movement intent. A
   responsive local presentation may move immediately, but only the server
   result changes the canonical player root.
2. **Two players in one cell:** both players move concurrently and observe one
   another. Neither connection may write the other player's root, and interest
   filtering cannot create two canonical versions of the same shared value.
3. **Late join:** a new client receives a server-produced canonical snapshot
   plus presentation metadata. It never asks an existing client which state is
   authoritative.
4. **Reconnect:** a client resumes after predicting beyond its last accepted
   command. The new connection generation receives canonical state and
   acknowledgements; unconfirmed presentation is discarded or replayed only as
   newly valid commands.
5. **Contention:** two authorized commands target one revisioned shared entity.
   A deterministic server decision commits at most one applicable transition;
   selective replication cannot turn the conflict into per-client realities.
6. **Malformed or cheating client:** an authenticated client sends another
   player's identity, impossible values, a fabricated snapshot, a stale
   revision, or a stale authority epoch. The server rejects it before mutation.
7. **VR presentation:** head and hand samples are bounded and distributed as
   optional transient presentation data. A headset movement alone cannot move
   the canonical root or create VR-only gameplay authority.
8. **Future actor delegation:** a client that was simulating an actor
   disconnects while delayed results remain in flight. A later owner uses a new
   epoch and complete handoff state; the old epoch cannot commit.
9. **Script, administration, and replay:** each source submits typed commands to
   the same validation and commit boundary. Privilege may change authorization,
   but it does not grant a mutable state view.
10. **Resynchronization:** a checksum or revision mismatch causes a bounded
    server snapshot/resync. A client cannot repair the server by uploading its
    local engine state.

## Decision 1: canonical mutation and client proposal model

### Option A: server command/reducer model (approved)

Clients submit bounded semantic intents or observations. The authoritative
server core validates identity, authority, current revision, content and
numeric rules, then applies an atomic reducer and assigns canonical revisions.
Server-produced snapshots and changes flow outward; client-authored canonical
snapshots are never accepted.

This most directly implements the accepted single-writer and hostile-client
constraints. It supports deterministic tests and replay, keeps OpenMW state out
of the core, and gives scripts, administrators, and future replay one common
mutation boundary. Its cost is that the engine-independent server must own
enough domain rules to validate and reduce every canonical change.

The approved validation depth is intentionally modest. Mandatory checks cover
bounded and well-formed input, current session and proposal authority, valid
identities and representations, finite values, command/revision/idempotency/
session/epoch context, basic domain preconditions, and coarse rate or magnitude
bounds needed for safe and coherent shared state. The server remains the final
writer when those checks are coarse.

Comprehensive cheat detection, client-binary integrity, behavioral scoring,
and exact reproduction of all OpenMW physics, collision, scripts, or emergent
engine behavior are not goals. Later GDRs may require additional validation
where it is necessary for consistent shared gameplay, atomicity, or process
safety; anti-cheat complexity is not added by default.

### Option B: client-computed state with server plausibility checks

Each client sends resulting transforms or other state for its player, and the
server accepts values that pass rate and range checks. This is easier to bridge
to OpenMW and can reduce server simulation work, but it makes canonical results
depend on untrusted engine execution, weakens deterministic replay, and turns
plausibility thresholds into the real authority model. It also creates a poor
precedent for combat, inventory, and world objects.

This option conflicts with the semantic-command and canonical-integrity
direction unless restricted to explicitly presentation-only samples.

### Option C: client simulation owners with server forwarding

The server assigns entities or cells to clients and forwards the assigned
client's resulting state. This resembles the archived lightweight-server model
and can reuse client engine simulation, but it makes disconnect, malicious
owners, inactive cells, contention, handoff, and replay core correctness risks.
It also couples canonical behavior to OpenMW client timing and content.

This option conflicts with the accepted dedicated authoritative-server model.

## Decision 2: canonical state-scope model

### Option A: explicit scope and lifetime, with bounded defaults (approved)

Every canonical value declares both its scope and lifetime in domain types.
Durable physical world state defaults to global scope; player knowledge,
identity, and progression default to per-player scope. Group and world-instance
scope remain unavailable until their identities, membership, lifecycle,
visibility, persistence, and migration rules are approved. A domain GDR may
approve an exception to a default before that domain is implemented.

Interest is a derived delivery decision over canonical state, not a state
scope. Per-player or future group/instance state is represented with explicit
canonical keys rather than by suppressing a global update on selected clients.

This option keeps contention and persistence comprehensible and lets two-client
tests prove scope directly. It requires each domain to make scope visible in its
state and commands instead of relying on sender or connection context.

### Option B: one globally shared durable world

All durable state, including knowledge and progression, is global. This is
simple to replicate and persist, but one player's journal, dialogue knowledge,
or character progression would affect everyone. It removes familiar
individual progression and makes private or party gameplay difficult to add.

### Option C: implicit per-client realities through visibility filtering

The server stores or forwards one nominal state while hiding selected changes
to create per-player behavior. This minimizes explicit scope types initially,
but reconnect, late join, scripting, persistence, resync, and contention cannot
reconstruct which reality is canonical. It is rejected as an implementation
shortcut rather than a coherent scope model.

### Option D: global, player, group, and instance scope from the first slice

All four scopes become first-class immediately. This offers maximum early
flexibility, but group and instance identity, membership changes, teardown,
cross-scope references, persistence, and visibility are not yet product
requirements. It adds a large untested state-space before the first vertical
slice and risks accidental instancing behavior.

## Decision 3: prediction, interpolation, and correction

### Option A: speculative local presentation with authoritative reconciliation (approved)

The controlling client may predict only effects necessary for responsive local
presentation. Prediction is tagged with the originating command/sequence and is
never durable evidence. Server acknowledgements and snapshots reconcile the
local view; remote entities are normally rendered through a bounded
interpolation/extrapolation policy over server snapshots. Detailed correction,
collision, teleport, and latency behavior remains for GDR-0001 and GDR-0004.

Apply-once durable effects are not shown as committed before server acceptance.
A UI may show a clearly pending attempt, but it cannot present success as final
or cause follow-on canonical actions without the accepted result.

This option preserves responsiveness while keeping one canonical reality. It
requires command correlation, bounded prediction history, correction policy,
and tests that distinguish presentation from canonical state.

### Option B: no local prediction

Every local visible result waits for a server snapshot. This is easy to reason
about and minimizes reconciliation code, but ordinary Internet latency would
make movement and interaction feedback sluggish, especially in VR. It remains
useful as a test/reference mode, not the recommended player experience.

### Option C: optimistic client commit until challenged

The client treats predicted outcomes as committed and rolls them back only when
the server objects. This feels responsive but blurs authority, can expose false
durable outcomes, and creates rollback chains across inventory, combat,
scripting, and persistence. It is incompatible with the single canonical
writer when applied beyond reversible presentation.

## Decision 4: presentation-only sampled state

### Option A: typed, bounded, non-durable samples (approved)

Presentation-only values such as VR head/hand pose are distinct message and
state types. The server validates their sender, shape, range, rate, age, and
interest before latest-wins distribution, but does not persist or replay them
as canonical world state. Missing, stale, or unsupported samples degrade to a
defined desktop-compatible presentation and never block canonical simulation.

This keeps VR optional and cross-platform while still preventing a client from
using a presentation channel to move the authoritative root or invoke gameplay.

### Option B: include presentation samples in canonical player state

This makes all observers share one revisioned pose and eases snapshot assembly,
but it expands durable state with high-rate platform-specific data, couples
gameplay revisions to sample frequency, and complicates desktop fallback,
replay, and persistence.

### Option C: peer-to-peer or unmediated presentation forwarding

Clients exchange presentation samples outside the authoritative session. This
may reduce server bandwidth, but creates a second connection, identity,
security, interest, and abuse boundary and conflicts with the selected
dedicated-server transport milestone.

## Decision 5: delegated simulation authority

### Option A: absent from the first slice; later explicit leases only (approved)

The Phase 7 vertical slice has no delegated gameplay authority. Later actor or
other domain GDRs may introduce proposal leases that name the subject, grantee,
allowed result class, expiry, and monotonically increasing epoch. The server
retains validation and commit authority. Transfer/revocation requires a
complete server-owned handoff snapshot, and stale epochs fail before mutation.

This keeps the first slice small and makes future delegation an explicit
optimization or gameplay decision rather than a hidden dependency. It may
require more server-side simulation work before actor delegation is available.

### Option B: implicit active-cell client ownership

Whichever client currently loads a cell simulates its actors and objects. This
can leverage OpenMW directly, but ownership changes with view/loading state,
has no durable identity or safe handoff, and gives a malicious or stalled client
broad influence. It repeats the ambiguity this clean-break architecture is
intended to remove.

### Option C: generalized leases in the first vertical slice

Build a reusable lease/epoch/handoff subsystem before any feature requires it.
This could reduce later rework, but the first slice contains only player roots
whose clients should not receive canonical commit authority. Without actor or
object scenarios, the abstraction and lifecycle would be speculative.

## Approved framework by initial subsystem

This table records the result of the owner's approval of Option A for all five
decisions. Domain-specific behavior remains gated by the GDRs below.

| Subsystem/value | Proposal source | Validation and commit | Scope and lifetime | Client presentation |
|---|---|---|---|---|
| Principal and connection | Authentication/transport boundary | Owned session boundary; never gameplay authority | Connection/session metadata, non-durable | Connection UI only |
| Player identity | Authenticated session and approved lifecycle commands | Server command/reducer path | Explicit per-player durable identity; details in GDR-0002 | Server-confirmed identity |
| Canonical cell and player root/velocity | Controlling player sends semantic intent | Server validates and commits revisions | Per-player entity state; durability and transition details in GDR-0001/GDR-0004 | Local prediction/reconciliation; remote interpolation |
| Acknowledgement/resume generation | Each endpoint reports protocol/session progress | Owned session state machine | Connection/session lifetime, not gameplay state | Diagnostics and recovery UI |
| Remote-player rendering | None; derived from server snapshots | No canonical mutation | Client-local presentation | Bounded interpolation/extrapolation |
| VR head/hand pose | Controlling VR client sends samples | Server validates and latest-wins distributes | Transient per-player presentation sample | Optional pose or defined fallback; never root authority |
| Physical world objects | Domain command sources | Server command/reducer path | Global durable by default; each domain GDR decides exceptions | Interest-filtered canonical result plus local effects |
| Knowledge and progression | Authorized player/script commands | Server command/reducer path | Per-player durable by default; GDR-0009 decides details | Private/shared visibility follows explicit rule |
| Actors and AI | Initially server; later lease grantee may propose | Server always validates and commits | Global durable by default; GDR-0005 decides lifecycle/delegation | Snapshot interpolation and local animation |
| Scripts, administration, replay | Typed domain commands | Same authorization, validation, ordering, revision, and atomic commit path | Scope/lifetime declared by the target domain | Typed results/events, never mutable state |
| Persistence | Committed records and snapshots only | Cannot originate an online mutation except through validated restore/replay boundaries | Preserves declared scope, identity, revision, and lifetime | None |

## Domain questions deferred to GDRs

ADR-0006 supplies vocabulary and hard boundaries. It does not approve these
player-visible choices:

- **GDR-0001:** spawn point and readiness; interior/exterior entry authority;
  when players become visible; semantic movement intent for the first slice;
  local/remote correction presentation; disconnect grace; and what a resumed
  or replaced connection observes.
- **GDR-0002:** player identity creation and lifetime; character-generation
  ownership; duplicate/replacement connections; content mismatch; kick/ban and
  reconnect consequences; and which lifecycle facts persist.
- **GDR-0003:** canonical cell identity; transition validation; interest radius
  and visibility; initial snapshot boundary; revision/checksum mismatch; resync
  replacement; and inactive-cell handling.
- **GDR-0004:** movement inputs and server validation; collision and numeric
  tolerances; prediction/correction thresholds; teleports; animation state;
  sample rates; and VR root, head, and hand semantics.
- **GDR-0005:** actor creation and deletion; AI simulation rules; unloaded-cell
  behavior; lease eligibility and selection; expiry/revocation/handoff;
  contention; and behavior when no eligible grantee exists.
- **GDR-0006:** activation and placed-object result scope; object creation and
  movement; door/lock/trap contention; reset/restock; destination overrides;
  and which effects are durable versus local presentation.
- **GDR-0007:** item ownership; pickup/drop/trade scope; container visibility;
  concurrent looting; equipment consequences; restock; and transaction failure.
- **GDR-0008:** hit and damage authority; lag handling; projectiles and effects;
  friendly fire; death/killer attribution; resurrection; and rollback or
  compensation presentation.
- **GDR-0009:** dialogue eligibility; learned topics; journal/faction/quest scope;
  shared kill counts; party behavior if introduced; and progression visibility.
- **GDR-0010:** game-clock ownership and pause/rate rules; weather scope;
  offline evolution; globals; reset; scheduled events; and persistence across
  restart.

If a GDR needs a new scope kind, client-originated durable authority, a bypass
mutation path, or a different meaning of prediction/delegation, ADR-0006 must be
reopened before dependent production work.

## Approved acceptance tests and demo

Later slices must implement these as named tests at the earliest owning phase:

1. `client_cannot_submit_canonical_snapshot`
2. `authenticated_client_cannot_write_another_player_root`
3. `rejected_command_does_not_partially_mutate_or_increment_revision`
4. `duplicate_apply_once_command_commits_once`
5. `stale_revision_and_stale_session_generation_cannot_mutate`
6. `late_join_uses_server_snapshot_not_peer_state`
7. `interest_filter_does_not_change_canonical_scope`
8. `local_prediction_does_not_mutate_server_state`
9. `server_correction_replaces_unconfirmed_presentation`
10. `vr_pose_sample_cannot_move_authoritative_root`
11. `missing_or_stale_pose_uses_platform_neutral_fallback`
12. `latest_wins_sample_is_not_persisted_or_replayed_as_durable_state`
13. `script_admin_and_replay_have_no_bypass_mutation_api`
14. `stale_delegation_epoch_cannot_commit_after_handoff` when delegation lands
15. `resync_replaces_client_view_without_accepting_client_state`

The owner demo for the Phase 7 behavior review should run two fake clients
through join, shared-cell visibility, independent movement, an injected stale
or fabricated update, disconnect/resume, and canonical resync. A VR-capable
fake client should then change head/hand pose while its authoritative root
remains fixed. The trace must distinguish proposed commands, accepted canonical
changes, rejected inputs, and client-local presentation.

Exact tick rate, numeric representation, total ordering, compatibility window,
and capability negotiation belong to Slice 2.6. Exact queue sizes, correction
thresholds, movement rules, interest rules, and reconnect grace belong to their
own implementation slices and GDRs. The acceptance names above constrain
semantics without prematurely fixing those values.

## Consequences of the approved decision

- The server core needs domain commands and reducers rather than a generic
  replicated-property or packet-forwarding API.
- State APIs must encode scope and lifetime explicitly; connection identity and
  interest membership cannot supply them implicitly.
- Client session APIs need separate confirmed canonical state, bounded
  speculative presentation, and reconciliation metadata.
- Server snapshots flow to clients. Client inputs flow as commands or explicitly
  typed transient samples, never as trusted canonical snapshots.
- Persistence and replay record domain state/changes and accepted command
  ordering, not client engine memory or presentation samples.
- Desktop and VR compose the same canonical root and command model; VR adds only
  optional bounded presentation samples.
- Future delegation can optimize simulation without transferring canonical
  commit authority, but it carries explicit lease, epoch, handoff, fallback,
  and test costs.

## Failure modes and mitigations

- **Scope inferred from sender:** require explicit scoped keys/domain types and
  multi-client tests for every durable feature.
- **Interest mistaken for ownership:** test canonical results independently of
  recipients and reconstruct them on late join/resync.
- **Prediction leaks into durable state:** isolate presentation buffers and
  permit canonical changes only from server results.
- **Client snapshot accepted during reconnect:** make resumption server-to-client
  and bind all client commands to the current session generation.
- **High-rate sample grows queues or revisions:** use bounded latest-wins storage
  and keep samples out of durable entity revisions unless a later approved rule
  promotes a specific value.
- **VR pose changes gameplay accidentally:** calculate canonical movement from
  semantic commands and the platform-neutral root; test head/hand movement with
  a fixed root.
- **Privileged bypass corrupts replay:** expose commands and immutable committed
  events to scripts/admin/persistence, not mutable state.
- **Delegated owner stalls or lies:** keep server validation/commit, expire and
  revoke leases, require epoch and complete handoff, and retain a server-owned
  fallback policy.
- **Nondeterministic conflict winner:** Slice 2.6 must define server ordering;
  never use client wall clocks, packet arrival races, hash iteration, or
  presentation timing as canonical tie-breakers.

## Review and replacement triggers

Reopen this ADR if:

- listen-server, peer-to-peer, federation, offline merge, or client-authored
  canonical state is proposed;
- a domain requires a scope other than global, player, explicitly modeled group,
  or explicitly modeled world instance;
- an engine limitation prevents semantic commands or server validation without
  making OpenMW client state authoritative;
- prediction must cross a durable/apply-once boundary;
- a presentation sample acquires a gameplay, persistence, or replay consequence;
- a script, administrator, persistence adapter, or replay tool requires direct
  mutable-state access; or
- a delegation cannot be expressed as a finite, epoch-bound proposal lease with
  server validation and atomic handoff.

## Owner approval

Approved by the project owner in the 2026-08-26 working session: Option A for
Decisions 1 through 5.

For Decision 1, the owner required basic domain validation suitable for a
friends-oriented game server and explicitly declined overcomplicated
client-input policing where cheating is not a major concern. This condition
does not waive bounded decoding, process/resource safety, session and proposal
authority, stable identity, finite/numeric validity, revision/idempotency/
epoch checks, atomic mutation, or the basic preconditions required for coherent
shared state. It does exclude elaborate anti-cheat systems and full duplicate
implementation of OpenMW mechanics solely for cheat detection.

This approval establishes the architecture framework only. It does not approve
the domain-specific gameplay questions listed above, which remain gated by
their GDRs and behavior reviews.
