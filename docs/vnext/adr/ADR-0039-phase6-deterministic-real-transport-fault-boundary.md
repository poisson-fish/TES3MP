# ADR-0039: Phase 6 deterministic real-transport fault boundary

Status: **Accepted**

Date opened: 2026-09-01

Date approved: 2026-09-01

Decision owner: project owner

Needed by: Phase 6 Slice 6.6

## Decision

The project owner approved Option A on 2026-09-01. The repository-owned,
seeded, manual-clock fault scheduler remains the deterministic source of loss,
duplication, latency, reordering, stalls, disconnects, and bounded pending work.
Real encrypted localhost tests place that scheduler above the owned
`TransportRuntime` boundary and submit its released application messages through
the production adapter.

Selected-library native fake-network controls remain supplemental coverage below
the adapter. They may prove recovery and channel behavior but are not described
as deterministic replay evidence. No GameNetworkingSockets type or fault knob
enters the owned production transport or test-support API.

This slice does not authorize packet corruption, an operating-system network
proxy, production fault controls, new transport semantics, or gameplay behavior.

## Alternatives rejected

A GameNetworkingSockets-specific primary harness would exercise a lower layer,
but its global controls and real-time random behavior do not provide the owned,
same-seed deterministic contract. An operating-system UDP proxy or platform
network tooling would offer deeper packet-level control at the cost of a much
larger platform-specific and CI surface. Neither is required for this slice.

## Consequences and failure modes

Deterministic application-message faults can be replayed identically across
platforms and do not couple test support to the selected transport. Because the
scheduler sits above the adapter, it does not claim deterministic UDP-packet or
retransmission behavior. Native below-adapter tests use real time and may fail
from an unhealthy or excessively slow host; their bounded timeout reports that
failure without changing the deterministic transcript contract.

The decision must be reviewed if a future phase requires deterministic packet
corruption, congestion-control internals, multi-process network partitions, or
a selected transport whose public test controls can provide owned seeded and
caller-clocked behavior without leaking dependency types.

## Acceptance

Tests demonstrate that the existing same-seed scheduler contract remains green;
faulted messages in both directions and both delivery classes cross encrypted
localhost sockets; reliable ordering and latest-wins independence retain their
approved semantics; pending work and overflow remain bounded; stall/disconnect
behavior remains explicit; and the supplemental native loss/reordering test is
kept without claiming deterministic replay. Existing lifecycle, queue,
telemetry, authentication, target-boundary, and baseline checks must remain
green.

## Owner approval

Approved without amendment on 2026-09-01: Option A and the proposed acceptance
scenarios.
