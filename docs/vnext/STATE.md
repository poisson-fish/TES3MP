# TES3MP vNext state

Updated: 2026-09-05

## Current status

- Phases 0–10: **Complete**
- Active work: **none; Phase 10 identity foundation is implemented**
- Next pass: **run the Phase 10 milestone gate and select the next vertical slice**
- Authoritative tracker: [rolling implementation plan](IMPLEMENTATION_PLAN.md)
- Historical evidence: [implementation notes](IMPLEMENTATION_NOTES.md)

The repository now uses a rolling **Now / Next / Later** workflow. Only the
active pass is specified in implementation detail. The next few passes are
tentative, and later work stays at outcome level until evidence makes it timely.
ADRs and GDRs are required only for consequential, hard-to-reverse decisions.

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
- Hardware/content-backed desktop and desktop-to-VR visual proof was not run.

## Next pass

Read the rolling **Now** section in
[IMPLEMENTATION_PLAN.md](IMPLEMENTATION_PLAN.md). Run the Phase 10 milestone gate,
then select and specify either the canonical cells/interest or production movement
vertical pass. Do not broaden identity persistence before a new decision.

## Working-tree expectation

Before starting the next pass, `vnext` should contain the Phase 10 discovery and
identity-foundation commits and be clean. The separate `vnext-vr` worktree remains
at the completed Phase 9 baseline until a later shared merge is deliberately made.
