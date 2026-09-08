# TES3MP vNext state

Updated: 2026-09-08

## Current status

- Phases 0–10: **Complete**
- Phase 11: **Complete**
- Phase 12 discovery: **Complete**
- Phase 12 evidence: **Complete**
- Phase 12 package decision/safety: **Complete**
- Phase 13 discovery: **Complete**
- Phase 13 actor core: **Complete**
- Phase 13 actor composition/replication: **Complete**
- Phase 13 lifecycle proof: **Complete**
- Phase 14 discovery: **Complete**
- Phase 14 canonical core: **Complete**
- Phase 14 interactive-object replication and server composition: **Complete**
- Phase 14 client reception and OpenMW presentation: **Complete**
- Phase 14 client activation and round-trip verification: **Complete**
- Phase 15 discovery: **Complete**
- Phase 15 canonical core: **Complete**
- Phase 15 replication and server composition: **Complete**
- Active work: **None — Phase 15 replication and server composition complete**
- Last pass: **Capability-gated inventory wire protocol, private/public exact-cell projection, ordered transaction intake, atomic server commit, lifecycle delivery, and bounded production content composition**
- Authoritative tracker: [rolling implementation plan](IMPLEMENTATION_PLAN.md)
- Historical evidence: [implementation notes](IMPLEMENTATION_NOTES.md)

The repository now uses a rolling **Now / Next / Later** workflow. Only the
active pass is specified in implementation detail. The next few passes are
tentative, and later work stays at outcome level until evidence makes it timely.
ADRs and GDRs are required only for consequential, hard-to-reverse decisions.

## Phase 11 result

- The approved A/A/A package is implemented in [GDR-0018](gdr/GDR-0018-phase11-production-cell-interest-resync.md)
  and [ADR-0058](adr/ADR-0058-phase11-cell-catalog-interest-baseline.md).
- Manifests now carry at most 256 typed cell spaces and 4,096 sorted unique exact
  cells. Server config declares the catalog and explicit spawn; unknown proposals
  fail before canonical mutation. OpenMW owns complete injective local record maps.
- Interest remains server-owned exact canonical-cell equality. Membership deltas,
  complete views, and the optional pose relay share that predicate.
- Join, resume, and authenticated resync now deliver a reliable complete-membership
  baseline plus a latest-wins scoped snapshot. Client readiness requires both and
  a snapshot revision at or beyond the baseline; a newer baseline atomically
  replaces membership.
- Metadata-only resync requests are session/generation checked and coalesced to one
  pending request. They cannot upload state or mutate canonical state.
- Protocol advances from 1.1 to 1.2 and scalar cell config is replaced. The player
  registry, canonical checksum encoding, authority model, and pose lane do not
  change. Movement, teleport policy, adjacent interest, persistence, and world
  objects remain out of scope.

## Phase 12 discovery result

- The fixture path is traced from shared desktop/VR semantic input through the
  reliable intent, 30 Hz canonical advance, latest-wins snapshots, exact local
  correction, bounded remote smoothing, idle animation, and optional 20 Hz pose.
- Existing bounds are recorded in
  [the movement discovery](PHASE12_MOVEMENT_DISCOVERY.md), with a platform-neutral
  production seam and twelve named proof scenarios. No runtime behavior changed.
- The first safety gate is explicit: desired velocity currently accepts any
  signed 64-bit components, and a later checked integration overflow is fatal to
  the composed server pump. Production movement must validate magnitude before
  canonical mutation.
- Discovery found that production remote motion used a null sink and command
  latency, local correction, tick lag, and pose loss lacked one combined capture.
  The completed evidence baseline below closes that instrumentation gap.

## Phase 12 evidence result

- The behavior-neutral baseline adds one bounded, identity-free client metric
  vocabulary for command acknowledgement/stop latency, local correction, remote
  motion, and pose age/loss. OpenMW retains fixed summaries and emits them only
  at exit; dropped observations cannot alter presentation.
- Server due-tick lag now enters the existing injected typed metric sink on every
  scheduler pump. No protocol, authority, movement, correction, animation, pose,
  interest, or resync behavior changed.
