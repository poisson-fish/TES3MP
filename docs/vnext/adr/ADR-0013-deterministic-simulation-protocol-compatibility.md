# ADR-0013: Deterministic simulation and protocol compatibility policy

Status: **Accepted**

Date opened: 2026-08-26

Date approved: 2026-08-26

Decision owner: project owner

Needed by: Phase 2

## Decision questions

What fixed tick and overload model drives canonical simulation; which numeric
representations are allowed in canonical state; how are simultaneous commands
totally ordered; how are time, randomness, checksums, and replay inputs made
deterministic; and which protocol versions/capabilities interoperate?

These choices are required before Phase 3 defines ticks, numeric primitives,
the clock/RNG/scheduler harness, and before Phases 4–5 harden negotiation,
envelopes, reducers, ordering, checksums, and replay-facing change records.

ADR-0013 is the next unused ADR identifier. ADR-0008 through ADR-0012 are
already reserved by the implementation plan for later phase decisions.

## Decision summary

The project owner approved Option A for Decisions 1 through 5 on 2026-08-26:

1. use a 30 Hz fixed logical tick with at most four due ticks per scheduler
   pump and no variable reducer delta or silent tick skipping;
2. use checked integer/fixed-point canonical values with `1/1024` OpenMW-world-
   unit position quanta and 32-bit turn orientation;
3. have the single writer assign command eligibility and monotonic ingress
   ordinals, without client timestamps affecting canonical order;
4. inject logical time and use versioned, labeled deterministic random streams
   plus a stable canonical byte contract; and
5. support the current and immediately previous protocol minor within one major,
   with bounded required/optional capability negotiation.

This approval authorizes the owning phases to implement these architecture and
interoperability rules after the Phase 2 exit gate and Phase 3 kickoff are
explicitly approved. It does not select gameplay-domain values such as movement
speed, collision, interest, correction thresholds, or world-time evolution.

## Accepted constraints

Every option must preserve the vNext README and accepted ADR-0003 through
ADR-0007:

1. The server core is a single canonical writer with no OpenMW, renderer,
   platform, socket, script-runtime, or database dependency.
2. The same initial state, accepted ordered command stream, negotiated rules,
   deterministic seed, and tick sequence produce the same canonical changes and
   checksum on every supported server platform.
3. Wall-clock time, network callback timing, hash-container iteration, worker
   scheduling, filesystem enumeration, locale, and client timestamps are not
   canonical reducer inputs.
4. Clocks and random sources are injected. Fault profiles and fake clients are
   seedable and replayable.
5. Stable IDs, ticks, sequences, revisions, command IDs, and authority epochs
   use strong types and checked arithmetic. Wraparound never silently restores
   stale authority or ordering.
6. Latest-wins samples and reliable apply-once commands retain separate
   ordering, queue, acknowledgement, and replay semantics.
7. Protocol versions are semantic vNext product metadata, not OpenMW versions,
   Git commits, schema-generator versions, or transport-library versions.
8. Unknown optional capabilities are ignored safely; an unknown or unsupported
   required capability causes a clear pre-mutation rejection. Capabilities do
   not activate merely because an optional schema field is present.
9. Stable protocol/schema/capability identifiers are never reused for a new
   meaning, and TES3MP 0.8.x compatibility remains out of scope.

## Representative scenarios

1. **Normal scheduling:** a server runs for ten minutes while the host clock
   has small jitter. Exactly 18,000 logical ticks execute at 30 Hz, independent
   of how often the outer process loop wakes.
2. **Host stall:** the process pauses long enough for many ticks to become due.
   Catch-up work per scheduler pump remains bounded; ticks use the fixed step,
   never a large variable delta, and canonical ordering does not depend on wall
   time.
3. **Cross-platform replay:** Windows/MSVC, Linux/GCC/Clang, and macOS/
   AppleClang consume the same trace and seed. Canonical values, committed event
   bytes, and declared checksums match exactly.
4. **Boundary conversion:** an OpenMW float transform is non-finite, outside the
   declared range, or halfway between two canonical quanta. The adapter applies
   the approved rejection/rounding rule before a command enters the session.
5. **Two-client contention:** commands from two clients become eligible for one
   tick. The server-owned admission order decides the winner and is recorded;
   client clocks and thread races cannot break the tie inside the reducer.
6. **Duplicate/stale input:** a duplicate command ID, old source sequence,
   stale revision, old session generation, or old authority epoch arrives. It
   cannot re-enter the total order as a new mutation.
