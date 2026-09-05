# TES3MP vNext state

Updated: 2026-09-05

## Current status

- Phases 0–9: **Complete**
- Active work: **none**
- Next pass: **Phase 10 player lifecycle and content identity discovery**
- Authoritative tracker: [rolling implementation plan](IMPLEMENTATION_PLAN.md)
- Historical evidence: [implementation notes](IMPLEMENTATION_NOTES.md)

The repository now uses a rolling **Now / Next / Later** workflow. Only the
active pass is specified in implementation detail. The next few passes are
tentative, and later work stays at outcome level until evidence makes it timely.
ADRs and GDRs are required only for consequential, hard-to-reverse decisions.

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

- Shared MSVC 19.51 standalone targets build.
- Shared transport, protocol-pose, headless-client, adapter, and server
  behavioral executables pass.
- PC VR RelWithDebInfo `openmw_vr.exe` builds and links.
- The same focused runtime/server/transport tests pass from the VR tree.
- Phase 9 composition/protocol tests and the OpenMW-VR provenance/patch verifier
  pass.
- Built VR executable SHA-256:
  `2a17b1d43abcfca433cbe78bf87d1c6c06a03a178bbf68dfaba8819a53b3baf2`.
- Built desktop executable SHA-256:
  `32ce0139a805749c1521e4810eb1db4c816474ee5bca8a7e795e52c61f93c978`.

Hardware/content-backed desktop-to-VR visual proof was not run in this pass.
That does not weaken the compile, protocol, authority, queue, or composition
closure above, but it should be included when a licensed content fixture and VR
runtime are available.

## Next pass

Read the Phase 10 **Now** section in
[IMPLEMENTATION_PLAN.md](IMPLEMENTATION_PLAN.md). Begin with a read-only trace of
join identity, player/entity allocation, fixture avatar mapping, resume, and
expiration. Do not revive the old preplanned slice ledger.

## Working-tree expectation

Before starting the next pass:

- `vnext` should contain the rolling-plan/status documentation commit;
- `vnext-vr` should contain the shared Phase 9 merge, VR provider commit, and
  the rolling-plan/status merge;
- both worktrees should be clean.