- The deterministic walk/turn/stop capture covers desktop and PC-VR under direct,
  jitter, loss, and stall schedules. Shared non-pose evidence is identical; loss
  reports two pose gaps; stall reaches 100 ms extrapolation, 4,096-quanta remote
  correction, and seven ticks of scheduler lag. These are evidence, not budgets.
- Hardware/content-backed capture is still required before player-facing tuning.

## Phase 12 decision and safety result

- The owner approved A/A/A/A/A: server content collision, manifest movement
  profiles, bounded local replay, bounded adaptive remote playback, and small
  canonical locomotion state with ephemeral pose. [ADR-0059](adr/ADR-0059-phase12-production-movement-architecture.md)
  and [GDR-0019](gdr/GDR-0019-phase12-production-movement-package.md) are accepted.
- Version 1.2 raw velocity is now checked at the canonical reducer. Components
  must stay within 4,096 quanta/tick and magnitude within 4,097; the radial
  allowance covers ties-to-even yaw rounding and is not a production profile.
- Out-of-range exact-next commands final-reject as `MotionOutOfRange`, advance
  acknowledgement, preserve player spatial state, and never reach integration.
- Protocol, collision, profiles, correction, animation, pose, interest, resync,
  persistence, and world-object behavior otherwise remain unchanged.

## Phase 12 movement profile and kernel result

- Content manifests now own bounded sneak/walk/run/jump speeds. Server config and
  OpenMW require the same ordered profile; zero, misordered, malformed, and
  over-limit profiles fail before startup composition.
- A deterministic engine-neutral kernel performs checked fixed-tick integration
  and delegates the resolved position and velocity to an injected server
  collision query. The result preserves canonical cell and orientation; clients
  have no collision-result or root-transform input.
- Version 1.2 raw velocity maps to the manifest walk profile while retaining its
  stricter safety envelope. The executable uses an explicit unobstructed
  compatibility query until a content-backed provider binds at the same seam.
- Protocol schema, exact-cell interest/resync, client prediction, presentation,
  animation, pose, persistence, and world objects are unchanged.

## Phase 12 locomotion input and replay result

- Protocol 1.3 additively carries bounded input tick/sequence, explicit
  sneak/walk/run/jump mode, root-facing intent, and desired velocity. Protocol
  1.2 remains the exact legacy walk path.
- The server requires ordered per-connection input ordinals, validates velocity
  against the negotiated manifest mode, and installs canonical mode/facing only
  through the reducer. Canonical checksum encoding advances to version 3 with
  movement rules version 2.
- The shared client retains at most 128 unacknowledged semantic inputs and
  rebuilds local presentation from each authoritative baseline. Replay never
  re-enters command input; authority, entity, cell, tick, overflow, and explicit
  hard discontinuities clear inapplicable history.
- Desktop and PC-VR use the same semantic input and reconciliation path. Remote
  playback, animation, pose, exact-cell interest/resync, persistence, and world
  objects remain unchanged.

## Phase 12 content collision result

- The dedicated server now requires a bounded V1 collision-content artifact
  whose exact manifest ID and complete exact-cell declarations are validated
  before listen. Missing, oversized, malformed, mismatched, or incomplete
  content fails composition closed.
- Static pre-inflated blocked-root volumes use bounded signed integer AABBs. The
  deterministic provider sweeps each attempted fixed-tick segment in the
  current cell; contact retains the current position with zero velocity, while
  clear movement accepts the kernel attempt.
- Runtime manifest, cell, coordinate, and step-integrity violations return
  collision unavailable before canonical mutation. The provider remains in the
  app layer and binds the existing engine-neutral server query without OpenMW,
  Bullet, client, or protocol types.
- Explicit cells with no solids retain the version 1.2 and demo fixture path.
  Cell traversal, dynamic bodies, gravity, slopes, capsule dimensions, world
  objects, persistence, client replay, presentation, animation, and pose remain
  unchanged.

## Phase 12 remote presentation result

- Latest-wins views additively carry canonical locomotion mode beside the fixed
  spatial entry vector. Missing metadata decodes as legacy walk, non-walk modes
  round-trip in bounded parallel metadata, and version 1.2 payloads remain
  readable. No one-shot trigger enters the snapshot lane.
- Remote playback adapts from two to three ticks using arrival timing, pays added
  delay without reversing presentation, and returns after four stable samples.
  The existing three-tick extrapolation, correction blend, and hard-snap bounds
  remain in force.
