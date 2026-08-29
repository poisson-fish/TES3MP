# ADR-0033: Phase 6 transport channel and delivery semantics

Status: **Accepted**

Date opened: 2026-08-28

Date approved: 2026-08-28

Decision owner: project owner

Needed by: Phase 6 Slice 6.2

## Decision question

How should session control, reliable apply-once operations, and latest-wins
snapshots share one encrypted GameNetworkingSockets connection; what owned API
should expose their channel identity and bytes; and which delivery, recency,
failure, and provisional work-bound behavior belongs in Slice 6.2 rather than
the later queue, telemetry, and fault-harness slices?

This decision refines ADR-0005's required reliable/latest-wins separation and
ADR-0032's lifecycle-only adapter. It does not change protocol schemas,
authentication, resumption, gameplay authority, canonical state, or player-
visible movement behavior.

## Decision summary

The project owner approved Option A for Decisions 1 through 3 on 2026-08-28:

1. use two fixed owned channels, `ReliableOrdered` and `LatestWins`, mapped to
   two independent equal-priority, equal-weight GameNetworkingSockets lanes;
2. expose separate owned send and bounded receive-drain operations carrying a
   closed channel value and owned bytes, while keeping protocol decoding and
   class/channel validation outside the selected-library adapter; and
3. implement only minimal fail-closed delivery semantics in Slice 6.2, leaving
   application queues, retries, coalescing, rate policy, slow-peer eviction,
   detailed disconnect catalogs, and product capacity to Slices 6.4 and 6.5.

Session control and reliable operations map to `ReliableOrdered`. Latest-wins
snapshots map to `LatestWins`. Reliable acceptance is transport admission, not
application commit. Latest-wins messages may drop or reorder; the receiving
client-session still uses `ServerTick` to reject stale snapshots.

## Existing constraints

1. ADR-0005 selects the pinned encrypted-but-endpoint-unauthenticated
   GameNetworkingSockets profile and requires distinct reliable and unreliable
   congestion behavior.
2. ADR-0021 fixes 12-byte protocol framing plus 4 KiB session-control, 16 KiB
   reliable-operation, and 64 KiB latest-wins payload ceilings.
3. ADR-0024 keeps command idempotency independent of transport reliability and
   makes `ServerTick` the latest-wins snapshot recency key.
4. ADR-0032 keeps GameNetworkingSockets types private, uses an explicitly
   pumped owning runtime, and exposes never-reused owned connection IDs.
5. Slice 6.4 owns product queues, coalescing, priority/rate/backpressure policy,
   slow-peer eviction, and measured product capacity. Slice 6.5 owns detailed
   stable telemetry and disconnect/rejection categories.

## Representative scenarios

1. Twenty-four reliable operations encounter loss and reordering. They retain
   message boundaries and arrive in submission order.
2. A large reliable fragment is delayed by loss. A newer snapshot on the other
   lane reaches the receiver first instead of freezing sampled state behind the
   retransmission.
3. Snapshots for ticks 20, 22, and 21 arrive in that order. Transport may deliver
   all three; the client-session accepts 22 as newest and rejects 21 as stale.
4. A snapshot is presented on the reliable channel or a reliable operation on
   the latest-wins channel. The protocol/session composition rejects the class
   mismatch before application state changes.
5. An outbound message is empty, over its channel's whole-frame ceiling, or
   names an unknown channel. It is rejected before selected-library admission.
6. An inbound message has an unknown lane, contradicts the lane's reliability,
   or exceeds its channel ceiling. No bytes are delivered and the connection
   fails closed.
7. A caller supplies space for more than 128 messages in one receive drain. The
   call is rejected rather than turning caller input into unbounded work.
8. A send targets a pending, closed, or replaced owned connection. It cannot
   reach another library handle or acquire session authority.

## Decision 1: fixed channel topology and lane scheduling

### Option A: two equal-priority fixed channels (approved)

Define `ReliableOrdered` and `LatestWins` as closed project-owned channel values.
Configure exactly two lanes in both directions before exposing an established
connection. Give both lanes priority zero and weight one. Map reliable traffic
to lane zero with reliable ordered delivery. Map latest-wins traffic to lane one
with unreliable no-delay delivery.

Independent lanes remove reliable stream head-of-line blocking. Equal scheduling
prevents either class from acquiring strict transport priority before Slice 6.4
adds measured queue, rate, and coalescing policy.

### Option B: strict snapshot priority

Give the latest-wins lane higher priority. This minimizes snapshot latency but
allows sustained sampled traffic to starve reliable work until later rate and
coalescing controls exist.

### Option C: one lane with per-message flags

Use one lane and choose only reliable/unreliable flags per message. This is
smaller, but a delayed reliable fragment can block newer sampled state and it
does not satisfy the Phase 6 delivery-class gate.

## Decision 2: owned send/receive and protocol boundary

### Option A: explicit channel plus owned bytes (approved)

Add `send(connection, channel, bytes)` and
`receive(connection, callerProvidedOutput)` to the owned runtime. Send copies
bounded caller bytes into the selected library. Receive validates every drained
library message as one batch before publishing any owned result, then copies
validated channel-tagged bytes into caller-provided storage.

Keep payload delivery separate from lifecycle events so potentially 64 KiB
messages do not consume or enlarge the retained lifecycle-event queue. Define a
pure protocol-class mapping: session control and reliable operations require the
reliable channel; latest-wins snapshots require the latest-wins channel. The
adapter does not parse FlatBuffers, authenticate identities, validate command
authority, compare ticks, or mutate session/canonical state.

