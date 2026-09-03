# GDR-0015: Phase 8 remote motion presentation

Status: **Accepted**

Date opened/approved: 2026-09-03

Decision owner: project owner

Needed by: Phase 8 Slice 8.6

Companion architecture record:
[`ADR-0052`](../adr/ADR-0052-phase8-remote-motion-smoothing.md)

## Decision

The owner approved Option A as provisional desktop-fixture presentation:

- render remote position two server ticks (66.7 ms) behind the newest usable
  authoritative sample and interpolate linearly between samples;
- extrapolate with authoritative linear velocity for at most three server
  ticks (100 ms), then hold;
- blend an ordinary correction of at most 16 OpenMW units (16,384 canonical
  quanta) over two server ticks;
- hard-snap larger corrections and identity, cell, or authority-epoch
  discontinuities; and
- leave orientation and animation outside this slice.

This affects only client-local remote presentation. Server authority, accepted
snapshot state, local-player correction, and canonical movement do not change.

## Alternatives rejected

A one-tick buffer with shorter prediction was rejected because the approved
Phase 7 jitter profile can remain visibly exposed. Adaptive delay, thresholds,
and longer prediction were rejected because Phase 12 owns production movement
tuning. State retention across observation lifetimes was rejected because it
can present a stale remote as current.

## Required evidence

- Four samples are the exact per-entity maximum and older history is discarded.
- Two-tick interpolation produces exact endpoints and bounded intermediate
  positions.
- Extrapolation never exceeds three ticks and then holds.
- Small correction remains continuous and finishes within two ticks; a larger
  correction and each discontinuity hard-snap.
- Snapshot age, buffer depth, extrapolation time, correction distance, and hard
  snaps emit typed bounded observations without identity dimensions.
- Despawn, cell/epoch change, disconnect, and provider failure clear state.
- The two-client desktop demo shows bounded smooth remote motion and convergence
  under the approved adverse-network profile.

## Boundary and review trigger

This is fixture presentation, not production locomotion. Phase 12 GDR-0004
still owns adaptive jitter policy, prediction, correction tuning, orientation,
animation, collision, speed, and VR root/pose behavior.

## Owner approval

Approved by the project owner on 2026-09-03 as the recommended Option A package.
