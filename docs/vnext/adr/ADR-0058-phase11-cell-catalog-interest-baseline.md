# ADR-0058: Phase 11 cell catalog, interest, and baseline resync

Status: **Implemented**

Date: 2026-09-06

Decision owner: project owner

## Decision

Replace the two-cell fixture with a bounded manifest catalog of typed cell-space
IDs and exact allowed cells. The server validates every cell-transition proposal
against that catalog. The OpenMW leaf maps every declared cell space to one local
`ESM::RefId`; missing, duplicate, kind-mismatched, or case-insensitively colliding
mappings fail closed. Record names remain local and never enter protocol state.

Keep interest server-owned and equal to exact canonical-cell membership. Reliable
ordered messages carry membership changes. Latest-wins messages carry complete
spatial samples. Pose relay consults the same interest predicate.

Add two protocol 1.2 roots:

- `ReliableInterestBaseline`: target session/generation, canonical revision,
  canonical state version, server tick, and at most 256 sorted unique
  player/entity members.
- `SessionResyncRequest`: authenticated session/generation, a closed reason enum,
  and the last observed state version; it carries no client state.

Join, resume, and resync atomically enqueue a baseline and a scoped snapshot.
Readiness requires both, with snapshot revision at or beyond the baseline. A new
baseline replaces membership and resets delta history. Server dispatch retains
at most one pending resync request per connection generation and coalesces floods.

## Consequences

- Protocol and server/client content configuration advance incompatibly from 1.1
  to 1.2; mixed versions reject during negotiation.
- Exact exterior catalogs can express holes and negative or boundary grid values
  without admitting arbitrary signed-32-bit coordinates.
- Membership reconstruction is explicit even for empty or quiet streams and no
  longer depends on synthetic `Enter` deltas.
- Canonical state layout, checksum encoding, persistent identity format, and
  authoritative write ownership remain unchanged.

## Exclusions

Adjacent-cell visibility, interest chosen by clients, teleport legality,
prediction, collision, world-object streaming, and general persistence remain
future work.
