# ADR-0017: Deterministic facilities and harness boundaries

Status: **Accepted**

Date opened: 2026-08-26

Date approved: 2026-08-26

Decision owner: project owner

Needed by: Phase 3 Slice 3.4

## Decision questions

What concrete injected-clock boundary should the engine-independent code use;
which target owns the fixed-tick scheduler and deterministic RNG; how are
labeled random streams identified and restored; what semantics and ownership
should the first in-memory link have; and how can Slice 3.4 prove repeatable
event logs and checksums without deciding Phase 5's canonical checksum early?

ADR-0013 already fixes the 30 Hz tick, four-tick catch-up cap,
`xoshiro256**`/`SplitMix64`, labeled stream isolation, and prohibition on
ambient clocks and standard distributions. This ADR must not reopen those
choices. It selects only the concrete engine-independent seams needed to
implement and test them.

## Decision summary

The project owner approved Option A for Decisions 1 through 5 on 2026-08-26:

1. use a project-owned monotonic nanosecond instant plus an injected clock
   interface and a test-support manual clock;
2. put a passive fixed-tick scheduler in `tes3mp_server_core` that returns at
   most four due tick values and never sleeps, invokes reducers, or skips;
3. put versioned xoshiro streams in `tes3mp_server_core`, derive them from a
   world seed and numeric domain/subject key through a fixed SplitMix64 V1
   contract, and expose checked state snapshot/restore plus unbiased integer
   sampling;
4. keep the first bounded deterministic byte link in `tes3mp_test_support`
   rather than deciding Phase 6's production transport interface; and
5. compare exact typed trace bytes and a test-only FNV-1a diagnostic digest,
   explicitly reserving the canonical checksum choice for Phase 5.5.

Approval authorizes only Slice 3.4 deterministic facilities and reusable
test harness. It would not authorize wall-clock gameplay, game-calendar rules,
threads, reducer behavior, command admission, transport delivery semantics,
network faults, a production transport interface, or a canonical state
checksum algorithm.

## Required boundaries

Every option must preserve ADR-0006, ADR-0013, ADR-0014, and the later phase
gates:

1. Reducers receive logical `ServerTick` and declared deterministic inputs,
   never wall time or measured frame duration.
2. The scheduler runs exactly 30 logical ticks per second, emits sequential
   ticks, executes at most four due ticks per pump, and never skips or merges a
   tick after a stall.
3. Canonical random streams use project-owned xoshiro256** and SplitMix64 code,
   numeric operations with defined unsigned overflow, fixed vectors, labeled
   isolation, no standard distributions, and no OpenMW RNG.
4. Production server-core code cannot depend on test support. Manual clocks,
   scripted links, trace capture, and diagnostic digests may depend on the
   production facilities in the approved direction.
5. Slice 3.4 does not introduce a reducer, simulation state, command admission,
   worker thread, sleep loop, socket, selected transport type, or fault policy.
6. The in-memory link is bounded by both message count and byte count in each
   direction; ownership and failure results are explicit.
7. Phase 3.5 owns latency, loss, jitter, duplication, reordering, stalls, and
   disconnect controls. Phase 5.5 owns the canonical checksum algorithm and
   state-byte coverage.

## Scenarios evaluated

1. A manual clock advances ten minutes in irregular increments while the
   scheduler emits the same 18,000 sequential ticks with no accumulated drift.
2. A host stall makes more than four ticks due; one pump returns four and later
   pumps continue without skipping or changing the fixed step.
3. The logical tick reaches `UINT64_MAX` and cannot advance.
4. A clock observation moves backwards because a bad injected test source is
   used.
5. Two runs with the same world seed and stream key produce identical random
   vectors on every supported compiler.
6. Adding draws to one domain/subject stream does not perturb another stream.
7. Rejection sampling receives a zero exclusive upper bound or a value that
   would expose modulo bias.
8. A saved RNG state is restored during deterministic replay.
9. One direction of an in-memory link reaches its message or aggregate-byte
   capacity while the opposite direction remains usable.
10. A repeated scripted run produces byte-identical event traces and matching
    diagnostic digests without claiming that digest as canonical state.

## Decision 1: injected clock representation and ownership

### Option A: project-owned monotonic nanosecond instant and interface (recommended)

