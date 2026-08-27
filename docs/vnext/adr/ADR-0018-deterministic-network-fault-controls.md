# ADR-0018: Deterministic network fault controls

Status: **Accepted**

Date opened: 2026-08-26

Date approved: 2026-08-26

Decision owner: project owner

Needed by: Phase 3 Slice 3.5

## Decision questions

How should the reusable Phase 3 harness inject latency, loss, jitter,
duplication, reordering, stalls, and disconnects without changing the accepted
base-link semantics or prematurely defining Phase 6's production transport;
how are independently configurable paths identified; how are fault decisions
made repeatable and isolated; and what bounded behavior applies during stalls,
disconnects, duplication, and underlying-link backpressure?

ADR-0017 already fixes a test-support-only bounded byte duplex, injected
monotonic clock, versioned deterministic RNG, and exact test trace boundary. It
explicitly leaves fault policy to Slice 3.5. This ADR adds only that test
facility. It does not define packet schemas, reliable/latest-wins delivery,
production transport channels, socket behavior, session state, authority,
canonical state, or gameplay behavior.

## Decision summary

The project owner approved Option A for Decisions 1 through 5 on 2026-08-26:

1. place a passive `FaultInjectingLink` wrapper around the accepted
   `InMemoryDuplexLink` in `tes3mp_test_support`;
2. schedule delivery against the injected monotonic clock, with explicit
   pumping, minimum latency, bounded nonnegative jitter and reorder holds, and
   stable deadline/enqueue/copy ordering;
3. identify independently configured fault paths with direction plus a
   test-only nonzero numeric channel key that makes no production channel claim;
4. use an explicit fault seed, integer parts-per-million rates, and isolated
   deterministic streams per direction/channel/fault dimension; and
5. provide explicit per-path stall and disconnect controls, bounded pending
   message/byte budgets, atomic duplicate admission, and independent underlying
   directional backpressure.

## Required boundaries

1. The wrapper and all fault types remain in `tes3mp_test_support`; production
   targets cannot link or include them.
2. `InMemoryDuplexLink` retains its owned FIFO message, directional budget,
   atomic enqueue, and close semantics. A read-only budget observation may be
   added so the wrapper rejects messages that could never reach the base link.
3. The wrapper is single-threaded and passive. It has no sleep, callback,
   worker, socket, selected-transport type, or ambient clock/random source.
4. Direction/channel keys classify test fault policy only. Phase 4 owns message
   classification and Phase 6 owns production channel semantics.
5. Every configured path has nonzero pending message and byte limits. The
   complete original-plus-duplicate admission either fits or queues nothing.
6. Rates use the exact inclusive range 0 through 1,000,000 parts per million.
   No floating-point probability or standard random distribution enters the
   deterministic contract.
7. A fixed profile and seed produce identical decisions, deadlines, delivery
   order, and exact trace on supported compilers. Unrelated path/fault streams
   cannot perturb one another.
8. Fault injection changes delivery observation only. It does not mutate
   protocol values, canonical state, authority, gameplay, or credentials.

## Scenarios evaluated

1. One A-to-B channel has fixed latency while an unrelated B-to-A channel
   delivers immediately.
2. Loss is disabled or certain at the integer endpoints without partially
   queuing a message.
3. Duplication produces at most one additional copy and fails atomically when
   either copy would exceed pending limits.
4. Jitter and reorder holds change deadlines within declared bounds and can
   deliver a later enqueue before an earlier enqueue.
5. Equal deadlines retain stable enqueue and copy ordering.
6. A stalled path retains bounded due packets while other directions/channels
   continue; resuming releases retained packets deterministically.
7. Disconnect discards only the selected path's not-yet-delivered packets and
   rejects later sends; unrelated paths continue.
8. One base-link direction backpressures while the reverse direction continues
   to drain.
9. Clock-plus-delay overflow and impossible message sizes reject without a
   partial pending queue.
