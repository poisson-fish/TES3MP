# ADR-0048: Canonical revision and simulation tick separation

Status: **Accepted**

Date opened: 2026-09-02

Decision owner: project owner

Needed by: Phase 8 Slice 8.2

## Decision

The project owner approved Option A3 on 2026-09-02. `ServerTick` remains the
fixed-rate simulation scheduler coordinate. A distinct monotonic
`CanonicalRevision` orders authoritative publications and command bases. Every
committed join, leave, resume, expiration, or command batch advances exactly
one canonical revision.

Snapshots and reliable observation batches carry canonical revision. Commands
name the canonical revision observed by their sender. A client rejects two
different publications with the same revision. Wire compatibility with the
pre-vNext development protocol is not retained.

## Rationale

Lifecycle state can commit more than once during one simulation tick. Reusing
the simulation tick as a publication revision creates valid authoritative
updates that look contradictory to clients. Delaying lifecycle work or
accepting contradictory same-tick snapshots weakens either responsiveness or
the contradiction guard. Separate coordinates state the two invariants
directly.

## Alternatives considered

- A1: permit different authoritative content at one tick. Rejected because it
  removes the client's contradiction invariant.
- A2: assign synthetic ticks or defer/batch lifecycle commits. Rejected because
  scheduler time would no longer mean simulation time and lifecycle latency
  would depend on batching.
- A3: use separate simulation and publication coordinates. Selected because it
  preserves both invariants and remains correct when lifecycle rates grow.

## Consequences and failure modes

The development wire schema breaks intentionally and generated bindings must
change with it. Canonical state versions may advance several times inside one
commit, while canonical revision advances once. Revision exhaustion rejects a
new preparation. Reused revisions with different content remain protocol
errors; ticks must never be substituted for revisions at projection seams.

## Review or replacement trigger

Review this decision only if authoritative commits become multi-server or need
durable globally ordered identities that a process-local monotonic counter
cannot provide.

## Required evidence

- Multiple lifecycle commits during one simulation tick receive increasing
  canonical revisions.
- Simulation movement remains paced only by fixed `ServerTick` scheduling.
- Commands based on the latest published canonical revision are accepted.
- Duplicate canonical revisions are accepted only for identical content.
- Headless lifecycle, movement, reconnect, and convergence proofs pass.

## Owner approval

Approved by the project owner on 2026-09-02 after explicitly accepting the
breaking protocol and domain migration in preference to a short-term batching
workaround.