- Replicated actors select idle, sneak, walk, run, direction, and jump loops from
  canonical mode/velocity with content fallbacks and zero root accumulation.
  Animation remains presentation-only and cannot author canonical transform.
- Optional remote pose stays latest-wins and ephemeral. Shared presentation now
  emits a pose weight: full through 100 ms, blended to canonical locomotion over
  the existing 66.7 ms correction window, then fully canonical. Missing pose is
  canonical immediately; pose still cannot move root or prove reach.
- These are bounded fixture defaults, not ratified production tuning. Hardware
  and representative-content desktop/PC-VR capture remains required.

## Phase 12 capture readiness result

- A test-only five-second desktop route walks, turns, stops, and exits before a
  stale tail can overflow or dominate the fixed metric sink. A bounded localhost
  UDP relay supplies fixed direct, alternating 5/45 ms jitter, every-twentieth-
  datagram loss, and one 250 ms bidirectional stall schedules.
- Representative Morrowind content passes all four desktop profiles with two
  peers, complete metric sets, one initial-placement hard snap per client, and no
  sink drops. The retained maxima are recorded in
  [the hardware capture record](PHASE12_HARDWARE_CAPTURE.md); they are not budgets.
- Owner-approved Option A was rehearsed in a disposable worktree. The maintained
  `vnext-vr` branch now includes shared Phase 10–12 content, protocol, collision,
  replay, presentation, and metric composition at merge `223d5a74e9`, while
  retaining the fork-local OpenXR providers and startup path.
- No headset/HMD device or SteamVR compositor was present. PC-VR metrics, visual
  animation/fallback review, and player-facing budget ratification remain blocked
  on a connected headset. No movement or presentation policy changed.

## Phase 13 discovery result

- Current canonical state, reliable membership, latest-wins views, resync, and
  presentation are player-shaped. Actors need separate stable identity and
  canonical state; they cannot reuse player/session identity without breaking
  authority and compatibility semantics.
- The recommended first package uses a manifest-bound bounded actor catalog,
  separate additive actor records, the existing server movement/collision seam,
  server-owned idle plus waypoint travel/wander, exact-cell inactive freeze, and
  no client delegation. Renderer state remains presentation-only.
- Alternatives are active-cell client proposal leases with atomic handoff, or a
  broad always-running server simulation. Both materially expand authority,
  security, content-parity, and gameplay obligations.
- The decision surface and twelve named proofs are recorded in
  [the actor discovery](PHASE13_ACTOR_DISCOVERY.md). No runtime behavior changed.
  Phase 12 PC-VR capture is deferred until the first actor desktop/VR presentation
  demo, or Phase 22 stabilization at the latest.

## Phase 13 actor core result

- The owner approved Package A in [ADR-0060](adr/ADR-0060-phase13-server-owned-actor-foundation.md)
  and [GDR-0020](gdr/GDR-0020-phase13-actor-lifecycle-and-ai.md).
- New strong actor/prototype identities and a manifest-bound catalog keep actors
  separate from player/session identity. Catalogs are capped at 4,096 actors;
  travel/wander packages are capped at 32 waypoints.
- A separate immutable canonical actor world validates ordered actor IDs, unique
  entity IDs, and disjoint actor/player entities. Initial actors come only from
  validated catalog content.
- The server-only scheduler runs idle, ordered travel, and looping wander through
  manifest walk speed plus the existing checked collision kernel. Cells with no
  active player freeze exactly; failed batches do not partially replace actors.
- No client proposal, authority lease, protocol record, app composition,
  renderer behavior, combat, deletion, scripting, or persistence was added.

## Phase 13 content-backed actor vertical slice result

- The dedicated server requires the bounded manifest-scoped artifact documented
  in [ACTOR_CONTENT_V1.md](ACTOR_CONTENT_V1.md). It collision-validates every
  spawn and waypoint before listen and reserves actor entity IDs away from
  durable player allocation.
- Actor simulation runs inside every due fixed scheduler tick, including bounded
  catch-up. Player and actor output admission is atomic before either canonical
  world commits; actor motion remains wholly server-owned.
- Optional `actorReplication` capability negotiation adds separate reliable
  actor membership and latest-wins actor views. Canonical revision and server
  tick completion cover join, exact-cell transitions, resume, and authenticated
  resync without changing player protocol 1.2/1.3 behavior.
