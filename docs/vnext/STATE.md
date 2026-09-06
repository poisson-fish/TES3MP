# TES3MP vNext state

Updated: 2026-09-05

## Current status

- Phases 0–10: **Complete**
- Phase 11 discovery: **Complete**
- Active work: **production cell/interest/resync package awaiting owner approval**
- Last pass: **Phase 11 repository trace and bounded implementation design complete**
- Authoritative tracker: [rolling implementation plan](IMPLEMENTATION_PLAN.md)
- Historical evidence: [implementation notes](IMPLEMENTATION_NOTES.md)

The repository now uses a rolling **Now / Next / Later** workflow. Only the
active pass is specified in implementation detail. The next few passes are
tentative, and later work stays at outcome level until evidence makes it timely.
ADRs and GDRs are required only for consequential, hard-to-reverse decisions.

## Phase 11 discovery result

- OpenMW cell changes flow through one pending plus one deferred reliable fixture
  transition. The server validates the current session/generation and one
  manifest interior or exterior `(0,0)`, prepares canonical state, derives every
  target's same-cell deltas/view, admits output, then commits.
- The client independently applies reliable membership changes and complete
  latest-wins spatial views. Presentation uses their intersection. Join and resume
  fake a baseline with ordinary `Enter` changes; quiet input has no explicit
  completion meaning.
- The existing core resync helper validates session/generation and returns the
  latest unfiltered publication only. It has no protocol, routing, interest
  projection, request bound, or client replacement path.
- Recommended cell catalog: at most 256 unique cell spaces and 4,096 exact allowed
  cells, with complete local OpenMW mappings. IDs and cells are sorted/unique;
  missing records, kind mismatches, unknown grids, and collisions after OpenMW's
  case-insensitive `ESM::RefId` comparison fail startup or command admission.
- Recommended interest remains exact canonical-cell equality and server-owned.
  Adjacent-grid visibility and client-selected interest stay out of scope.
- Recommended resync adds a reliable membership-only baseline plus a latest
  snapshot. Initial/resume/resync completion requires both; a metadata-only
  authenticated request permits one pending response per session generation and
  cannot upload state.
- The A/A/A package advances protocol 1.1 to 1.2 and replaces scalar cell config,
  but does not migrate the player registry, alter canonical checksum encoding, or
  add movement/teleport/world-object behavior. Alternatives and named failures are
  in the rolling plan.

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

- Phase 11 discovery inspected the OpenMW provider/adapter, reusable client
  runtime/state, protocol roots, server dispatch/intake/reducer, projection,
  join/resume/disconnect/expiration composition, manifest config, and core resync
  helper.
- Current bounds were confirmed at 256 canonical players/sessions/view entries/
  observation changes, 128 pending commands per session generation, 32 inbound
  messages per client drain, and 16/64 KiB reliable/latest payloads.
- No build or runtime test was run because production code did not change. The
  Phase 10 milestone evidence below remains the last implementation verification.

- Standalone protocol/determinism/fault/observability, authenticated-join,
  server-app, and OpenMW adapter contract targets pass under MSVC RelWithDebInfo.
- Player identity tests cover issue, digest lookup, expiration, same-ID
  reattachment, process restart reload, persistence failure rollback, and ID reuse
  only after an uncommitted failure.
- Networking-enabled server/headless binaries and their affected contract targets
  build and pass. The Phase 7 lifecycle integration flow passes 32 reconnects,
  resume/expiry/fresh identity, converged views, stale rejection, and zero final
  queue depth.
- Full OpenMW desktop `openmw.exe` builds and links in RelWithDebInfo with the
  production GameNetworkingSockets transport.
- GCC 16.2.1 on Linux builds and passes authenticated-join, server-app, and
  OpenMW adapter contracts, including owner-only credential permissions and
  failed-replacement cleanup.
- All 145 repository-owned Python tests pass, including decoder golden-corpus
  registry verification.
- Indexed baseline provenance passes with 367 intentional differences and 73
  dependency inputs; the OpenMW patch registry passes.
- Hardware/content-backed desktop and desktop-to-VR visual proof was not run.

## Next pass

Read the rolling **Now** section in
[IMPLEMENTATION_PLAN.md](IMPLEMENTATION_PLAN.md). Obtain the three recorded owner
choices, record the approved GDR/ADR package, then implement only that bounded
cell/interest/resync pass. Do not begin production movement, teleport policy,
world-object streaming, or broader persistence.

## Working-tree expectation

Before starting the next pass, `vnext` should contain the Phase 11 discovery
commit and be clean. The separate
`vnext-vr` worktree remains at the completed Phase 9 baseline until a later shared
merge is deliberately made.