7. **Random outcome:** a domain consumes random values. Replaying the recorded
   seed and stream identity gives the same results, and adding presentation
   randomness cannot perturb canonical gameplay streams.
8. **Compatible peer:** current and immediately previous protocol minors share
   one major and negotiate the highest common minor plus an immutable capability
   intersection.
9. **Incompatible peer:** major versions do not overlap, a required capability
   is unsupported, or no minor overlaps. The handshake returns a stable bounded
   incompatibility reason before authentication or canonical mutation.
10. **Unknown optional capability:** one peer advertises an unknown optional ID.
    The other ignores it, does not enable it, and continues if all required
    capabilities are satisfied.
11. **Resume:** a reconnect renegotiates and must reproduce a compatible
    version/capability/content context before its single-use resume token can
    restore the application session.
12. **Counter exhaustion:** a tick, revision, sequence, admission ordinal, or
    epoch approaches its maximum. Checked arithmetic fails closed with an
    explicit operational error; it never wraps.

## Decision 1: canonical tick and scheduler-overload policy

### Option A: 30 Hz fixed tick with bounded catch-up (approved)

Use an unsigned 64-bit logical `ServerTick` at exactly 30 ticks per second.
Canonical reducers receive the tick and a fixed rational step of `1/30` second;
they never receive measured frame duration. Scheduler deadlines are computed
from an epoch and tick count using integer/rational arithmetic so repeated
`33 ms` rounding cannot accumulate drift.

An outer scheduler pump executes at most four due canonical ticks before
returning to transport, cancellation, metrics, and operating-system work. If
more ticks remain due, the next pump continues with the next sequential tick.
No canonical tick is merged, renumbered, or evaluated with a variable delta.
Command, event, and sink queues remain independently bounded, and tick lag is
observable. Phase 5 may choose an operator shutdown/degraded-admission threshold
from measured evidence, but cannot silently skip canonical time to catch up.

This is a balance between movement responsiveness, friends-server CPU cost, and
future combat timing. A 64-bit counter at 30 Hz does not practically exhaust,
but checked increment and an explicit terminal failure remain required.

### Option B: 20 Hz fixed tick

Use a 50 ms step. This lowers CPU, snapshot, and command-processing pressure and
has an exact integer duration, but increases input-to-canonical latency and
requires more client interpolation/prediction for movement and later combat.

### Option C: 60 Hz fixed tick

Use a `1/60` second rational step. This reduces tick quantization and can feel
more responsive, but doubles baseline reducer, validation, snapshot-selection,
and test workload before representative load evidence exists.

### Option D: variable timestep from elapsed host time

Feed measured elapsed time to reducers. This follows OpenMW's presentation loop
and naturally absorbs stalls, but floating elapsed time, host jitter, and
scheduler behavior become canonical inputs. Cross-platform replay and exact
checksums would be much harder to guarantee.

## Decision 2: canonical numeric representation

### Option A: checked fixed-point/integer canonical values (approved)

Canonical network-visible and reducer state uses declared integer units:

- root positions use signed 64-bit values in `1/1024` OpenMW-world-unit quanta,
  with canonical cell identity represented separately;
- linear velocity/delta uses signed 64-bit position quanta per server tick;
- orientation uses unsigned 32-bit turn units where `2^32` units is one full
  turn; domain rules later define permitted axes and pitch limits;
- ticks, sequences, revisions, admission ordinals, and authority epochs use
  nonzero or explicitly optional unsigned 64-bit strong types; and
- command/session/entity identities use their separately declared stable widths
  and never share primitive aliases accidentally.

Reducers use checked integer operations and explicit quotient/remainder rules.
Overflow, invalid sentinel values, and out-of-domain conversion fail before
mutation. Float-to-canonical conversion occurs only at adapter/protocol
boundaries, rejects non-finite/out-of-range input, and rounds to nearest with
ties to the even quantum. Canonical-to-float conversion is presentation-only.

This makes canonical equality, ordering, serialization, checksums, and replay
bit-exact across compilers. It adds conversion and checked-math work and couples
velocity units to the approved tick rate, so a tick-rate or quantum change is a
protocol/canonical-state migration rather than a tuning switch.

### Option B: normalized IEEE-754 binary32 canonical values

