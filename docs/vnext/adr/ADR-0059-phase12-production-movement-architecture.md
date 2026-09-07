# ADR-0059: Phase 12 production movement architecture

Status: **Accepted**

Date opened: 2026-09-06

Date approved: 2026-09-06

Decision owner: project owner

Companion gameplay record:
[`GDR-0019`](../gdr/GDR-0019-phase12-production-movement-package.md)

## Decision

The owner approved the Phase 12 architecture package:

1. A server-owned fixed-tick movement kernel is the only producer of canonical
   player-root movement. Collision enters through a content-backed server query
   boundary; clients cannot propose collision results.
2. Local presentation retains bounded semantic input history and reconciles by
   bounded replay. Replay and correction cannot become new input.
3. Remote presentation uses bounded adaptive playback inside explicit floors and
   ceilings selected from evidence.
4. Canonical state carries only the small locomotion state needed for shared
   simulation and animation. Reliable one-shots stay separate, and optional VR
   pose remains root-relative, latest-wins, ephemeral presentation data.

OpenMW, Bullet, OpenXR, socket, and renderer types do not enter the movement
kernel, collision query, protocol model, or canonical state.

## First safety boundary

Before the production schema and content profiles land, the existing version 1.2
velocity intent is admitted only through a checked compatibility envelope. The
canonical reducer final-rejects an out-of-range vector before player spatial
mutation. This closes the signed-64-bit integration failure path for online
client commands without treating the envelope as a production speed profile.

## Consequences

- ADR-0006 server authority, Phase 11 exact-cell interest, and resync semantics
  remain unchanged.
- Production input ticks, locomotion modes, facing, discontinuities, and
  animation state require an additive versioned schema.
- Collision data and movement profiles are manifest-scoped server inputs.
- Adaptive playback, correction, and pose fallback budgets require measured
  desktop and PC-VR evidence before ratification.
- This decision creates no persistence or world-object behavior.

## Approval

The project owner approved Options A/A/A/A/A in the 2026-09-06 working session.
