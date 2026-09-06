# TES3MP vNext state

Updated: 2026-09-06

## Current status

- Phases 0–10: **Complete**
- Phase 11: **Complete**
- Active work: **Phase 12 production movement discovery ready**
- Last pass: **Phase 11 exact cell catalog, interest baseline, and resync complete**
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
[IMPLEMENTATION_PLAN.md](IMPLEMENTATION_PLAN.md). Perform the Phase 12 production
movement discovery pass only. Do not change movement, collision, correction,
animation, or player-facing lag behavior without the resulting evidence and any
required owner decisions.

## Working-tree expectation

Before starting the next pass, `vnext` should contain the Phase 11 implementation
commit and be clean. The separate
`vnext-vr` worktree remains at the completed Phase 9 baseline until a later shared
merge is deliberately made.
