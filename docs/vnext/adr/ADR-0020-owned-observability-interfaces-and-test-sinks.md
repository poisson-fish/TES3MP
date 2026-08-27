# ADR-0020: Owned observability interfaces and test sinks

Status: **Proposed**

Date opened: 2026-08-27

Date approved: pending

Decision owner: project owner

Needed by: Phase 3 Slice 3.7

## Decision questions

Which production target should own the first metrics and structured-event
interfaces; how should callers receive and report to sinks; which metric and
event shapes are safe and stable; what privacy/cardinality rules apply; and how
should bounded test sinks expose ordering, overflow, and deterministic evidence?

This decision is required before Slice 3.7 adds a public observability header,
sink contract, test recorder, or target dependency. It defines an architectural
and diagnostic boundary, but does not define gameplay behavior, canonical state,
protocol fields, production logging/export backends, administration, persistence,
or a thread/queue model.

## Existing constraints

The choice must preserve ADR-0003, ADR-0013, and ADR-0014:

1. Metrics and structured events are evidence, never authority or a canonical
   mutation path.
2. Sinks cannot receive a mutable canonical-state view, packet buffer, reusable
   credential, opaque authentication input, or unfiltered user data.
3. A slow or failed sink cannot make canonical behavior nondeterministic, block
   the canonical writer indefinitely, or create unbounded retained work.
4. Protocol, server-core, client-session, and transport code remain independent
   of OpenMW, operating-system logging APIs, exporter SDKs, and the selected
   transport library.
5. Production targets cannot depend on `tes3mp_test_support`; test support may
   implement production interfaces.
6. Wall-clock timestamps, exporter delivery, formatting, files, consoles,
   remote collection, and operational retention are composition/backend
   concerns, not server-core state.
7. Phase 5's committed-change sinks, Phase 6's transport telemetry, and Phase
   21's audit/admin surface keep their own semantics. This scaffold must not
   impersonate persistence, replay, audit, or network protocol.

## Scenarios evaluated

1. A deterministic core operation emits one counter observation and one
   structured rejection event; a test asserts their exact typed values and
   order without installing a production logger.
2. A composition root intentionally chooses no output backend. The absence is
   explicit and core code does not branch on a global singleton.
3. A test sink reaches capacity while hostile input continues producing
   observations. Memory stays bounded, overflow is visible, and canonical
   behavior is unchanged.
4. A future feature needs a new metric or event field. Its owning slice adds a
   reviewed typed category rather than an arbitrary string key or field map.
5. A malicious input contains a password/token canary or a very long player
   string. No observation API can accept the raw bytes or text by convenience.
6. Two identical deterministic runs emit the same semantic observation sequence
   even when host wall-clock time differs.
7. A future exporter is slow, throws internally, or loses connectivity. Its
   adapter owns isolation and buffering; the core-facing contract neither waits
   nor retries and the canonical result does not change.
8. A metric consumer attempts to attach entity/session identifiers as labels.
   The fixed low-cardinality dimension API rejects that shape.

## Decision 1: interface ownership and target topology

### Option A: server-core-owned ports plus test-support recorders (recommended)

Place the initial metrics/event value types and sink interfaces in
`tes3mp_server_core`. Place bounded recording implementations in
`tes3mp_test_support`. Keep the ADR-0014 target graph unchanged: server core
does not gain a dependency, while test support already depends on server core.

This matches Slice 3.7's completion evidence—core tests can assert observations
without a backend—without pretending that future transport/client telemetry has
identical domain semantics. Phase 6 may add transport-owned categories behind
the same design principles without moving GameNetworkingSockets types into the
core.

### Option B: add a shared `tes3mp_observability` production target

Create a new dependency-free library used by protocol, transport, server core,
and client session. This centralizes vocabulary, but reopens ADR-0014's exact
target graph and risks a cross-domain taxonomy that every component must depend
on before shared requirements exist.

### Option C: place the common API in `tes3mp_protocol`

All current production targets can already reach protocol types. This avoids a
new target, but metrics and log events are not wire/schema concepts and would
blur the protocol boundary.

### Option D: adopt a logging/metrics library directly

Use an exporter SDK or logging framework as the core API. This supplies mature
backends but leaks dependency types and backend policy into deterministic code,
contradicting the owned-boundary requirement.

## Decision 2: injection, lifetime, and failure contract

### Option A: explicit injected sink references with non-blocking `noexcept` attempts (recommended)

