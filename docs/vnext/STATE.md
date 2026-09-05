# TES3MP vNext state

Updated: 2026-09-05

## Current status

- Phases 0–9: **Complete**
- Phase 10 discovery: **Complete**
- Active work: **player/content identity foundation awaiting owner approval**
- Next pass: **implement the approved identity foundation**
- Authoritative tracker: [rolling implementation plan](IMPLEMENTATION_PLAN.md)
- Historical evidence: [implementation notes](IMPLEMENTATION_NOTES.md)

The repository now uses a rolling **Now / Next / Later** workflow. Only the
active pass is specified in implementation detail. The next few passes are
tentative, and later work stays at outcome level until evidence makes it timely.
ADRs and GDRs are required only for consequential, hard-to-reverse decisions.

## Phase 10 discovery result

- Authentication currently produces a new process-local routing principal for
  every successful password join; it is not a durable player subject.
- Join allocates independent monotonic process-local session, player, and entity
  IDs and installs one fixed fixture spawn. Its retained principal list is not
  pruned when lifecycle expiration removes the player.
- Disconnect/resume preserves the session/player/entity binding only within the
  in-memory grace window. Expiration deletes the player; restart restores no
  player, lifecycle, token, or allocation state.
- The resume content digest comes from a fixed server-only fixture string. It is
  not peer-negotiated or tied to the client's actual mapping.
- Canonical movement accepts only cell-space IDs `7` and `8`; the OpenMW adapter
  maps those IDs and every remote avatar through three local command-line values.
- The production boundary is join composition: resolve a stable authenticated
  player subject and exact manifest context before canonical allocation or
  reattachment. Protocol state should carry only context-scoped opaque content
  IDs; OpenMW record names stay in the adapter manifest.

Recommended next increment: a dedicated durable player credential layered after
ordinary authentication, plus exact manifest-digest negotiation and opaque
cell/appearance IDs. This changes credential security, durable identity,
protocol compatibility, and visible avatar selection, so implementation awaits
owner approval. Alternatives are recorded in the rolling plan.

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

- Phase 10 discovery traced production join allocation, connection/session
  composition, disconnect/resume/expiration, resume-token context, fixture cell
  validation, and desktop cell/avatar mapping.
- Repository search found no canonical player restore path or client/server
  content-manifest agreement path.
- No build or runtime test was run: this pass changed documentation only.

Phase 9's last implementation verification remains:

- Shared MSVC 19.51 standalone targets build.
- Shared transport, protocol-pose, headless-client, adapter, and server
  behavioral executables pass.
- PC VR RelWithDebInfo `openmw_vr.exe` builds and links with the production GNS
  transport enabled.
- The same focused runtime/server/transport tests, including encrypted GNS
  loopback, pass from that networking-enabled VR build.
- Phase 9 composition/protocol tests and the OpenMW-VR provenance/patch verifier
  pass.
- Built VR executable SHA-256:
  `82bfc2946c29dee76ed54d2b465601a59e0fbd5ef921706136865f3754d13bbd`.
- Built desktop executable SHA-256:
  `32ce0139a805749c1521e4810eb1db4c816474ee5bca8a7e795e52c61f93c978`.

Hardware/content-backed desktop-to-VR visual proof was not run in this pass.
That does not weaken the compile, protocol, authority, queue, or composition
closure above, but it should be included when a licensed content fixture and VR
runtime are available.

## Next pass

Read the Phase 10 **Now** section in
[IMPLEMENTATION_PLAN.md](IMPLEMENTATION_PLAN.md). Obtain the two recorded owner
choices, then implement the approved narrow identity foundation. Do not broaden
the pass into character creation, inventory, scripting, moderation, or general
persistence.

## Working-tree expectation

Before starting the next pass:

- `vnext` should contain the Phase 10 discovery documentation commit;
- `vnext-vr` should contain the shared Phase 9 merge, VR provider commit, and
  the rolling-plan/status merge;
- both worktrees should be clean.
