# ADR-0047: Phase 8 adapter lifecycle and provider boundary

Status: **Accepted**

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

## Approved frame order

The project owner approved Option A on 2026-09-02: bounded inbound drain, apply
authoritative presentation, sample current semantic input, queue the command,
then perform a bounded outbound flush. This runs after OpenMW input update.
Tests must prove the exact order, both work caps, non-blocking behavior, feedback
suppression, and failure closure.

## Approved reusable session runtime

Repository inspection after frame-order approval found that
`HeadlessClientSession::pump()` combines transport polling and resulting work.
It cannot express the approved distinct inbound-drain and outbound-flush passes.
The project owner approved Option A on 2026-09-02: add a reusable client-session
runtime that owns bounded inbound receive/decode and outbound encode/queue/flush,
then compose both headless and OpenMW clients through it. The adapter receives
typed domain results and must not own a private packet or transport pump.

The owner further approved the concrete Option A boundary on 2026-09-02: a new
`ClientSessionRuntime` owns the existing session and outbound queue, exposes
separate bounded typed `drainInbound`, `queue`, and `flushOutbound` passes, and
closes the session on transport or protocol failure. Expanding the headless-only
class and using a borrowing pump helper were rejected.

Putting transport composition in the OpenMW adapter was rejected because it
violates ADR-0007. Retaining the combined caller-owned flow was rejected because
it cannot guarantee the approved frame order.

## 2026-09-03 Slice 8.4 amendment

The owner approved the breaking corrective package. The coordinator captures
OpenMW's semantic cell-change latch before authoritative correction, queues one
tracked transition, coalesces at most one deferred transition, and suppresses
stale correction until acknowledgement finalizes the tracked command. Provider
application returns typed content/presentation failures and presentation is
cleared before borrowed OpenMW dependencies are destroyed. Runtime queue calls
return the assigned command sequence so callers never infer it.

Snapshot headers carry explicit target player/entity identity. Desktop content
mapping is explicit configuration for the fixture interior, exterior
worldspace, and avatar NPC. Remote avatars use the approved P8-004 opaque
renderer-only transient actor seam; they must not enter `WorldModel`, cell
stores, physics, navigation, mechanics, Lua, or persistence.

## 2026-09-02 owner-approved runtime amendment

After the first caller migration exposed duplicated protocol progression in the
headless executable, the owner approved a breaking correction (Option D). The
reusable runtime is the complete client orchestrator, not merely a typed codec
and transport queue. It owns connection and negotiation progression,
authentication submission, initial session binding, authoritative snapshot and
observation application, command sequencing, bounded transport work, and
failure closure. Callers supply configuration/credentials and semantic intent;
they consume typed state and presentation events. They do not drive the session
state machine or assemble protocol envelopes.

The runtime remains engine-independent and transport-injected. OpenMW owns the
concrete transport/clock/runtime aggregate through its coordinator; headless and
OpenMW callers use the same orchestration API. Endpoint selection, credential
acquisition, and actionable OpenMW UI policy remain Slice 8.3 decisions.

Alternatives rejected by the owner were retaining duplicated caller
orchestration, adding a partial helper while ownership stayed split, and
unconditionally attaching a dormant OpenMW coordinator.

## Owner approval

Approved by the project owner on 2026-09-02: Option A, two narrow borrowed
providers with coordinator-owned session lifecycle, followed by Option A exact
correct-then-command frame order and the original Option A runtime. The owner
then approved the breaking Option D amendment above after caller migration
revealed split orchestration ownership.

The owner approved the Slice 8.4 corrective package and P8-004 on 2026-09-03.
