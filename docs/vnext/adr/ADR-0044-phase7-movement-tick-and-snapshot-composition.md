# ADR-0044: Phase 7 movement tick and snapshot composition

Status: **Accepted**

Date opened: 2026-09-02

Date approved: 2026-09-02

Decision owner: project owner

Needed by: Phase 7 Slice 7.5

## Decision summary

The project owner approved Option A for both decisions on 2026-09-02:

1. one writer transaction drains eligible semantic commands, reduces velocity
   intents, integrates every non-zero canonical velocity with checked integer
   arithmetic, derives outputs, admits them, and only then commits; any
   integration, projection, admission, or version failure commits nothing; and
2. each active target receives a complete same-cell latest-wins view identified
   by the existing `ServerTick`. Changed-entity deltas and global broadcast views
   are rejected for this slice.

This implements the provisional movement and correction behavior already
approved by GDR-0001. It does not add collision, acceleration, speed limits,
prediction, interpolation, general interest management, or resynchronization.

## Alternatives and consequences

A separate command commit followed by movement integration could expose accepted
velocity without matching movement. Per-player commits could expose partial tick
state. Changed-entity snapshots would introduce delta/resync rules owned by Phase
11, while global views would violate approved cell visibility.

The selected transaction may reject a whole tick because one player overflows or
one target cannot accept its view. This gives a simple deterministic fail-closed
boundary. Latest-wins admission remains bounded and coalesces older queued views.

## Acceptance tests and demo

1. `fixed_tick_integrates_velocity_with_checked_atomic_revision`;
2. `simultaneous_players_move_only_their_own_roots`;
3. `overflowing_integration_fails_closed_without_partial_commit`;
4. every active target receives exactly the complete same-cell view;
5. missing target or encoding/admission failure commits neither state nor views;
6. newer tick views converge and stale/same-tick contradictory views are rejected.

The owner demo must show two clients moving simultaneously, observing converged
same-cell roots and velocity, rejecting a stale view, and one atomic overflow
failure.

## Review triggers

Reopen before adding partial/delta views, prediction, interpolation, collision,
speed or acceleration rules, per-player failure isolation, cross-cell visibility,
or a sequence separate from `ServerTick`.

## Owner approval

Approved by the project owner on 2026-09-02: Option A for the atomic writer tick
and Option A for targeted complete same-cell latest-wins views.