- The transport keeps distinct bounded player and actor latest slots. Shared
  desktop/PC-VR presentation maps prototype IDs locally, smooths canonical
  motion, selects animation, and owns renderer objects only.
- Combat, death, deletion, inventory, scripts, persistence, pathfinding, dynamic
  bodies, client simulation, and authority delegation remain excluded.

## Phase 13 lifecycle closure result

- A bounded representative Morrowind/Tribunal/Bloodmoon route ran two real
  desktop clients against one dedicated server with a locally mapped wandering
  actor. Thirty-three shared canonical samples matched exactly.
- Exact-cell leave/re-entry, four disconnect/resume cycles, authenticated
  resync, grace expiration, and durable-credential rejoin retained actor
  identity `1/9001/1`; revision checkpoints advanced `238 -> 244 -> 367 -> 374`.
- The proof fixed cross-lane player/actor membership ordering, same-revision
  resync publication ticks, and identical-baseline resync completion. Automation
  now forwards production actor presentation and retains bounded evidence.
- No protocol schema, authority, AI, collision, persistence, gameplay, or
  player-facing tuning changed. Phase 13 is complete; evidence is recorded in
  [the lifecycle capture](PHASE13_ACTOR_LIFECYCLE_CAPTURE.md).

## Phase 13 live presentation follow-up result

- Replicated actor animation now consumes horizontal root motion locally while
  canonical snapshots remain the only owner of world translation. This removes
  the walk-cycle drift and reset that appeared as repeated rubber-banding.
- Server config accepts an optional bounded `spawn_positions=x:y:z;...` list.
  New and durable-credential rejoin identities select stable points by player
  ID; existing canonical players retain their transform. Omitting the key keeps
  the prior single spawn for compatibility.
- Every configured point is collision-checked before listen. The representative
  two-client demo was relaunched with neighboring Guild of Mages floor points
  and a nearby two-waypoint actor route.
- Focused actor contract, authenticated-join, and server-app tests pass. The
  RelWithDebInfo dedicated server and full OpenMW targets build and link.

## Phase 14 discovery result

- Traced OpenMW door activation/rotation (`MWClass::Door`, `World::activateDoor`,
  `World::processDoors`), locks/traps (`MWWorld::CellRef`, `MWMechanics::Security`),
  and raycast activation (`ActionManager::activate`, `Player::activate`).
- Archived TES3MP 0.8.1 used client-authored packets (`PacketDoorState`,
  `PacketObjectLock`, `PacketObjectTrap`, `PacketObjectActivate`) without server
  authority, reach validation, key verification, or concurrency control.
- The recommended seam uses a bounded manifest-scoped object catalog, separate
  canonical object state, discrete door states (Closed/Open), server-owned
  reliable interaction commands with same-cell and reach checks, client-local
  visual animation interpolation, and inactive-cell simulation freeze.
- Three owner options and twelve named proof scenarios are recorded in
  [the interactive-object discovery](PHASE14_OBJECT_DISCOVERY.md). No runtime,
  protocol, gameplay, authority, or rendering behavior changed.
- Inventory, containers, combat, scripting, persistence, physics, and client
  authority remain strictly excluded from the Phase 14 package.

## Phase 14 canonical core result

- The owner approved Package A in [ADR-0061](adr/ADR-0061-phase14-server-authoritative-interactive-objects.md)
  and [GDR-0021](gdr/GDR-0021-phase14-interactive-objects-locks-traps-doors.md).
- Added `InteractiveObjectId`, `KeyPrototypeId`, `TrapPrototypeId`, and `ObjectRevision`
  strong value types.
- Manifest-bound `InteractiveObjectCatalog` validates ordered, unique entries up
  to 16,384 objects per world with exact-cell-consistent static transforms,
  teleport destinations, lock declarations, and trap declarations.
- Separate immutable `CanonicalInteractiveObjectWorld` manages runtime object state
  with discrete `DoorState`, `LockState`, and `TrapState`, initialized deterministically
  from catalog declarations.