### Option B: decode protocol frames inside transport

Have transport parse frames and choose or validate lanes. This centralizes one
check but couples socket delivery to protocol decoding and weakens the replaceable
selected-library boundary.

### Option C: expose numeric lanes and flags

Let callers choose selected-library lane numbers, reliability flags, and
priority. This is flexible but leaks dependency policy and permits accidental
semantic downgrade or promotion.

## Decision 3: Slice 6.2 delivery and failure scope

### Option A: minimal fail-closed semantics (approved)

Reliable send acceptance means only that transport admitted the message.
Command IDs, revisions, final dispositions, and acknowledgements still provide
application apply-once behavior. Latest-wins delivery may drop, duplicate, or
reorder; transport does not retry or choose a winner, and client-session recency
checks remain authoritative.

Reject empty, over-budget, unknown-channel, over-drain, pending-connection, and
stale-connection calls with closed owned results. Configure the selected library
to reject messages above the largest approved whole-frame ceiling and retain at
most the initial 128-message proof scope. Validate a complete receive batch
before exposing any bytes. Unknown lanes, reliability contradictions, and
over-budget input close that connection with an owned invalid-message failure.

Do not add an application send queue, retry queue, snapshot coalescer, bandwidth
weighting beyond the equal lane profile, rate limit, slow-peer eviction, or
product player capacity. A selected-library full/ignored result becomes owned
`WouldBlock`; policy for retrying, replacing, or evicting remains Slice 6.4.

### Option B: productize queues and coalescing now

Add reliable retry queues and latest-wins replacement storage in this slice.
This gives more complete congestion behavior but prematurely decides Slice
6.4's capacity and eviction policy without its required measurements.

### Option C: silently discard malformed or misrouted input

Keep the connection alive and drop invalid messages. This reduces disconnects
but weakens fail-closed behavior and makes protocol abuse less observable.

## Approved acceptance tests and demo

1. session control/reliable operations map only to `ReliableOrdered`, while
   latest-wins snapshots map only to `LatestWins`;
2. unknown message classes/channels and exact-plus-one channel sizes reject;
3. public abstraction/factory headers contain no selected-library types;
4. both endpoints configure two equal-priority/equal-weight lanes before their
   established events;
5. reliable messages retain order and boundaries under loss/reordering;
6. a latest-wins message passes a delayed large reliable fragment;
7. bidirectional encrypted loopback preserves owned channel identity and exact
   bytes;
8. one receive drain cannot exceed 128 messages;
9. invalid lane, reliability, or size input publishes no partial receive batch
   and closes only the affected connection;
10. empty, oversized, full-buffer, pending, stale, and unknown-connection sends
    return closed owned results without an implicit retry;
11. all new selected-library calls use the public flat API boundary and target
    dependency checks continue to pass; and
12. applicable protocol, lifecycle, adapter, baseline-provenance, formatting,
    and repository policy tests pass locally.

The implementation demo must show class mapping, exact boundary rejection,
ordered reliable delivery, a snapshot bypassing delayed reliable work, and the
absence of application queue/coalescing or gameplay behavior. Owner demo
acceptance remains required before Slice 6.2 becomes **Implemented**.

## Consequences

- Delivery intent is explicit and cannot be selected through raw library flags.
- A delayed reliable fragment does not freeze newer canonical samples.
- Equal lane scheduling avoids making snapshot latency a strict starvation
  policy before product queue controls exist.
- Transport remains a bounded byte-delivery layer; protocol/session/server-core
  retain decoding, recency, idempotency, authority, and mutation ownership.
- Session control shares the reliable lane without becoming a gameplay
  operation or receiving application commit semantics.
- The initial 128-message drain/buffer scope is a safety ceiling, not a release
  capacity promise, and must be revisited in Slice 6.4.

## Failure modes and mitigations

- **Reliable delivery is mistaken for apply-once commit:** retain command IDs,
  reducer deduplication, final dispositions, and acknowledgements.
- **Snapshot is accidentally made reliable:** use a closed class mapping and
  reject class/channel contradictions before session application.
- **Sample flood starves commands:** use equal lane scheduling now; Slice 6.4
  adds coalescing/rate/backpressure limits before product operation.
- **Malformed input partially escapes a drain:** validate the complete bounded
  receive batch before assigning caller output.
- **Library handles or lanes leak:** expose only owned connection/channel/result
  values and require flat selected-library calls in private adapter/test code.
- **A stale handle receives new-session work:** preserve ADR-0032 generation
  bindings and reject finalized owned IDs.

## Review and replacement triggers

Reopen this ADR if measured traffic requires more than two delivery classes;
session control requires isolation from other reliable work; equal scheduling
cannot preserve both reliable progress and sample opportunity; unreliable
messages cannot carry a future approved snapshot size safely; product queue
evidence requires a different lane topology; or a future selected transport
cannot preserve these owned semantics.

## Owner approval

Approved by the project owner in the 2026-08-28 working session: Option A for
Decisions 1 through 3 without amendment.

This approval authorizes only Slice 6.2 channel mapping and minimal delivery.
It does not approve authentication/resumption, application queue policy,
product capacity, detailed telemetry/disconnect catalogs, socket fault-harness
integration, gameplay authority, state scope, or player-visible behavior.
