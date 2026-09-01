# ADR-0037: Phase 6 bounded outbound queue and slow-peer policy

Status: **Accepted**

Date opened: 2026-08-31

Date approved: 2026-08-31

Decision owner: project owner

Needed by: Phase 6 Slice 6.4

## Decision question

How should reliable operations and latest-wins snapshots be retained, scheduled,
rate-limited, and discarded or disconnected when the owned transport reports
backpressure?

## Decision summary

The project owner approved Option A for all four decisions and the proposed
ceilings/tests on 2026-08-31:

1. use one bounded reliable FIFO and one replaceable latest-wins slot per
   connection, with validated caller policy below fixed hard ceilings;
2. service a bounded reliable burst followed by one latest-wins opportunity,
   retaining ADR-0033's equal-priority transport lanes;
3. use injected-monotonic-time per-channel token buckets, reject reliable
   overflow, and replace pending sampled state; and
4. evict only after reliable work remains blocked through a configured deadline
   or consecutive-block threshold; snapshot replacement alone never evicts.

The hard safety ceilings are 256 connections, 256 reliable messages and 4 MiB
of reliable bytes per connection, one latest-wins frame within its existing
64 KiB payload ceiling, and 32 send attempts per connection per pump. Caller
policy may reduce but not raise these values.

## Options considered

For ownership, relying only on selected-library buffering hides memory policy,
while one shared application queue loses channel-specific overload semantics.
For scheduling, either strict priority can starve the other delivery class.
For rate control, a shared bucket permits cross-class starvation and queue-only
limits do not bound repeated admission work. For slow peers, first-block
eviction penalizes transient congestion while never evicting permits indefinite
reliable retention.

The approved per-connection, per-channel Option A policy keeps selected-library
types private, preserves reliable FIFO order, drops only superseded sampled
state, and isolates a blocked peer from healthy peers.

## Acceptance tests

Tests cover exact and exact-plus-one message, byte, rate, and work bounds;
reliable FIFO after repeated backpressure; ten-snapshot newest-only replacement;
bounded reliable bursts plus snapshot opportunity; deterministic refill edges
and clock regression; transient versus sustained blocking; isolated eviction;
and complete release on eviction/clear. Existing channel, encryption,
authentication, redaction, target-boundary, provenance, formatting, and policy
checks remain applicable.

## Consequences and deferrals

The queue owns byte retention and admission work only. It does not decode
protocol messages, grant authority, acknowledge application commit, compare
snapshot ticks, or choose gameplay behavior. Slice 6.5 owns stable public
disconnect reasons and detailed telemetry. Operational release defaults and
measured product capacity may be narrowed by later evidence but cannot exceed
these ceilings without reopening this ADR.

## Owner approval

Approved by the project owner on 2026-08-31: Option A for Decisions 1 through 4
and the proposed hard ceilings and tests without amendment.