- Server-authoritative `applyObjectInteraction` reducer processes reliable client
  interaction commands with mandatory validation: player existence, catalog/state and
  same-cell matching, overflow-safe Euclidean reach enforcement (<= 384 units by default)
  from the server player root and the reported interaction origin, a root-relative origin
  envelope, monotonic ticks, optimistic concurrency checking via expected revision,
  server-verified key possession for locks, and trap sprung triggers. Revision exhaustion
  and internal reducer failures have explicit fail-closed outcomes.
- Inventory, containers, combat, scripting, persistence, physics, and client authority
  remain strictly excluded.

## Phase 14 interactive-object replication and server composition result

- Pinned FlatBuffers schemas:
  `components/tes3mp/protocol/schema/reliable_interactive_object_interest_baseline.fbs` (identifier `T3IO`)
  and `components/tes3mp/protocol/schema/client_interact_object_command.fbs` (identifier `T3IC`) with strict LF line endings.
- Implemented size-prefixed verified FlatBuffers wire codecs in `interactive_object_replication.hpp` and `.cpp`.
- Extended handshake capabilities with `InteractiveObjectReplicationCapabilityValue = 3` and capability negotiation via `interactiveObjectReplicationCapability()`.
- Implemented exact-cell object interest baseline projection in `interactive_object_interest_projection.hpp` and `.cpp` (`projectInteractiveObjectInterestBaseline`, `projectCellInteractiveObjectBaseline`, `admitInteractiveObjectInterestBaseline`).
- Wired `TransportJoinResponseQueue` to deliver initial cell interactive-object baselines on join when capability is negotiated.
- Wired `ConnectionSessionCoordinator` to receive `ClientInteractObjectCommand` (kind `0x0106`), validate session and generation, resolve the canonical entity binding, and submit the complete command header and interaction payload to `ServerCommandIntakeCoordinator`.
- Composed `InteractiveObjectCatalog` and `CanonicalInteractiveObjectWorld` into `ServerApplicationWiring`.
- Added optional bounded [Interactive object content V1](INTERACTIVE_OBJECT_CONTENT_V1.md)
  loading in the production process root. The replication capability is advertised
  only after the configured catalog and initial world load successfully.
- The canonical command reducer executes interactions in the same ingress order and session-global sequence/idempotency history as movement and cell-transition commands. It stages player state, object state, and typed `ObjectInteractionOutcome` records together.
- Prepared reduction takes bounded base/candidate snapshots once per affected
  batch and mutates individual records in place; rollback restores only the failed
  command's prior record instead of rebuilding the entire world for every command.
- Teleport doors atomically replace the activating player's canonical transform and stop velocity; armed traps disarm canonically and publish their typed trap outcome to persistence/replay/script/metrics sinks.
- Interactive-object commands are capability-gated and use the existing 128-command per-session-generation intake bound; player/object mutations commit only after affected reliable baselines are admitted atomically.
- Network `UnlockWithKey` commands are rejected until Phase 15 integrates
  canonical inventory ownership. The core reducer accepts only an explicit
  independently verified key set and does not treat an empty set as verification.
- Delivery on resume, resync, join, and cell transition delivers complete exact-cell baselines without requiring continuous ticking for unloaded or empty cells.
- Verified in `apps/tes3mp-server/server_app_tests.cpp`: baseline delivery on join, client interaction dispatch with reach enforcement, door open transition with revision increment, Scenario 11 (unloaded cell preserves canonical object state without ticking), and Scenario 12 (late join and resync receive complete modified cell baseline).
- Pre-commit regression coverage proves mixed cell-transition/interaction ordering, finalized command history, rejected-interaction outcomes, trap outcome delivery to canonical sinks, teleport player replacement, and rollback when reliable output admission fails.

## Phase 14 client reception, activation, and round-trip result

