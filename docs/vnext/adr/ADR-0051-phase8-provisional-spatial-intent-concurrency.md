# ADR-0051: Phase 8 provisional spatial-intent concurrency

Status: **Accepted**

Date opened/approved: 2026-09-03

Decision owner: project owner

Needed by: Phase 8 Slice 8.5

Companion gameplay record:
[`GDR-0014`](../gdr/GDR-0014-phase8-desktop-movement-and-correction.md)

## Decision

The owner approved Option A. For the two provisional Phase 7 spatial intent
bodies (`PlayerMotionIntent` and `FixtureCellTransition`), the entity revision
in the required precondition records observed context but is not an equality
gate. The reducer still validates active session, generation, contiguous
sequence, command identity, bound entity, current authority epoch, and the
body's domain rule before mutation. Reliable command order serializes these
intents. Future command bodies retain exact revision checks unless separately
approved.

The desktop coordinator keeps at most one pending motion command and one latest
desired intent. It issues the latest intent only when no motion command is
pending and authoritative velocity differs, then uses contiguous acknowledgement
to release the pending slot. This bounds traffic and guarantees that a changed
or zero/stop intent is retried against later authoritative state.

Existing OpenMW action-state and same-cell move APIs are sufficient. Slice 8.5
adds no engine hook or patch-registry entry.

## Rationale and alternatives

Canonical movement advances entity revision every non-zero server tick. Exact
revision equality therefore makes a delayed continuous-motion command stale
before arrival. Waiting/retrying behind exact equality cannot converge when
latency exceeds one tick. A new latest-wins motion protocol is the preferred
production design question, but input sequence/acknowledgement, rate, loss, and
security rules remain Phase 12 work.

## Required evidence

- Delayed ordered motion start/change/stop and fixture transition converge.
- Wrong session, generation, entity, epoch, duplicate ID, and sequence fail.
- Motion queuing never exceeds one pending command plus one latest intent.
- No OpenMW type crosses the adapter boundary and no new engine patch lands.

## Review trigger

Phase 12 must replace or ratify this provisional reliable-intent policy when it
defines production locomotion input sequencing and acknowledgement.

## Owner approval

Approved by the project owner on 2026-09-03 as Option A for Slice 8.5 command
concurrency and hook scope.
