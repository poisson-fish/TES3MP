# ADR-0060: Phase 13 server-owned actor foundation

Status: **Accepted**

Date opened: 2026-09-07

Date approved: 2026-09-07

Decision owner: project owner

Companion gameplay record:
[`GDR-0020`](../gdr/GDR-0020-phase13-actor-lifecycle-and-ai.md)

## Decision

The owner approved Package A from the Phase 13 discovery:

1. Actors have stable manifest-scoped actor/prototype identity, a globally unique
   entity identity, and canonical state separate from players and sessions.
2. A deterministic server-owned scheduler is the only actor simulation commit
   source. It uses the existing checked movement and content-collision seam.
3. Actor replication is additive and typed. Actors never acquire fake player or
   session identity, and legacy player protocol remains bounded independently.
4. No client proposal lease exists in the first actor package. Delegation may be
   introduced only by a later measured decision with finite epoch-bound leases,
   atomic complete handoff, revocation, and server fallback.
5. Actor renderer and animation state remain presentation-only and cannot author
   canonical AI, lifecycle, or transform.

## First implementation boundary

The first pass owns the engine-independent actor catalog, immutable canonical
actor world, entity-identity separation, and atomic server scheduler. Server-app
content loading, negotiated wire records, interest/resync, and OpenMW presentation
follow after these core invariants pass. General persistence remains Phase 20.

## Consequences

- Actor/client proposal packets are absent; server simulation consumes only
  immutable catalog data, current canonical state, active server sessions, tick,
  movement profile, and collision results.
- Canonical actor batches are computed before replacement. A failed actor step
  cannot partially publish another actor's step.
- Protocol capability/version selection is deferred to the replication pass, but
  it must preserve 1.2/1.3 player decoding and reliable-membership/latest-wins
  lane separation.
- This decision adds no combat, dialogue, inventory, scripting, spawning API,
  general deletion, persistence store, or VR gameplay authority.

## Approval

The project owner approved Package A in the 2026-09-07 working session.