Define `MonotonicInstant` as a fully initialized unsigned 64-bit count of
nanoseconds from an arbitrary process-local monotonic epoch. Define a minimal
`MonotonicClock` interface whose only observation is `now()`. Neither type
exposes a calendar, timezone, system timestamp, sleep, callback, or global
singleton.

`tes3mp_server_core` owns the value/interface because the scheduler consumes it.
`tes3mp_test_support` owns `ManualClock`, which starts from an explicit instant
and advances only through checked nonnegative durations. A production steady-
clock adapter belongs to a later server composition root, not this slice.

Nanoseconds are precise enough for host scheduling but cannot represent 1/30
second exactly. The scheduler therefore compares elapsed nanoseconds against
the rational tick rate; it never repeatedly adds a rounded 33,333,333 ns step.
The process-local epoch is scheduling state, not protocol, persistence, replay,
or gameplay-calendar state.

### Option B: expose `std::chrono::steady_clock` types directly

Inject `steady_clock::time_point` and durations. This is concise and monotonic,
but its duration representation and epoch are implementation-defined and leak
standard-clock types throughout public deterministic APIs and fixtures.

### Option C: template every scheduler on a clock callable

Use a generic `Clock::now()` template with no virtual interface. This removes a
virtual call but spreads implementation into headers, creates distinct
scheduler types per clock, and makes composition/injection vocabulary less
stable for no measured benefit at one observation per pump.

### Option D: use wall-clock Unix timestamps

Use epoch nanoseconds or milliseconds from the system clock. This aids logs but
permits administrator/NTP changes and calendar time to affect scheduling and
confuses host scheduling with the Phase 18 game clock.

## Decision 2: fixed-tick scheduler API and target

### Option A: passive server-core scheduler returning bounded tick batches (recommended)

Place `FixedTickScheduler` in `tes3mp_server_core`. Construct it with an
injected `MonotonicClock`, explicit epoch instant, and explicit next
`ServerTick`. A `pump()` observation returns a fixed-capacity batch containing
zero to four sequential due ticks. Each item carries the logical tick and the
compile-time rational step `1/30`; it never carries measured elapsed time.

Tick N's deadline is derived directly from the epoch using
`ceil(N * 1,000,000,000 / 30)` nanoseconds with checked quotient/remainder
decomposition, not an overflowing multiplication or accumulated rounded
duration. A backwards clock, unrepresentable deadline, or exhausted tick
returns an explicit error and leaves scheduler state unchanged. The scheduler
also exposes due-tick lag as a checked diagnostic count.

The scheduler does not sleep, own a thread, call a reducer, drain commands,
emit metrics, or decide overload shutdown. The future server loop retains
control after every bounded pump.

### Option B: callback-running scheduler

Have `pump(callback)` invoke reducers directly for each due tick. This is
convenient but couples scheduling to mutation and makes exception/error,
cancellation, command-cutoff, and partial-batch behavior part of Slice 3.4.

### Option C: scheduler only in test support

Keep a fake scheduler in `tes3mp_test_support` and implement production
scheduling in Phase 5. This avoids production code now but makes the Phase 3
determinism proof non-reusable and risks two subtly different tick policies.

### Option D: self-running threaded scheduler

Own a thread, sleeps, callbacks, and cancellation. This tests host timing more
realistically but imports nondeterministic scheduling and lifecycle policy
before the dedicated-server composition root exists.

## Decision 3: deterministic RNG streams, keys, and state

### Option A: numeric labeled streams with versioned V1 derivation (recommended)

Place project-owned `SplitMix64` and `Xoshiro256StarStar` value classes in
`tes3mp_server_core`. A `RandomStreamKey` contains a nonzero unsigned 64-bit
domain ID and an unsigned 64-bit subject ID; zero subject is the domain-wide
stream. Domain IDs are stable registered values assigned by future owning
features, never hashes of display strings or enum ordinals that may reorder.

Version 1 derives a stream as follows, using unsigned 64-bit modulo arithmetic:

1. `mix64(z)` applies the SplitMix64 finalizer constants
   `0xbf58476d1ce4e5b9` and `0x94d049bb133111eb` with shifts 30, 27, and 31.
2. Start `seed = mix64(world_seed ^ 0x544553334d505231)`, where the constant is
   the stable ASCII V1 domain separator `TES3MPR1`.
