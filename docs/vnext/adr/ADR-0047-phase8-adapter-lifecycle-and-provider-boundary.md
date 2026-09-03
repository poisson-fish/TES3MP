# ADR-0047: Phase 8 adapter lifecycle and provider boundary

Status: **Accepted; frame order pending**

Date opened: 2026-09-02

Decision owner: project owner

Needed by: Phase 8 Slice 8.2

## Approved decision

The project owner approved Option A on 2026-09-02. The OpenMW adapter uses two
narrow borrowed interfaces: a `SemanticInputProvider` supplies platform-neutral
current intent and a `PresentationProvider` receives authoritative session
results. The adapter coordinator owns client-session lifecycle and borrows both
providers only for its bounded main-thread frame call. Provider references must
outlive the coordinator, and coordinator shutdown precedes provider and OpenMW
dependency destruction.

A combined platform facade was rejected because it mixes observation and
presentation authority. Loose callbacks were rejected because they weaken
lifetime, thread, and bounded-work contracts.

## Constraints and acceptance

- OpenMW, SDL, OSG, renderer, and transport-library types do not cross into the
  client session or semantic provider values.
- Providers are called only from the engine-owning main thread.
- Each frame does bounded, non-blocking work; network stalls cannot block input,
  rendering, or UI.
- Authoritative presentation cannot feed back as a new local command.
- Multiplayer-disabled behavior and frame ordering remain unchanged.
- Tests cover provider lifetime, call thread/order, bounded work, type boundary,
  feedback suppression, disabled mode, and ordered shutdown.

## Pending decision

Before production implementation, the owner must approve the exact per-frame
ordering among inbound session drain, authoritative presentation, and current
input sampling/submission. That order affects correction and command semantics
and is not inferred from this interface approval.

## Owner approval

Approved by the project owner on 2026-09-02: Option A, two narrow borrowed
providers with coordinator-owned session lifecycle. Exact frame order remains
pending.