Define separate metrics and structured-event sink interfaces, grouped in a
small non-owning observability bundle. Construction receives explicit sink
references; composition that wants no output supplies explicit no-op sinks.
There is no process-global registry, hidden default, ownership transfer, or
`shared_ptr` lifecycle.

Emission is a direct, bounded `noexcept` attempt returning `Accepted` or
`Dropped`. Callers do not wait, retry, allocate an unbounded queue, or change a
canonical result because observation failed. A later production adapter may
enqueue asynchronously, but must satisfy this core-facing contract and prove
its own bounded failure behavior.

### Option B: global registry/service locator

Callers discover the active sinks globally. This shortens constructors but
hides dependencies, complicates parallel tests, and permits runtime replacement
to perturb otherwise deterministic code.

### Option C: owning callbacks (`std::function`)

Pass callable objects into each component. This is flexible but obscures metric
versus event contracts, permits capture/lifetime mistakes and allocations, and
makes backend capabilities hard to inspect mechanically.

### Option D: core-owned asynchronous dispatcher

Put a thread and queue behind the initial interface. This isolates slow backends
at runtime, but prematurely chooses threading, shutdown, ordering, allocation,
and backpressure behavior before a production composition root exists.

## Decision 3: metric model and cardinality

### Option A: closed typed metric keys, operations, units, and enum dimensions (recommended)

Use scoped project-owned metric keys whose definition fixes kind and unit.
Support three integer-only operations: monotonic counter addition, signed gauge
set, and non-negative distribution observation. Each observation carries only
a small fixed-capacity list of approved enum key/value dimensions. Metric
dimensions cannot contain free-form strings, entity/player/session IDs, packet
bytes, pointers, or backend-native labels.

Real metric keys land with the behavior they describe. Slice 3.7 adds only the
minimal contract vocabulary and test fixtures needed to prove dispatch and
bounds; it does not invent future gameplay or transport categories.

### Option B: arbitrary string name plus string label map

This is familiar to common exporters and easy to extend, but allows typos,
unbounded allocation, secret leakage, and hostile high-cardinality labels in
the core API.

### Option C: numeric key and opaque integer fields without a registry

This is compact, but loses compile-time kind/unit meaning and makes accidental
key reuse or incompatible interpretation difficult to detect.

### Option D: exporter-native instruments

Expose counters/histograms from a selected monitoring SDK. This reduces adapter
work but couples core compilation and lifetime to an operational backend.

## Decision 4: structured-event shape and privacy

### Option A: closed event kinds with typed fields and backend-rendered text (recommended)

Represent each event as a closed project-owned kind, severity, optional semantic
tick, and a fixed typed payload for that kind. Payloads may contain approved
semantic value types and closed reason/category enums, but never arbitrary text,
byte spans, exception/library strings, credentials, addresses, or mutable state
references. Backends map the kind and typed values to human-readable text.

Stable entity/session identifiers may appear only in event kinds whose owning
slice explicitly justifies their diagnostic need and visibility. They never
become metric dimensions. Unknown or user-provided text is categorized and
redacted at its ingress boundary rather than truncated inside the sink.

### Option B: format string plus arguments

This is convenient for developers, but message structure and redaction depend
on call-site discipline and tests must parse prose to assert behavior.

### Option C: generic JSON/object field map

This is readily exportable, but introduces strings, allocation, schema drift,
duplicate keys, and runtime type validation inside the deterministic boundary.

### Option D: plain enum with no fields

This is safest and smallest, but cannot carry the typed reason, tick, revision,
or bounded diagnostic identity required to explain later failures.

## Decision 5: deterministic bounded test sinks

### Option A: fixed-capacity FIFO recorders with reject-newest overflow (recommended)

Provide separate single-threaded test recorders for metrics and events. Capacity
is fixed at construction within a declared maximum. Accepted observations are
stored in exact call order. When full, the recorder rejects the newest item,
returns `Dropped`, and increments a saturating dropped-count diagnostic. Tests
can inspect an immutable span, dropped count, and explicit clear operation.

Recorders do not read a clock, add timestamps, allocate after construction,
format text, hash observations, or affect canonical traces/checksums. Semantic
ticks/revisions are supplied by the caller when the event contract includes
them. Identical calls therefore produce identical recorder evidence.

### Option B: unbounded vector recorder

This is simple, but cannot demonstrate the hostile-input memory bound or sink
overflow behavior required by the threat model.

### Option C: overwrite-oldest ring buffer