- Extended `ClientSession`, `HeadlessClientSession`, and `ClientSessionRuntime` to ingest `ReliableInteractiveObjectInterestBaseline`, enforce baseline revision monotonicity, and gate session resync on object baseline completion when capability is negotiated.
- Implemented `DesktopPresentation::applyInteractiveObjects` to map and synchronize door state, apply lock/trap data, snap door rotation on cell entry, smoothly interpolate visual door swings, and play 3D audio.
- Added non-intrusive activation interception hook on `MWWorld::Player` (`mActivationInterceptor`) to catch door raycast activation before stock single-player activation or Lua callbacks run, cleanly intercepting local door rotation and teleportation.
- Registered patch `P14-001` in `docs/vnext/OPENMW_PATCH_REGISTRY.json` for `apps/openmw/mwworld/player.cpp` and `player.hpp`.
- Implemented `DesktopSemanticInput::handleActivation` and `queueObjectActivation` translating door activation into `ObjectInteractionCapture` with the current observed revision from `DesktopPresentation`.
- Added explicit input session cleanup so reconnect, terminal failure, and coordinator teardown remove the activation interceptor and discard pending interactions before OpenMW world teardown.
- Limited fallback activation interception to objects already observed in an authoritative baseline, and rejected ambiguous reverse object mappings.
- In `Coordinator::frame`, sampled object interaction when negotiated and dispatched via `queueInteractObject`.
- Enabled `interactiveObjectReplicationCapability()` across production client hello arrays in `adapter.cpp` and `client_connection.cpp`.
- Verified the client round-trip boundary in `openmw_tes3mp_adapter_tests`: activation proposal -> `ClientInteractObjectCommand` wire transmission and FlatBuffers decoding -> simulated server baseline response with object revision advance -> presentation observation. Verified unnegotiated rejection and input cleanup on reconnect.

## Phase 15 discovery result

- Traced OpenMW player inventory, container store, equipment slots, container
  activation, item stacking, and looting mechanics (`MWWorld::InventoryStore`,
  `MWWorld::ContainerStore`, `MWClass::Container`, `MWGui::ContainerItemModel`).
- Archived TES3MP 0.8.1 relied on uncoupled client-authored packets
  (`PacketPlayerInventory`, `PacketContainer`, `PacketPlayerEquipment`) forwarded
  directly to Lua scripts, lacking transactional atomicity and causing item
  duplication and race conditions.
- Traced authoritative key ownership for Phase 14 object unlocking: player canonical
  inventory keys will populate `ObjectInteractionValidationContext::verifiedPlayerKeys`,
  resolving `UnlockWithKey` commands authoritatively.
- Defined five clean lanes: server-owned canonical inventory/container world, reliable
  client transaction commands, server-evaluated atomic transfer reducer, interest
  projection (private backpacks vs. public equipment), and client presentation.
- Three owner options and thirteen named proof scenarios are recorded in
  [the inventory discovery](PHASE15_INVENTORY_DISCOVERY.md). No runtime, protocol,
  gameplay, authority, or rendering behavior changed.
- Scripting, durable disk persistence, and client authority remain strictly excluded.

## Phase 15 replication and server composition result

- Five explicit-ID FlatBuffers schemas and owned codecs cover private player
  inventory, exact-cell container and ground-item baselines, public equipment,
  and authenticated transaction commands. Chunk and collection limits are
  checked before allocation and against the transport payload caps.
- Inventory transactions share the established session-global ordering,
  duplicate handling, acknowledgement, and canonical revision path. Player,
  interactive-object, and inventory candidates commit only after all affected
  output is admitted; failure leaves every canonical domain unchanged.
- Join, resume, resync, cell changes, and transaction outcomes project complete
  target-specific inventory state. Only the owner receives backpack stacks;
  same-cell peers receive visible prototype IDs for the 19 equipment slots.
- The canonical key bridge now supplies verified possession to Phase 14 door
  commands. The end-to-end app contract takes a key from a container and then
  unlocks a matching door without trusting a client key claim.
- Optional [inventory content V1](INVENTORY_CONTENT_V1.md) startup composition is
  manifest-bound, size/count constrained, collision-cell checked, and rejects
  cells whose complete reliable view cannot fit the bounded queue. The server
  advertises inventory replication only when this content loads successfully.
- OpenMW ingestion, GUI reconciliation, transaction capture, and equipment mesh
  presentation remain the next pass.

## Phase 10 result

- Fresh password joins issue a random 32-byte player credential after ordinary
  authentication. The client stores the secret; the bounded server registry
  atomically stores its SHA-256 digest with stable player/entity identity.
- A credential-authenticated join restores those IDs after grace expiration or
  server restart. Expiration still removes the visible avatar, and reattachment
  recreates it at the configured spawn. Session/resume identity stays separate.
- Protocol 1.1 negotiates an exact nonzero 32-byte content-manifest identity and
  rejects mismatches before authentication. Resume tokens bind to that negotiated
  identity instead of a fixture string.
