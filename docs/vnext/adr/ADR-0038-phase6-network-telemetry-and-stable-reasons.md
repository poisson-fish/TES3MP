# ADR-0038: Phase 6 network telemetry and stable reasons

Status: **Accepted**

Date opened: 2026-08-31

Date approved: 2026-08-31

Decision owner: project owner

Needed by: Phase 6 Slice 6.5

## Decision

The project owner approved Option A for all five decisions on 2026-08-31:

1. transport owns a selected-library-free, closed typed telemetry interface
   with an injected non-blocking sink;
2. exact application counters cover submitted, admitted, received, coalesced,
   rejected, blocked, and evicted work, with per-channel queue gauges and honest
   selected-library pending/unacknowledged-byte gauges;
3. cumulative counters saturate and bounded gauges are sampled during explicit
   pumping; telemetry failure never changes transport or session behavior;
4. stable closed reasons describe local graceful/abort, peer close, timeout,
   authentication denial/unavailability, protocol violation, slow peer,
   capacity, resolution, security, dependency, and shutdown outcomes while
   private library diagnostics remain private; and
5. metric dimensions are closed and low-cardinality. Structured lifecycle
   observations may contain a process-local owned connection ID but never an
   endpoint, address, admission scope, credential, packet bytes, or free text.

GameNetworkingSockets 1.6 exposes lane pending reliable/unreliable and
unacknowledged reliable bytes but no exact public per-lane retransmission
counter. The implementation records those gauges as retransmission pressure;
it does not patch private internals or label an estimate as an exact count.

## Alternatives rejected

Depending on server-core observability would reopen the target graph. Exposing
GNS types/codes would make a selected dependency part of the stable API.
Instrumenting private internals or estimating retransmissions would create a
fragile or misleading contract. Per-packet events and an internal asynchronous
telemetry queue add unbounded work or another thread/queue policy. Endpoint or
connection metric labels violate the approved privacy/cardinality boundary.

## Acceptance

Tests assert exact per-channel application counters and queue gauges, honest
lane pressure mapping, saturation, sink-failure isolation, exhaustive stable
reason mapping, unknown-library failure closure, redaction, bounded cardinality,
and preservation of transport/authentication/queue/target-boundary contracts.

## Owner approval

Approved without amendment on 2026-08-31: Option A for Decisions 1 through 5,
including the retransmission-pressure interpretation and proposed tests.
