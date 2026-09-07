# ADR-0061: Phase 14 server-authoritative interactive objects

Status: **Accepted**

Date opened: 2026-09-07

Date approved: 2026-09-07

Decision owner: project owner

Companion gameplay record:
[`GDR-0021`](../gdr/GDR-0021-phase14-interactive-objects-locks-traps-doors.md)

## Decision

The owner approved Package A from the Phase 14 discovery:

1. Interactive objects (doors, locks, traps) have stable manifest-scoped identity
   (`InteractiveObjectId`), exact-cell placement, and canonical state owned
   entirely by the server.
2. The server canonical world tracks discrete states: door is `Closed` or `Open`,
   lock is `Locked` (with lock level and key ID) or `Unlocked`, and trap is
   `Armed` (with spell ID) or `Disarmed`.
3. The server is the sole commit authority for state transitions. Interaction
   requests are reliable commands validated against player session, exact-cell
   equality, bounded reach limits, and lock/trap preconditions before mutation.
   Client-presented key IDs are hints only; successful unlocking also requires
   possession supplied from server-verified state.
4. Clients interpolate visual 90° door rotation and sound locally upon observing
   discrete state transitions. No intermediate rotation angles stream across the
   network or enter server simulation ticks.
5. Teleport doors initiate server-owned player cell transitions rather than
   moving door geometry.
6. Cells with 0 player observers freeze simulation and retain canonical object
   modifications in memory without CPU overhead. Joining or entering a cell
   delivers a reliable complete interactive-object baseline.
7. Inventory, containers, combat, scripting, general persistence, physics, and
   client simulation authority remain strictly excluded from Phase 14.

## First implementation boundary

The first pass implements the engine-independent interactive object catalog,
immutable canonical object world, reach/cell validation rules, and atomic
interaction reducer in `components/tes3mp`. The reducer accepts independently
verified key possession without implementing inventory itself. Server-app content ingestion,
wire protocol replication, resync baselines, and OpenMW presentation follow.

## Consequences

- Client-authored packets (`PacketDoorState`, `PacketObjectLock`, etc.) from
  legacy TES3MP 0.8.1 remain excluded.
- The server does not tick moving door angles at 30 Hz, saving network bandwidth
  and CPU while eliminating rotation jitter.
- VR pose samples cannot author interaction reach beyond a server-enforced player
  root envelope.
- Teleportation, lock status, and trap triggering are immune to client-side
  manipulation or race conditions.

## Approval

The project owner approved Package A in the 2026-09-07 working session.