- Manifest-scoped opaque cell/worldspace/appearance IDs replace magic production
  constants. OpenMW record names remain local adapter mappings, and canonical
  checksum encoding advances to version 2 for explicit appearance identity.
- The pass retains one configured spawn and appearance. It does not add character
  creation, inventory, account recovery, scripting, moderation, or general
  character/world persistence. See [ADR-0057](adr/ADR-0057-phase10-durable-player-and-content-identity.md).
- The milestone audit keeps the V1 registry format fail-closed, moves all
  fallible in-memory allocation before atomic replacement, removes failed
  temporary files, scrubs client copy buffers, and applies POSIX owner-only mode
  before credential bytes are written. Manifest-aware handshake corpus hashes are
  pinned in the decoder safety registry, and Phase 10 identity paths/dependency
  input are recorded in baseline provenance.

## Phase 9 result

Desktop OpenMW and PC OpenMW-VR now use one protocol, client session, and server
runtime:

- the VR executable is no longer compiled around the multiplayer startup path;
- a distinct VR provider leaf reuses the desktop semantic movement, cell,
  correction, smoothing, and renderer-only remote actor implementation;
- the VR leaf samples OpenXR head and optional hand transforms;
- `vr_pose` is optional and capability-gated;
- presentation samples use a dedicated unreliable latest-wins lane, isolated
  from canonical world snapshots and reliable operations;
- sampling runs at 20 Hz, independently of engine frame and server tick rates;
- the server validates source session, generation, root entity, authority epoch,
  sequence recency, and shared cell before relaying;
- disconnect/resume clears pose sequence and retained presentation state;
- desktop presentation safely ignores articulated pose while retaining
  canonical root rendering; clients without `vr_pose` keep the old path.

Pose remains ephemeral presentation data. It cannot move the authoritative root,
enter canonical state, persist, replay, or prove gameplay reach. Remote skeleton
articulation and production pose hardening remain later movement/presentation
work.

## Implementation points

- Shared pose runtime: `617b8bc3a1`
- VR provider/composition: `af52e60659`
- Phase 12 VR integration merge: `223d5a74e9`
- Phase 13 actor vertical slice: `ae6c6717ff`
- Phase 13 VR integration merge: `2762445c8b`
- Shared branch: `vnext`
- VR branch/worktree: `vnext-vr` / `../TES3MP-vr`
- VR source baseline: `56a8e01390507375c9c2f2593e1c09e0df88c505`
- OpenXR source: `1ca7bec6b531185530c9b4f1e7a50e1fd55e7641`

## Last verified

- Standalone TES3MP MSVC C++20 build completed cleanly in `build/slice51-msvc`.
- `tes3mp_protocol_tests_run` and `tes3mp_server_app_tests_run` build and pass,
  including inventory wire bounds, projection privacy, lifecycle delivery,
  atomic rollback, and the inventory-to-door-key integration path.
- `tes3mp_transport_queue_tests.exe` and `tes3mp_inventory_replication_tests.exe`
  pass cleanly; the production `tes3mp_server` executable links successfully.
- The pinned FlatBuffers selection proof regenerates and compares all 22
  production schemas and generated headers successfully.
- Python inventory contracts pass (11 tests), and all 174 repository Python
  tests pass (`python -m unittest discover -s scripts/tests`).
- `verify_openmw_patch_registry.py` passes cleanly with `P14-001` covering `player.cpp` and `player.hpp`.
- `verify_vnext_legacy_exclusion.py` passes (4,128 tracked paths, 62 CMake files, 1,254 compile commands, and 1,971 Ninja build edges checked).
- Indexed candidate-tree `verify_vnext_baseline.py --index` passes with 469
  intentional differences and 95 verified dependency declarations.
- Target boundary verification and forbidden include checks enforced in CMake.

## Next pass

Phase 15 OpenMW client integration: ingest complete inventory/container/ground
views, reconcile GUI state, dispatch transactions, and present confirmed local
and remote equipment without expanding client authority.

## Working-tree expectation

`vnext` contains the completed Phase 15 inventory replication and
server-composition pass and is expected to be clean after closeout. The separate
`vnext-vr` worktree remains at the Phase 13 integration merge `2762445c8b`.