3. Set `seed = mix64(seed ^ domain_id)`, then
   `seed = mix64(seed ^ subject_id)`.
4. Initialize SplitMix64 state with `seed`; four successive standard
   SplitMix64 outputs initialize xoshiro state words 0 through 3.

The stream exposes `nextU64()`, `uniformBelow(exclusive_upper_bound)` using
project-owned rejection sampling, and explicit `RandomStateV1` snapshot/
restore. A zero bound and all-zero restored xoshiro state reject without
consuming or changing state. No floating distributions, implicit global stream,
or seed-from-clock API exists. Algorithm version, world seed, key, and restored
state are explicit replay inputs.

This is small, stable, and isolates unrelated domains. Numeric IDs require a
future registry, but avoid choosing string normalization and hashing now.

### Option B: bounded string labels

Derive streams from normalized ASCII domain and subject labels. Diagnostics are
readable, but the project must choose encoding, case, length, normalization,
hashing, and collision behavior for every random draw boundary.

### Option C: one shared xoshiro stream

Use the approved algorithm but share one stream across all domains. This is
simple, yet adding an unrelated random draw shifts every later result and
violates ADR-0013's isolation requirement.

### Option D: caller-provided raw 256-bit xoshiro seeds

Let each domain create four seed words. This keeps the generator small but
duplicates derivation, permits all-zero state, and makes replay evidence unable
to explain how streams relate to the world seed and subject.

## Decision 4: in-memory link ownership and semantics

### Option A: bounded deterministic test-support byte duplex (recommended)

Implement `InMemoryDuplexLink` in `tes3mp_test_support`, not a production
target. It owns two independent FIFO directions of owned byte messages. Each
direction has explicit nonzero maximum queued-message and aggregate-byte
budgets. Construction rejects impossible budgets; `send` returns `Accepted`,
`WouldBlock`, `MessageTooLarge`, or `Closed`; `receive` moves one complete
message or reports empty; and close state is explicit per sending direction.

The link is deliberately single-threaded and has no clock, sleep, callback,
packet fragmentation, channel priority, authentication, retransmission, or
socket behavior. It preserves message boundaries and never partially enqueues.
Phase 3.5 can put a deterministic fault scheduler around its directions. Phase
4 can exchange encoded bytes through it. Phase 6 still owns the production
transport interface and selected-library adapter.

### Option B: define the production transport interface now

Put an endpoint abstraction in `tes3mp_transport` and make the in-memory link
implement it. This maximizes reuse but prematurely selects lifecycle, channel,
backpressure, cancellation, threading, and delivery semantics owned by Phase 6.

### Option C: unbounded in-memory queues

Use two `deque<vector<byte>>` values with infallible send. This is concise but
hides backpressure and lets tests pass behavior that the bounded product cannot
support.

### Option D: localhost sockets

Exercise operating-system networking from the start. This adds timing,
platform, port, and cleanup variability and does not provide the deterministic
in-memory boundary required by Phase 4.

## Decision 5: deterministic trace and checksum evidence

### Option A: exact trace bytes plus test-only diagnostic digest (recommended)

Add a reusable scripted harness in `tes3mp_test_support` that composes a manual
clock, production scheduler/RNG, and the in-memory link. It records typed events
to an explicit little-endian test format with stable tags and field order. Exact
event sequence and bytes are the primary equality evidence.

Also compute `TestTraceDigestV1` with fixed 64-bit FNV-1a solely as a compact
test diagnostic. Its type and API are under `TES3MP::TestSupport`, its name and
documentation say non-canonical, and canonical production code cannot link it.
Phase 5.5 remains free to select a stronger versioned canonical checksum over
the approved canonical state bytes.

The Slice 3.4 demo runs the same script twice and compares events, bytes, and
diagnostic digest; changes only an unrelated labeled RNG stream and proves the
main stream trace remains unchanged; and demonstrates bounded catch-up and
link backpressure.

### Option B: select the canonical checksum now

Choose a production hash and state coverage in server core. This would satisfy
the word checksum directly but preempts Phase 5.5 before canonical state and
change records exist.

### Option C: use `std::hash` for the trace

This is convenient but not a stable cross-platform byte contract and can vary
by standard-library implementation or process policy.

### Option D: compare trace values only