10. Two identical scripts produce byte-identical fault traces and a matching
    test-only digest; a different seed changes the trace.

## Decision 1: composition and ownership

### Option A: test-support wrapper around the base link (approved)

Add `FaultInjectingLink` to `tes3mp_test_support`. It owns an
`InMemoryDuplexLink`, configured path state, scheduled owned byte messages, and
fault RNG streams. Callers send through the wrapper, advance an injected clock,
explicitly pump due work, and receive complete messages from the unchanged base
link.

This preserves the simple base-link contract, composes with Phase 4's in-memory
session, and avoids claiming that a test facility is Phase 6 transport.

### Option B: add faults directly to `InMemoryDuplexLink`

This has fewer types, but makes the accepted FIFO link itself probabilistic and
forces every basic link test to configure time, random state, and fault policy.

### Option C: define a production transport interface now

This could maximize later reuse, but prematurely selects lifecycle, channel,
threading, cancellation, and delivery semantics owned by Phase 6.

## Decision 2: timing and reordering

### Option A: passive clock-driven scheduled delivery (approved)

`send` makes deterministic fault decisions and schedules zero, one, or two
owned copies. `pump` observes the injected monotonic clock and transfers due
messages to the base link without sleeping. Minimum latency is always applied;
jitter adds a uniform integer delay from zero through its declared maximum; and
reordering adds a separate uniform hold from zero through its maximum.

The delivery key is due instant followed by global enqueue/copy ordinal. A
reorder hold can therefore move a later enqueue ahead of an earlier enqueue,
while equal deadlines remain stable. Delay arithmetic is checked; an
unrepresentable due instant queues nothing and returns an explicit error.

### Option B: operation-count steps

Advance faults by send/receive counts. This is deterministic but cannot express
the latency and stall timing needed by later fixed-tick simulations.

### Option C: callbacks, sleeps, or a worker thread

This resembles real networking but introduces nondeterministic host scheduling,
lifecycle, and cancellation before the server composition root exists.

## Decision 3: independently configured paths

### Option A: direction plus test-only numeric channel key (approved)

`FaultPath` contains `AtoB` or `BtoA` plus a nonzero `FaultChannelId`. Every
path has its own immutable profile, scheduled queue, RNG streams, stall flag,
and disconnect flag. Up to 64 explicitly configured paths are permitted; empty
and duplicate configurations reject.

The numeric channel key is test vocabulary only. It does not identify a wire
channel, transport lane, reliable class, snapshot class, or capability.

### Option B: define production channel enums now

This would make fixtures read like the intended product, but would decide Phase
4 and Phase 6 classification before their envelopes and transport adapter exist.

### Option C: direction-only profiles

This is smaller but cannot prove that independent traffic classes have
independent loss, timing, stall, and disconnect conditions.

## Decision 4: deterministic fault decisions

### Option A: isolated integer-rate RNG streams (approved)

Construction takes an explicit 64-bit fault seed. Each direction/channel path
derives separate version-one xoshiro streams for loss, duplication, jitter, and
reorder using fixed nonzero numeric domain IDs and the channel ID as subject.
Loss and duplication use integer thresholds over 1,000,000; timing uses the
project-owned unbiased bounded sampler.

Changing traffic or draws on another path or fault dimension therefore cannot
shift this path's sequence. The same complete input reproduces exactly.

### Option B: one shared fault stream

This is simpler but lets unrelated channel traffic change every later fault
decision and makes minimized failures fragile.

### Option C: explicitly scripted outcomes only

This is exact for hand-written cases but cannot generate repeatable profile
matrices and adverse-network simulations from a compact seed.

## Decision 5: stalls, disconnects, and bounds

### Option A: explicit per-path controls and two bounded queue layers (approved)

`setStalled(path, true)` prevents the path's due packets from moving into the
base link while retaining them under its pending message/byte limits. Resume
allows the normal deterministic deadline/ordinal pump to continue. Other paths
remain eligible.

