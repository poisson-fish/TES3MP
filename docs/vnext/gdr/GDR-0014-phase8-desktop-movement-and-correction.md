# GDR-0014: Phase 8 desktop movement and correction

Status: **Accepted**

Date opened/approved: 2026-09-03

Decision owner: project owner

Needed by: Phase 8 Slice 8.5

Companion architecture record:
[`ADR-0051`](../adr/ADR-0051-phase8-provisional-spatial-intent-concurrency.md)

## Decision

The owner approved Option A for input mapping and correction.

Desktop keyboard/controller planar movement actions produce a normalized local
X/Y vector, rotate it by the local player's yaw into world axes, and scale it to
exactly 4,096 canonical position quanta per 30 Hz server tick (4 OpenMW units
per tick, 120 units per second). Float conversion rejects non-finite values and
rounds to nearest with ties to even. Zero input requests zero velocity. Jump,
run, sneak, automove, vertical movement, orientation commands, collision, and
content/stat-derived speed are outside this slice.

OpenMW may continue responsive local movement between snapshots. Every newer
same-cell authoritative snapshot corrects the local position exactly with a
zero hard-snap threshold while preserving local facing and animation state.
Cell correction remains governed by GDR-0013. Authoritative application cannot
be observed as a movement command because input comes from action state, never
position differences.

## Alternatives rejected

OpenMW character/stat speed would make client content an unchecked gameplay
input. Position-delta inference violates the semantic-input boundary. Tuned
deadbands/blending would pull Phase 12 prediction policy forward. Server-only
presentation would make the first desktop slice unnecessarily unresponsive.

## Required evidence

- Cardinal, diagonal, yaw-rotated, zero, and ties-to-even mappings are exact.
- Non-finite input maps safely to zero and engine types remain adapter-local.
- Changed intent coalesces; zero/stop reaches canonical state after delay.
- Every newer same-cell snapshot applies the exact authoritative position,
  preserves facing, and emits no feedback command.
- Two desktop clients move and converge in the owner demo.

## Boundary and review trigger

This is fixture behavior, not production locomotion. Phase 12 GDR-0004 still
owns speed, acceleration, collision, jumping, orientation, input streams,
prediction thresholds, animation, and VR root/pose semantics.

## Owner approval

Approved by the project owner on 2026-09-03 as Option A for desktop input
mapping and Option A for exact local correction.
