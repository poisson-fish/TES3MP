# GDR-0019: Phase 12 production movement package

Status: **Accepted**

Date opened: 2026-09-06

Date approved: 2026-09-06

Decision owner: project owner

Companion architecture record:
[`ADR-0059`](../adr/ADR-0059-phase12-production-movement-architecture.md)

## Decision

The owner approved the Phase 12 gameplay package:

1. Server movement profiles are selected by the negotiated content manifest and
   expose explicit walk, run, sneak, and jump states.
2. Server content collision is authoritative for the canonical root.
3. Local correction uses bounded input replay, measured soft convergence, and
   explicit hard discontinuities.
4. Remote motion uses bounded adaptive playback rather than ratifying the fixed
   fixture delay.
5. A small canonical locomotion state drives shared animation; reliable
   one-shots remain events and VR pose cannot move the root or prove reach.

Exact speeds, acceleration, correction, playback, snap, and pose fallback values
are not selected by this approval. They remain evidence-backed profile inputs.

## Version 1.2 safety amendment

Until the production schema replaces raw velocity intent, the reducer accepts
the current fixture vocabulary only inside this numeric envelope:

- each signed component is within 4,096 position quanta per tick; and
- three-dimensional magnitude is at most 4,097 quanta per tick.

The one-quantum radial allowance contains the desktop producer's ties-to-even
component rounding at arbitrary yaw. It is a compatibility and overflow-safety
envelope, not a new movement speed. An out-of-range exact-next command receives
the final `MotionOutOfRange` disposition, advances acknowledgement, and preserves
the player spatial value exactly.

This supersedes GDR-0012 Decision 2 for the integrated runtime. Other GDR-0012
fixture semantics remain until the production kernel replaces them.

## Acceptance evidence

1. `motion_magnitude_is_bounded_before_canonical_mutation`
2. `extreme_motion_input_cannot_fail_the_server_tick`

## Approval

The project owner approved Options A/A/A/A/A in the 2026-09-06 working session.