Skip any digest. Exact equality is sounder than a hash, but it does not meet
Slice 3.4's explicit checksum demonstration and makes CI mismatch summaries
less convenient.

## Proposed acceptance tests

If Option A is approved for all decisions, Slice 3.4 should add tests named for
these contracts:

1. `manual_clock_advances_checked_monotonic_instants_only`
2. `thirty_hz_scheduler_has_no_accumulated_rounding_drift`
3. `scheduler_pump_executes_at_most_four_due_ticks`
4. `stall_never_produces_variable_delta_or_tick_reordering`
5. `scheduler_rejects_backwards_clock_deadline_overflow_and_tick_exhaustion`
6. `splitmix64_matches_version_one_test_vectors`
7. `xoshiro256_star_star_matches_version_one_test_vectors`
8. `labeled_rng_streams_are_isolated_from_unrelated_draws`
9. `rng_snapshot_restore_reproduces_future_values`
10. `uniform_below_is_unbiased_by_construction_and_zero_bound_does_not_consume`
11. `in_memory_link_enforces_independent_message_and_byte_budgets`
12. `in_memory_link_preserves_fifo_message_boundaries_and_explicit_close`
13. `same_script_seed_and_clock_produce_identical_trace_bytes_and_digest`
14. `test_trace_digest_is_not_a_server_core_or_protocol_dependency`
15. `deterministic_facilities_compile_without_openmw_transport_runtime_or_platform_headers`

The implementation demo must show the long-run tick count, four-tick stall
batches, retained RNG vectors/state, isolated labeled streams, directional link
backpressure, and two byte-identical scripted traces with matching diagnostic
digests.

## Consequences of the recommendation

- Production server core gains small passive deterministic facilities without a
  server loop, reducer, or thread.
- Standard-library clocks remain confined to a future adapter; deterministic
  APIs use stable project-owned values.
- Numeric stream keys make RNG isolation and replay inputs explicit while a
  later registry owns domain allocation.
- Test support supplies the manual environment and bounded link without
  becoming a production dependency or deciding Phase 6 transport semantics.
- Exact trace bytes provide strong evidence; the small digest improves failure
  reporting without becoming canonical state policy.

## Failure modes and mitigations

- **Nanoseconds become game time:** name the value monotonic, omit calendar
  conversion, and keep Phase 18 game-clock values separate.
- **Rounded 33 ms accumulates drift:** derive every deadline from epoch/tick
  rational arithmetic and test ten minutes plus large tick values.
- **One pump monopolizes work:** make the returned batch capacity four and leave
  scheduler state at the next sequential due tick.
- **Clock rewind partially advances:** validate the observation before changing
  scheduler state.
- **RNG implementation drifts:** pin SplitMix/xoshiro/derivation vectors and
  version state explicitly.
- **Modulo bias enters bounded sampling:** own rejection sampling and test
  rejection/zero-bound state consumption.
- **Stream labels collide through ad hoc hashes:** use numeric registered domain
  and subject fields with no string hash in this layer.
- **Test link becomes production transport:** keep it in test support and forbid
  reverse production dependencies.
- **Harness digest becomes persistence/protocol evidence:** name it test-only,
  compare exact bytes, and retain Phase 5.5 as the canonical checksum gate.

## Review and replacement triggers

Reopen this ADR if:

- supported hosts cannot supply a safe monotonic observation convertible to the
  approved instant range;
- the passive scheduler cannot compose with Phase 5's single-writer loop;
- cross-platform vectors disagree for the specified unsigned operations;
- a canonical feature cannot obtain a stable numeric stream domain/subject;
- RNG state must cross a different replay or persistence version boundary;
- Phase 3.5 faults cannot wrap the proposed link without changing its base
  semantics;
- Phase 4 requires the test link to claim production transport behavior; or
- test trace evidence is used as a canonical state checksum.

## Owner approval

Approved by the project owner in the 2026-08-26 working session: Option A for
Decisions 1 through 5.

Approval fixes only the project-owned monotonic clock seam, passive bounded
scheduler, numeric-keyed versioned RNG streams, test-support-only bounded byte
link, and exact test trace plus non-canonical diagnostic digest. Gameplay,
authority, canonical state and checksums, reducer behavior, threading,
production transport semantics, network faults, and wall-clock game rules
remain gated by their owning phases.
