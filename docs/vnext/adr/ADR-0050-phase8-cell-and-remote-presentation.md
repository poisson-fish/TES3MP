# ADR-0050: Phase 8 cell and remote presentation

Status: **Accepted**

Date opened/approved: 2026-09-03

Review reopened: 2026-09-03

Decision owner: project owner

Needed by: Phase 8 Slice 8.4

## Decision

The owner approved the breaking package: targeted snapshots explicitly carry
target player/entity identity; reliable queue calls return their assigned
sequence; the adapter tracks one in-flight cell transition and one coalesced
deferred transition; fixture cell/avatar records are explicit validated
configuration; and provider failures are typed and fail closed.

Remote players are presentation objects keyed by `EntityId`. P8-004 provides a
narrow opaque renderer-only transient NPC API. It may reuse NPC rendering
internals, but never registers the avatar in `WorldModel`, `CellStore`, physics,
navigation, mechanics, Lua, or persistence. Reconciliation creates, updates,
and destroys the exact authoritative observed set and clears it before engine
dependency teardown.

## Rejected alternatives

Session/player numeric equivalence, a separate identity side message, normal
gameplay actor placement, direct adapter access to renderer internals, and
unbounded transition queues were rejected.

## Verification

Codec/session tests cover explicit stable target binding and malformed input.
Runtime and adapter contracts cover returned sequence ownership, bounded
transition handling, failure closure, and cleanup. Patch-registry verification
and a full RelWithDebInfo OpenMW link cover P8-004. The Slice 8.4 owner demo must
still show interior/exterior correction and peer spawn/despawn with two clients.

## Reopened remote-replica boundary

The real-content Phase 8 run showed that the nominal renderer-only seam closes
during remote creation. Inspection also showed that it initializes full NPC
custom data, including mechanics and inventory state, despite the boundary
claimed above. Archived TES3MP used normal dynamic NPC actors but required
dedicated-player exceptions across 16 engine files.

The owner approved focused replicated-actor architecture research on 2026-09-03.
The accepted cell-transition behavior and server authority remain in force, but
P8-004's remote representation mechanism is not sufficient completion evidence.
No replacement role, hook surface, or subsystem participation has been approved.
