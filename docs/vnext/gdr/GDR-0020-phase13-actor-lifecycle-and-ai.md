# GDR-0020: Phase 13 actor lifecycle and AI

Status: **Accepted**

Date opened: 2026-09-07

Date approved: 2026-09-07

Decision owner: project owner

Companion architecture record:
[`ADR-0060`](../adr/ADR-0060-phase13-server-owned-actor-foundation.md)

## Decision

The owner approved the bounded server-owned actor package:

1. A content-manifest-bound catalog creates stable canonical actors. Interest,
   player disconnect, and inactive cells never create, delete, or reset them.
2. The first AI vocabulary is idle, ordered waypoint travel, and deterministic
   looping waypoint wander. Movement uses the manifest walk speed and existing
   authoritative collision query.
3. Actor simulation freezes while no active session has a player in the actor's
   exact cell. It resumes from retained canonical state when that cell becomes
   active. Other cells do not tick in the background.
4. Travel stops at its final waypoint. Wander loops its bounded waypoint list.
   Axis-ordered movement is deterministic and presentation may smooth the result.
5. Only validated server content creates actors in this phase. Canonical removal
   requires a later typed domain command; absence from a client view means only
   interest leave.
6. No client simulates canonical actor results and no authority handoff occurs.

## Bounds

- At most 4,096 catalog actors.
- At most 32 waypoints per actor.
- Idle has no waypoints; travel and wander require at least one.
- Actor and prototype IDs are nonzero strong identities; actor IDs and entity IDs
  are unique within the catalog, and actor entity IDs cannot overlap players.
- All roots belong to the negotiated manifest's exact cell catalog.

## Deferred behavior

Combat, damage, death, resurrection, dialogue, inventory, equipment, schedules,
needs, pathfinding/navmesh parity, dynamic spawn/removal, scripting, general
persistence, and client proposal leases remain later domain work.

## Approval

The project owner approved Package A in the 2026-09-07 working session.
