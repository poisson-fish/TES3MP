# ADR-0052: Phase 8 remote motion smoothing

Status: **Accepted**

Date opened/approved: 2026-09-03

Decision owner: project owner

Needed by: Phase 8 Slice 8.6

Companion gameplay record:
[`GDR-0015`](../gdr/GDR-0015-phase8-remote-motion-presentation.md)

## Decision

The owner approved Option A. A pure adapter-side remote-motion buffer is owned
by the desktop presentation provider. The coordinator supplies its injected
monotonic time when authoritative samples arrive and on every frame. Renderer
objects only consume resolved poses. No protocol, canonical-state, authority,
or OpenMW hook change is introduced.

Each remote `EntityId` has at most four samples. State is client-local and
ephemeral; despawn, cell or authority-epoch discontinuity, disconnect, and
provider failure clear it. It is neither persisted nor resumed.

Remote-motion metrics use an adapter-owned closed typed observation and
explicit non-owning, non-blocking `noexcept` sink. Desktop composition supplies
an explicit no-op sink; tests may use a bounded recorder. Metrics contain no
entity, player, session, text, or byte dimensions.

## Alternatives and selection

Coordinator-owned smoothing was rejected because it mixes OpenMW presentation
policy into reusable session orchestration. Renderer-owned timing was rejected
because it requires a deeper engine seam and hides state lifetime. Server-owned
buffering was rejected because presentation delay is not canonical state.
Retaining state across reconnect/cell changes was rejected because samples are
from a discontinuous observation lifetime. Adding OpenMW keys to server-core
observability or exposing test-only getters was rejected in favor of a
domain-owned production observation boundary following ADR-0020 principles.

The selection keeps ownership beside remote renderer handles, uses the existing
clock and renderer update seam, bounds memory exactly, and preserves target
topology.

## Consequences and failure modes

Presentation advances every frame even without a new snapshot. A backward
clock observation makes no progress. Invalid entity/revision history fails the
provider closed. Sink rejection never changes presentation. Arithmetic and
history remain bounded; exhausted playback holds the last capped pose.

The fixture policy adds visible delay and may hold during a long stall. These
are measured provisional behaviors, not production locomotion policy.

## Verification

Focused contracts cover sample capacity, interpolation, extrapolation cap,
hold, correction blending, hard snaps, reset, clock regression, typed metrics,
sink rejection, and coordinator per-frame advancement. Existing adapter,
protocol, patch-registry, boundary, and full OpenMW build gates remain required.

## Review triggers

Reopen before adaptive timing, production prediction/correction tuning,
orientation or animation smoothing, cross-cell retention, a shared client
observability target, a new thread/queue, or any engine/protocol/canonical hook.
Phase 12 GDR-0004 replaces or ratifies the fixture policy.

## Owner approval

Approved by the project owner on 2026-09-03 as the recommended Option A package
for ownership, scope/bounds, fixture behavior, and metrics.
