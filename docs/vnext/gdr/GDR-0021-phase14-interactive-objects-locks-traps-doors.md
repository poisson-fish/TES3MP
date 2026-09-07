# GDR-0021: Phase 14 interactive objects, locks, traps, and doors

Status: **Accepted**

Date opened: 2026-09-07

Date approved: 2026-09-07

Decision owner: project owner

Companion architecture record:
[`ADR-0061`](../adr/ADR-0061-phase14-server-authoritative-interactive-objects.md)

## Decision

The owner approved the bounded server-authoritative interactive-object package:

1. Interactive objects are declared in a content-manifest-bound catalog. Interest,
   disconnect, and cell inactivity never create, delete, or reset them.
2. The core vocabulary covers standard animated doors, teleport doors, locks, and
   traps.
3. Standard doors toggle discretely between `Closed` and `Open`. Local clients
   interpolate the aesthetic 90° swing animation over 1.0 second.
4. Teleport doors initiate canonical cell transitions to declared destination
   cells and coordinates when activated.
5. Locks prevent door activation unless unlocked or activated with a matching
   key whose possession is independently verified from server-owned state.
   Successful key activation unlocks the door and disarms any attached trap;
   a client-presented prototype ID alone never grants authority.
6. Traps trigger a canonical outcome event when an armed object is activated
   without disarming.
7. Reach validation enforces that an activating player must be in the exact same
   canonical cell and within a bounded distance (e.g. 384 units) of the target
   object root. VR hands/poses cannot bypass this envelope.
8. Unloaded cells freeze simulation and retain canonical modifications in memory.
   Entering a cell receives a reliable complete baseline of interactive object
   states.

## Bounds

- At most 512 interactive objects per cell, and at most 16,384 total catalog entries.
- Object IDs are unique nonzero strong identities scoped to the catalog.
- Lock levels and prototype IDs (keys and trap spells) use typed representations.
- All declared object coordinates and teleport destinations belong to the
  manifest's exact cell catalog.

## Deferred behavior

Inventory item consumption, lockpicking minigames, probe disarming chance rolls,
container stores, full spell effect resolution, mwscript/Lua activation triggers,
and disk persistence remain later domain work.

## Approval

The project owner approved Package A in the 2026-09-07 working session.
