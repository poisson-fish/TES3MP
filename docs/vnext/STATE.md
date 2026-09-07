# TES3MP vNext state

Updated: 2026-09-06

## Current status

- Phases 0–10: **Complete**
- Phase 11: **Complete**
- Phase 12 discovery: **Complete**
- Phase 12 evidence: **Complete**
- Phase 12 package decision/safety: **Complete**
- Active work: **Phase 12 versioned locomotion input and bounded local replay**
- Last pass: **Phase 12 manifest movement profiles and collision kernel seam complete**
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
  reports two pose gaps; stall reaches 100 ms extrapolation, 8,192-quanta remote
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
- Shared branch: `vnext`
- VR branch/worktree: `vnext-vr` / `../TES3MP-vr`
- VR source baseline: `56a8e01390507375c9c2f2593e1c09e0df88c505`
- OpenXR source: `1ca7bec6b531185530c9b4f1e7a50e1fd55e7641`

## Last verified

- MSVC RelWithDebInfo full engine-independent protocol/server contracts and the
  dedicated-server app integration pass after movement-kernel integration.
- The named server-collision authority regression, invalid manifest-profile
  cases, and existing exact-cell interest/resync contracts pass.
- MSVC Release networking server, headless client, and full OpenMW `openmw.exe`
  build and link; the Phase 7 lifecycle integration passes 32 reconnects and
  drains both queues to zero.
- All 145 repository Python tests, indexed baseline provenance, legacy
  exclusion, and diff hygiene pass.
- MSVC RelWithDebInfo full engine-independent protocol/server contracts and the
  dedicated-server app integration pass after the movement safety gate.
- Both named input safety regressions pass; all 145 repository Python tests and
  diff hygiene pass.
- Phase 12 movement evidence, adapter, observability, command-intake, and
  server-app contracts pass. The deterministic capture emits all eight expected
  desktop/PC-VR platform/profile records.
- MSVC Release full OpenMW `openmw.exe` builds and links with the production
  GameNetworkingSockets path after the evidence wiring.
- The networking-enabled `tes3mp_server.exe`, all 145 repository Python tests,
  and diff hygiene pass.
- Phase 12 discovery claims were checked against the desktop/VR adapter, protocol,
  server intake/reducer/application, replicated actor, and focused movement tests.
- Release adapter, reducer, server-app, and pose contract executables pass; all
  145 repository Python tests and diff hygiene pass.
- MSVC Release builds and affected protocol exchange/frame/handshake, session,
  headless, reducer, server-app, and OpenMW adapter tests pass.
- Networking-enabled server/headless binaries and full OpenMW desktop
  `openmw.exe` build and link with the production GameNetworkingSockets path.
- The pinned FlatBuffers dependency, exact production schema generation, proof
  build, contract test, and corpus check pass. All 36 affected Python tests pass.
- Indexed baseline provenance passes with 376 intentional differences and 77
  dependency declaration inputs.
- The Phase 7 lifecycle integration passes simultaneous movement, converged and
  stale-rejected views, 32 reconnects, stable identity/progress, resume expiry,
  fresh identity, and zero final queue depth.
- Hardware/content-backed desktop and desktop-to-VR visual proof was not run.

## Next pass

Read the rolling **Now** section in
[IMPLEMENTATION_PLAN.md](IMPLEMENTATION_PLAN.md). Add versioned locomotion input,
bounded shared client input history, and replay-only local reconciliation without
extending remote presentation, animation, pose, world-object, or persistence
scope.

## Working-tree expectation

Before starting the next pass, `vnext` should contain the completed Phase 12
movement-profile/kernel commit and be clean. The separate
`vnext-vr` worktree remains at the completed Phase 9 baseline until a later shared
merge is deliberately made.
