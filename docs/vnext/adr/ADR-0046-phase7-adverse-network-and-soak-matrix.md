# ADR-0046: Phase 7 adverse-network and soak matrix

Status: **Accepted**

Date opened: 2026-09-02

Date approved: 2026-09-02

Decision owner: project owner

Needed by: Phase 7 Slice 7.7

## Decision

The project owner approved Option A: use a layered verification matrix. The
repository-owned seeded manual-clock scheduler is the deterministic authority
for latency, jitter, duplication, reordering, stalls, disconnects, replay, and
pending-work bounds above the owned transport boundary. Supplemental encrypted
localhost tests cover selected-library packet loss and reordering. Real server
and two-client processes cover reconnect loops and soak behavior without
claiming deterministic packet scheduling.

The approved initial thresholds are 50 ms latency, 20 ms maximum jitter, 30 ms
maximum reordering, 5% loss and 5% duplication where message semantics permit,
a 500 ms stall, 32 reconnect cycles, existing hard queue capacities with zero
growth after drain, 10,000 deterministic ticks, a 60-second real-process soak,
fixed recorded seeds, and zero durable divergence.

Real-process-only fault control was rejected because timing and replay would be
nondeterministic. An operating-system proxy was rejected because it adds a
platform-specific boundary already excluded by ADR-0039.

## Acceptance

- named profiles reproduce byte-identical traces for the same seed;
- reliable operations remain ordered/apply-once and latest-wins state converges
  under the faults each message class permits;
- stalls and queue pressure remain within existing message/byte caps and drain
  to zero without durable divergence;
- 32 disconnect/resume cycles preserve identity and progress;
- 10,000 deterministic ticks and the 60-second real-process soak finish with
  bounded memory/queues and matching canonical client views; and
- failures report profile, seed, threshold, and bounded evidence without
  credential or raw-payload output.

## Review triggers

Reopen before adding packet corruption, an OS proxy, production fault controls,
unbounded or wall-clock-dependent deterministic tests, different queue policy,
or gameplay recovery semantics.

## Owner approval

Approved by the project owner on 2026-09-02: Option A and all proposed initial
thresholds and acceptance scenarios.