`disconnect(path)` discards that path's not-yet-delivered scheduled messages,
clears stall state, marks it disconnected, and rejects later sends. Messages
already transferred to the base link remain observable because recall would
violate the base-link boundary. Other paths remain usable.

Pending admission counts both duplicate copies before allocation/commit. The
base link remains independently bounded; when one direction is full, its
earliest due message remains pending while the reverse direction can progress.

### Option B: random/periodic stall and disconnect profile fields

This is concise for soak profiles but makes exact lifecycle edges harder to
script and couples control-event timing to random draws.

### Option C: whole-duplex stall/disconnect only

This models a complete connection outage but cannot independently exercise
direction/channel failure and recovery.

## Acceptance tests

Slice 3.5 implements named contracts for:

1. invalid profile, zero channel, empty configuration, and duplicate path
   rejection;
2. exact latency boundary and direction/channel independence;
3. certain loss, certain duplication, at-most-one duplicate, and atomic pending
   budget behavior;
4. bounded jitter/reorder delivery and stable reordering evidence;
5. explicit stall/resume isolation;
6. explicit disconnect cleanup/rejection and unrelated-path continuity;
7. base-link directional backpressure without reverse-direction blockage;
8. checked due-time overflow with no partial scheduled queue;
9. identical seed/profile/script exact trace and diagnostic digest replay;
10. different-seed trace change and unrelated-path RNG isolation; and
11. independent compilation without OpenMW, platform, socket, or selected
    transport headers and continued reverse test-support dependency rejection.

The implementation demo must show one delayed path beside an immediate path,
certain loss and duplication, reordered delivery, a stalled/resumed path, a
disconnected path with cleared pending work, independent directional
backpressure, and the pinned exact seeded trace/digest.

## Consequences

- Later tests gain deterministic adverse-network controls without making the
  product targets depend on a test facility.
- Two explicit bounded layers make scheduled-fault pressure and receiver
  backpressure independently observable.
- Test-only numeric channels support independent profiles while leaving product
  message/channel design open.
- Nonnegative jitter means configured latency is the minimum rather than a mean;
  fixtures must state the intended range explicitly.
- Disconnect cannot recall bytes already admitted by the underlying link; tests
  choose the injection point intentionally.

## Failure modes and mitigations

- **Unbounded scheduled queue:** require nonzero per-path message/byte limits and
  reserve all copies atomically.
- **Fault traffic perturbs unrelated paths:** derive separate streams by
  direction, channel, and fault dimension and test isolation.
- **Equal-time iteration becomes platform-dependent:** use a global checked
  enqueue/copy ordinal rather than container iteration order.
- **A blocked direction starves its reverse:** pump directions independently and
  retain a blocked due message without busy retry.
- **Delay arithmetic wraps:** validate profile maxima and check the observed
  clock plus sampled delay before committing scheduled work.
- **Test channels become product protocol:** keep their types under test support
  and explicitly deny wire or transport meaning.
- **Disconnect pretends to recall delivered bytes:** discard only scheduled
  wrapper work and document the base-link boundary.

## Review and replacement triggers

Reopen this ADR if:

- Phase 4 cannot compose encoded session traffic through the wrapper without
  changing base-link semantics;
- a required fault cannot be represented by scheduled message delivery plus
  explicit path controls;
- supported compilers disagree on the pinned trace;
- 64 configured paths are insufficient for bounded test scenarios;
- Phase 6 needs to reuse these test-only channel or lifecycle types as product
  interfaces; or
- a real-socket fault layer requires materially different semantics rather than
  an adapter-specific implementation under Phase 6.6.

## Owner approval

Approved by the project owner in the 2026-08-26 working session: Option A for
Decisions 1 through 5.

Approval fixes only the test-support fault wrapper, passive timing/order,
test-only path identity, isolated deterministic random decisions, and bounded
stall/disconnect/backpressure behavior. It does not approve protocol or
production transport channels, socket behavior, session lifecycle, authority,
canonical state, state scope, or gameplay behavior.