Store positions, velocities, and rotations as floats after finite/range/
negative-zero/NaN normalization. This maps directly to OpenMW and minimizes
conversion, but expression contraction, intermediate precision, math-library
functions, denormals, and compiler flags can produce platform differences.
Checksums would need a canonical bit policy and cross-platform proof for every
reducer operation.

### Option C: IEEE-754 binary64 canonical values

Use doubles to reduce rounding error and keep familiar units. This does not
remove platform/compiler/libm determinism risks, increases state/wire size, and
still requires canonical NaN, zero, infinity, rounding, and serialization rules.

### Option D: tolerance-based equivalence

Allow platform results within an epsilon and exclude exact numeric state from
checksums. This is simple for simulation code but cannot provide identical
replay, revisions, or unambiguous contention at tolerance boundaries.

## Decision 3: command admission and total ordering

### Option A: writer-owned eligible tick and recorded ingress ordinal (approved)

Decoded session commands enter a bounded admission queue but receive no
canonical order on network/callback threads. At a declared tick cutoff, the
single writer drains eligible commands and assigns each a checked unsigned
64-bit `IngressOrdinal` in the serialized order observed by that writer.

Every accepted command record includes:

- server-assigned eligible tick;
- ingress ordinal;
- stable source/session identity and generation;
- per-source sequence and command ID;
- expected entity revision and authority epoch where applicable; and
- negotiated rules/capability context identity.

Reducers process `(eligible_tick, ingress_ordinal)` order. A client-requested
tick or timestamp is an observation only and cannot backdate, reorder, or win a
tie. Commands admitted after the cutoff become eligible no earlier than the
next tick. Duplicates and stale sequence/revision/generation/epoch inputs are
removed or rejected before mutation while preserving an auditable result.

Replay records the accepted order, including ordinals, rather than attempting
to reproduce operating-system packet arrival. Scripts and other future sources
submit through the same queue; their owning ADR/GDR must define when they become
eligible, and they receive no direct in-tick mutation callback.

This reflects actual server-observed order without biased stable-player-ID
sorting. It requires one serialized admission boundary and makes an ordinal
part of replay evidence.

### Option B: stable source-ID sorting each tick

Sort by tick, source identity, source sequence, then command ID. This is
independent of callback arrival order, but the same lower source identity wins
every same-tick contention and commands that arrived materially earlier can be
reordered behind another source.

### Option C: deterministic rotating round-robin by source

Rotate the first source each tick and drain one command per source per round.
This reduces stable-ID bias and bounds one peer's dominance, but adds scheduler
policy to gameplay contention and can reorder commands relative to server
receipt in surprising ways. Per-peer rate budgets can address floods without
making round-robin the canonical tie-breaker.

### Option D: client timestamp/order

Use sender ticks or clocks to order simultaneous actions. This can compensate
for latency but gives untrusted clocks gameplay authority and makes replay,
clock skew, manipulation, and cross-client ties ambiguous.

## Decision 4: deterministic time, randomness, iteration, and checksums

### Option A: injected logical inputs and versioned canonical encoding (approved)

The engine-independent core receives only injected scheduling observations and
logical `ServerTick`; it never calls a wall clock from a reducer. Canonical
randomness uses a project-owned, versioned generator contract with fixed test
vectors: `xoshiro256**`, seeded from a recorded 64-bit world seed through
`SplitMix64`, with project-owned rejection sampling rather than standard-library
distributions.

Random streams are labeled and derived by stable domain and subject identifiers
so presentation randomness or an unrelated domain cannot perturb another
canonical stream. Stream algorithm/version, seed derivation, and consumption
are replay inputs. A change requires a rules/replay version change and migration
decision; it is not a silent implementation update.

Canonical collections serialize in stable ID order using explicit field order,
width, signed representation, and byte order. Checksums cover the declared
canonical state, tick, revisions, rules version, and deterministic RNG stream
state; they exclude transport/session buffers, wall time, credentials, metrics,
unordered container layout, and presentation-only samples. Phase 5.5 selects a
versioned checksum algorithm behind this canonical byte contract and retains
full byte/trace comparison in tests so checksum collision is never the only
evidence.

This provides cross-platform replay without importing OpenMW's global RNG or
standard distributions. The explicit stream/version machinery is more work
than a shared generator but prevents unrelated features from changing every
later random result.

### Option B: one shared standard-library PRNG stream

Inject one `std::mt19937_64` or `std::minstd_rand` and use standard distributions.
The engine algorithm is stable, but standard distribution output is not
portable across implementations, and one added random draw shifts every later
domain result.