This retains recent evidence, but silently destroys the prefix needed for exact
causal assertions and makes overflow harder to distinguish from an intentionally
short trace.

### Option D: mocks only

Per-test mock callbacks can assert individual calls, but do not establish one
reusable bounded ordering/overflow contract for later multi-client simulations.

## Proposed acceptance tests and demo

If the recommended options are approved, Slice 3.7 must demonstrate:

1. server-core observability headers compile without OpenMW, transport-library,
   exporter, formatting-library, platform, or test-support headers;
2. ADR-0014's production dependency graph remains unchanged and the reverse
   production-to-test-support checks still fail closed;
3. explicit no-op sinks accept attempts without global state or allocation;
4. metrics distinguish counter-add, gauge-set, and distribution observations,
   enforce fixed key/kind/unit definitions, and reject dynamic/high-cardinality
   dimensions at the type boundary;
5. structured events preserve exact kind, severity, semantic tick, and typed
   fields without accepting free-form strings or byte payloads;
6. capacities zero, one, and the declared maximum have exact FIFO,
   reject-newest, dropped-count, clear, and reuse behavior;
7. dropped observations and sink absence do not change a simulated canonical
   result or deterministic trace;
8. secret/password/token and long-user-text canaries cannot enter either public
   observation shape;
9. all existing contracts plus the new observability contract pass in the
   standalone and real baseline graphs and under both ADR-0019 sanitizer jobs;
   and
10. repository-owned policy tests, legacy exclusion, provenance, and supported
    platform CI remain green.

The implementation demo should show typed metric and event capture, explicit
no-op composition, zero/one/full capacity behavior, deterministic replay of an
identical observation sequence, and compile-time rejection of a free-form or
test-support dependency. Owner acceptance remains required before Slice 3.7 or
Phase 3 can be marked **Implemented**.

## Consequences of the recommendation

- The first observability seam is deliberately server-core-owned; transport and
  client domains do not inherit server taxonomy accidentally.
- Explicit injection makes observability dependencies visible and parallel tests
  isolated, at the cost of passing a small bundle through future constructors.
- Closed types make privacy, units, cardinality, and tests enforceable, but each
  feature must add reviewed categories rather than inventing strings ad hoc.
- Direct attempts keep the core free of threads and queues. Production adapters
  still owe bounded non-blocking isolation before they are accepted.
- Reject-newest test recorders preserve causal prefixes and make lost evidence
  explicit, but are diagnostic tools rather than production retention policy.

## Failure modes and mitigations

- **A new target is added by convenience:** keep the current graph under Option
  A and require an ADR-0014 amendment before any topology change.
- **A sink blocks despite the contract:** production adapters must prove bounded
  `try` behavior; core code never owns or waits for their workers.
- **Observation failure changes gameplay:** return a diagnostic result only and
  test identical canonical output for accepted, dropped, and no-op sinks.
- **Metric cardinality grows through IDs:** make metric dimensions closed enums
  and prohibit semantic identities at the metric type boundary.
- **Secrets enter structured events:** expose no text/byte input field and keep
  canary tests with every future authentication/transport observation slice.
- **Event variants become a central dumping ground:** add event kinds only with
  their owning behavior and explicit fields, privacy, and acceptance assertions.
- **Test recorders hide loss:** reject newest and expose a saturating dropped
  count; never overwrite silently.
- **Diagnostic time becomes canonical:** accept only caller-provided semantic
  tick/revision fields and exclude observations from state bytes and checksums.

## Review and replacement triggers

Reopen this ADR if:

- transport/client observability demonstrates a genuinely shared vocabulary
  that justifies a new dependency-free production target;
- a required metric cannot use the fixed integer operations or low-cardinality
  enum dimensions;
- a structured event requires user text, raw dependency diagnostics, network
  addresses, or other privacy-sensitive fields;
- direct non-blocking attempts cannot satisfy measured production latency;
- a production sink needs a different overflow, shutdown, or delivery guarantee;
- multi-threaded emitters require a shared concurrency contract; or
- Phase 21 audit requirements need stronger durability than diagnostic events.

## Owner approval

Pending. The recommendation is Option A for Decisions 1 through 5.

Approval would fix a server-core-owned, explicitly injected, typed,
non-blocking/noexcept observability boundary and bounded deterministic
test-support recorders. It would not approve a production backend, exporter,
thread, queue, wire schema, canonical-state field, audit guarantee, gameplay
category, authority rule, or state-scope change.