### Option C: recorded random outcomes

Use convenient platform RNGs but record every result in replay. This can replay
an existing run, yet fresh deterministic simulations and property tests remain
platform-dependent, logs grow with every draw, and missed calls silently escape
the trace.

### Option D: platform/game engine time and RNG

Reuse OpenMW clocks and RNG state. This minimizes adapter conversion but couples
the server core to engine execution, rendering timing, save format, and platform
behavior, directly violating the independent-core boundary.

## Decision 5: protocol version and capability compatibility

### Option A: one major with a current-plus-previous-minor window (approved)

Use a semantic `ProtocolVersion { major: u16, minor: u16 }`; product patch/build
versions are diagnostic metadata and do not change wire identity. Released
clients and servers support the current minor and immediately previous minor of
one major. They advertise a bounded inclusive minor range for that major and
select the highest common minor. No cross-major compatibility is attempted.

Before the first stable public protocol release, development builds may support
only their exact minor, but the Phase 4 implementation and golden fixtures must
exercise current/previous-minor negotiation so the release window is real rather
than aspirational.

Each hello carries bounded, sorted, duplicate-free stable capability IDs split
into supported-optional and required sets. Negotiation succeeds only if every
required capability is supported by the other side; the immutable negotiated
set is the supported intersection permitted by the selected minor. Unknown
optional IDs are ignored and never enabled. Unknown/unsupported required IDs,
no minor overlap, and major mismatch receive stable bounded rejection categories
when it is safe to respond.

A capability is enabled only by the negotiated set, never field presence,
client platform labels, engine version, or optimistic fallback. Resume tokens
bind to selected version, capability set, rules version, content context, and
session generation. Changing required semantics needs a new capability or
protocol major/minor according to ADR-0004; stable IDs are never reused.

This gives small rolling-update tolerance while bounding compatibility tests to
two minors. It carries more encoder/decoder/session testing than exact-version
only support.

### Option B: exact major/minor only

Require identical protocol versions and use capabilities only for optional
platform features such as VR pose. This is simplest for friends who update
together and has the smallest test matrix, but every protocol minor update
forces coordinated client/server deployment and makes ADR-0004's additive
evolution support less useful operationally.

### Option C: broad minor range within a major

Support every prior minor of the current major. This maximizes compatibility
but grows schema, behavior, security, and test burden without a retirement
bound, eventually recreating compatibility constraints the clean break avoided.

### Option D: compatibility follows OpenMW version or source commit

Allow peers when their engine release/commit matches. Engine identity does not
describe protocol semantics, server-core rules, capabilities, content, or
security behavior and would couple independent release cadences.

## Approved policy summary

This table records the approved result for all five decisions.

| Area | Recommended contract |
|---|---|
| Tick | Logical unsigned 64-bit tick at 30 Hz; rational fixed step; maximum four due ticks per scheduler pump; no variable reducer delta or silent tick skip |
| Canonical numerics | Signed 64-bit `1/1024` OpenMW-unit position; signed 64-bit position quanta/tick velocity; 32-bit turn orientation; checked arithmetic and ties-to-even boundary conversion |
| Ordering | Single writer assigns eligible tick and monotonic ingress ordinal; reducers use that recorded total order; client time never orders commands |
| Deterministic inputs | Injected clock/scheduler, versioned xoshiro256**/SplitMix64 labeled streams, stable collection ordering, versioned canonical state bytes |
| Compatibility | One `u16` major/minor; released current plus previous minor; highest overlap; bounded required/optional capability negotiation; immutable negotiated set |

## Approved acceptance tests and evidence

The owning phases must add these tests:

1. `thirty_hz_scheduler_has_no_accumulated_rounding_drift`
2. `scheduler_pump_executes_at_most_four_due_ticks`
3. `stall_never_produces_variable_delta_or_tick_reordering`
4. `tick_revision_sequence_ordinal_and_epoch_overflow_fail_closed`
5. `canonical_fixed_point_boundaries_round_ties_to_even`
6. `non_finite_out_of_range_and_overflow_conversion_reject_before_mutation`
7. `canonical_reducer_trace_matches_across_supported_compilers`
8. `writer_assigns_next_tick_and_monotonic_ingress_order`
9. `client_timestamp_cannot_backdate_or_reorder_command`
10. `duplicate_and_stale_commands_do_not_reenter_total_order`
11. `xoshiro_and_splitmix_test_vectors_match_on_every_platform`
12. `labeled_rng_streams_are_isolated_from_unrelated_draws`
13. `canonical_encoding_is_independent_of_container_insertion_order`
14. `same_trace_seed_rules_and_capabilities_match_bytes_and_checksum`
15. `current_and_previous_minor_select_highest_overlap`
16. `major_mismatch_and_no_minor_overlap_reject_before_authentication`
17. `unknown_optional_capability_is_ignored_and_not_enabled`
18. `unknown_or_unsupported_required_capability_rejects`
19. `negotiated_capability_set_is_immutable_for_connection_lifetime`
20. `resume_rejects_version_capability_rules_or_content_context_mismatch`
21. `protocol_version_is_independent_of_openmw_and_build_identity`

The Phase 5 demo must replay one two-client command/fault trace on each supported
server toolchain and compare canonical change bytes and checksums. It must also
show bounded catch-up after an injected stall and a same-tick contention whose
recorded ingress ordinals explain the result. Phase 4's demo must show current/
previous-minor success plus major, minor-range, required-capability, and legacy
peer rejection.

## Consequences of the approved decision

- Canonical simulation units differ from OpenMW presentation floats; the adapter
  owns all conversion and its rounding/rejection tests.
- Tick rate and numeric quanta become versioned state/protocol rules rather than
  operator performance tuning.
- Production event arrival remains inherently network-dependent, but the writer
  turns observed admission into a complete recorded order that replay can
  reproduce exactly.
- Deterministic RNG and canonical encoding are project-owned contracts; standard
  distributions, unordered iteration, and ambient clocks are prohibited in
  reducers.
- Released compatibility work is bounded to two minors within one major. A
  protocol major is a deliberate clean break, never inferred from OpenMW.
- Capability negotiation permits optional desktop/VR presentation features
  without making platform identity a gameplay branch.

## Failure modes and mitigations

- **30 Hz represented as rounded milliseconds:** compute epoch deadlines and
  fixed reducer steps as rational tick counts; test long-run drift.
- **Catch-up monopolizes process work:** cap ticks per pump at four and keep
  ingress/sink queues bounded and observable.
- **Fixed-point overflow:** validate ranges, use checked intermediates, and fail
  before canonical mutation; never saturate silently.
- **Adapter rounding differs:** specify ties-to-even and test exact halfway,
  negative, range, and non-finite cases on every platform.
- **Network threads assign order:** restrict ordinal assignment to the single
  writer and retain the accepted record in replay evidence.
- **Counter wrap:** checked increment and terminal operational failure long
  before reuse; no modular comparison for canonical identities/revisions.
- **RNG algorithm/library drift:** own the algorithm contract, pin test vectors,
  avoid standard distributions, and version seed derivation/stream rules.
- **Hash iteration changes output:** serialize sorted stable IDs and explicit
  fields, then compare canonical bytes as well as hashes.
- **Capability smuggling through fields:** handlers consult the immutable
  negotiated set; field presence alone never enables behavior.
- **Compatibility window grows indefinitely:** require a new owner-approved
  policy before supporting more than current and previous minor.
- **Resume crosses negotiated context:** bind and compare version, capability,
  rules, content, and generation before session restoration.

## Review and replacement triggers

Reopen this ADR if:

- measured representative load shows 30 Hz cannot meet the later tick budget or
  gameplay review requires a different rate;
- four-tick catch-up cannot preserve responsiveness or bounded operations;
- `1/1024` OpenMW-unit position, per-tick velocity, or 32-bit turn units cannot
  represent an approved domain safely;
- a reducer requires floating point, platform math, wall time, unordered
  iteration, or nondeterministic parallel reduction;
- server-observed ingress order creates unacceptable fairness/gameplay behavior;
- the RNG algorithm/seed derivation or canonical encoding must change;
- more than two protocol minors, multiple majors, rolling mixed-version state,
  or a different release window becomes a product requirement;
- a capability needs renegotiation during a live connection; or
- cross-platform replay evidence differs despite identical declared inputs.

## Owner approval

Approved by the project owner in the 2026-08-26 working session: Option A for
Decisions 1 through 5.

Approval fixes architecture and interoperability rules, not domain gameplay
values such as movement speed, collision, correction thresholds, interest,
combat timing, or world-time evolution. Those remain gated by their GDRs.
