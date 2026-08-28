# ADR-0032: Phase 6 transport adapter and lifecycle boundary

Status: **Proposed**

Date opened: 2026-08-28

Decision owner: project owner

Needed by: Phase 6 Slice 6.1

## Decision question

How should the accepted GameNetworkingSockets dependency be integrated without
contaminating the project-owned transport abstraction, and what owned lifecycle
contract should wrap listen, connect, cancellation, disconnect, polling, and
shutdown before channel delivery is added?

This decision is required before Slice 6.1 production code. It refines the
accepted transport selection in
[`ADR-0005`](ADR-0005-transport-security-authentication-resumption.md) and, if
accepted, narrowly amends the target topology in
[`ADR-0014`](ADR-0014-phase3-target-topology-boundary-enforcement.md) by adding
one selected-library adapter target. It does not reopen the selected dependency,
encryption profile, active-MITM limitation, authentication ordering, or protocol
and server-core boundaries.

## Existing approved constraints

1. `tes3mp_transport` remains the project-owned abstraction and continues to
   expose no GameNetworkingSockets, OpenMW, operating-system, renderer, or
   platform types.
2. GameNetworkingSockets `v1.6.0` and its exact OpenSSL, Protobuf, Abseil, and
   `utf8_range` dependency profile remain pinned by ADR-0005 and
   `scripts/vnext_gamenetworkingsockets_proof.json`.
3. Direct-IP connections use automatic encryption without authenticated server
   identity. Production cannot enable plaintext operation or claim endpoint
   authentication.
4. Transport handles, addresses, callbacks, state enums, error strings, clocks,
   buffers, and statistics are adapter-private.
5. Connection identity grants routing only. It is not a principal, player,
   session, or gameplay authority.
6. Reliable/latest-wins channels, authentication/resumption, full queue and rate
   budgets, telemetry, and socket fault integration remain Slices 6.2–6.6.

## Scenarios evaluated

1. A dedicated server starts one direct-IP listener and reports its actual
   numeric endpoint without exposing a library address structure.
2. A client starts an asynchronous connection, cancels it before establishment,
   and receives exactly one terminal owned event even if a delayed library
   callback arrives.
3. A server accepts an encrypted connection, maps a reused library handle to a
   never-reused owned connection identity, then closes it without delivering
   stale work to a replacement connection.
4. A listener is stopped while inbound handshakes and connection-status
   callbacks are pending. No later accept event targets the stopped listener.
5. A caller repeatedly closes, cancels, stops, or shuts down already-finalized
   work. Operations remain idempotent and bounded.
6. The selected dependency is absent, altered, or supplied by an unverified
   system package. Production adapter configuration fails closed while the
   abstraction and deterministic tests remain buildable.
7. Lifecycle admission or event retention fills. New work is rejected with a
   typed result and no owned event is silently dropped.
8. Runtime destruction races delayed callbacks. Adapter-private generation
   checks discard them before any application callback or state access.

## Decision 1: build-target boundary

### Option A: separate abstraction and selected adapter targets (recommended)

Keep `tes3mp_transport` and `TES3MP::Transport` selected-library-free. Add a
private production adapter target, `tes3mp_transport_gns`, with build alias
`TES3MP::GameNetworkingSocketsTransport`. It may depend directly on
`tes3mp_transport` and the exact pinned GameNetworkingSockets static target.
Only composition and focused adapter tests link it; `tes3mp_client_session`,
`tes3mp_server_core`, and `tes3mp_protocol` do not.

The adapter may expose an owned factory header returning the project-defined
runtime interface, but that header contains no selected-library types. Boundary
checks scan the abstraction and public factory surface independently, allow GNS
includes only in the adapter's private sources, and continue to reject OpenMW,
renderer, platform, and test-support dependencies.

Advantages:

- preserves the already accepted abstraction dependency graph;
- deterministic fakes and core tests do not build or link the network stack;
- selected-library containment is mechanically visible; and
- a future reviewed transport replacement changes composition rather than the
  client-session API.

Tradeoffs:

- adds one production target and a narrow ADR-0014 topology amendment; and
- supported builds must explicitly provision and link the adapter.

### Option B: put the private implementation in `tes3mp_transport`

Link GameNetworkingSockets privately into the existing transport target and
keep only its headers clean. This uses fewer targets, but every transport
consumer inherits the dependency at link time and ADR-0014's engine-independent
target rule must be weakened.

### Option C: runtime plugin interface

Load transport implementations dynamically through a registry or shared-library
ABI. This offers replacement without relinking, but adds ABI versioning, loader,
deployment, lifetime, and error surfaces that the first release does not need.

## Decision 2: dependency provisioning

### Option A: one verified repository-owned provisioner (recommended)

Extract the approved lock validation, safe archive extraction, license checks,
and exact source builds into reusable repository-owned provisioning support.
Both the retained proof and production adapter consume the same lock and
verified outputs. Product CMake accepts only explicit verified source/install
paths plus a generated manifest matching the lock; it performs no implicit
system-package fallback or dependency-manager resolution.

Ordinary abstraction-only builds need none of these dependencies. A requested
adapter build fails at configure time if a verified input, exact pin, crypto
backend, license, generated-input policy, or restricted build option differs.

Advantages:

- proof and production cannot silently drift to different sources;
- downloads, hashes, licenses, and build flags remain auditable; and
- offline/repeat builds can reuse a verified local cache.

Tradeoffs:

- provisioning remains a deliberate pre-configure step; and
- the proof runner must be refactored without weakening its retained evidence.

### Option B: CMake downloads and builds the dependency graph

Use `FetchContent`/`ExternalProject` from product configuration. This is
convenient for a first build, but makes OpenSSL/Protobuf/GNS toolchain staging
part of CMake configuration and duplicates proof validation unless substantial
custom plumbing is added.

### Option C: system or package-manager dependencies

Use discovered OpenSSL, Protobuf, Abseil, and GameNetworkingSockets packages.
This reduces repository tooling but violates the accepted exact-pin, generated-
input, static-profile, and cross-platform evidence contract.

## Decision 3: public endpoint value

### Option A: owned numeric IP address plus port (recommended)

Expose a validated project-owned endpoint with a fixed 16-byte address, an
explicit IPv4/IPv6 family, and a 16-bit port. Connect rejects port zero; listen
may request port zero and returns the actual bound endpoint. Parsing user text,
DNS, discovery, and interface enumeration remain outside the adapter and outside
Slice 6.1.

Advantages:

- exactly matches the approved direct-IP transport scope;
- has deterministic validation and no raw user/library string lifetime; and
- avoids making DNS policy or OS resolver behavior part of the core boundary.

Tradeoffs:

- callers must resolve or parse host input before connecting; and
- hostname support requires a later reviewed boundary if it enters scope.

### Option B: bounded host string plus port

Pass a bounded string to the adapter and let GameNetworkingSockets parse it.
This is convenient at call sites but makes library parser behavior and raw-text
error handling part of the production boundary.

### Option C: endpoint plus asynchronous DNS resolver

Own hostname resolution, cancellation, address ordering, TTLs, and retry policy
inside transport. This is useful eventually but adds operating-system behavior
and security/UX policy not required by Slice 6.1.

## Decision 4: owned lifecycle API

### Option A: one runtime with commands and value events (recommended)

Define one owning `TransportRuntime` interface with typed commands for start/
stop listener, start/cancel connect, graceful close, immediate abort, bounded
poll, and shutdown. Commands return typed admission results and distinct opaque
`ListenerId`, `ConnectAttemptId`, and `TransportConnectionId` values. Polling
emits closed project-owned lifecycle event values into caller-provided bounded
storage; the runtime never retains a user callback or an application object
pointer.

Each accepted listener, attempt, and connection produces exactly one terminal
event. IDs are monotonically allocated and never reused within a runtime.
Counter exhaustion terminally fails the runtime. Repeated finalization is an
explicit idempotent result, and library handle reuse cannot revive an owned ID.

Advantages:

- event order and lifetime are explicit and testable;
- no callback can target a destroyed client/server object; and
- the same interface supports real and deterministic implementations.

Tradeoffs:

- callers must own a pump loop and route value events; and
- a composition layer must translate events into client/server session actions.

### Option B: virtual listener and connection objects with callbacks

Return RAII connection objects and invoke user callbacks for state changes.
This is familiar but makes callback reentrancy, destructor behavior, replacement,
and cross-thread lifetime part of every consumer.

### Option C: concrete PIMPL API

Expose one GameNetworkingSockets-backed concrete runtime whose implementation
is hidden by PIMPL. Library types stay private, but client session and tests
become coupled to one production implementation rather than the owned port.

## Decision 5: threading and callback ownership

### Option A: caller-confined explicit pumping (recommended)

All runtime commands and polling occur on one caller-owned I/O context. The
adapter may use GameNetworkingSockets' internal threads, but no library callback
crosses the adapter; connection-status callbacks are observed only during the
explicit pump and converted to owned events. An adapter-private runtime
generation rejects delayed work after shutdown. A future process may dedicate a
thread to this context, but the adapter itself owns no worker thread or sleep
loop.

Advantages:

- deterministic command/event order and teardown;
- no hidden synchronization or callback concurrency in the public contract; and
- deterministic tests can drive the same pump semantics.

Tradeoffs:

- the caller must pump frequently; and
- Phase 6 composition must supply bounded cross-thread queues if other threads
  submit work.

### Option B: adapter-owned worker thread

The runtime owns its thread and synchronized command/event queues. This reduces
caller pumping but commits to background lifetime, wakeup, shutdown, and queue
policy before the server/client composition roots exist.

### Option C: thread-safe direct calls and callbacks

Allow calls from arbitrary threads and deliver callbacks on whichever thread
observes them. This maximizes flexibility but makes ordering, teardown, and race
proof substantially harder and conflicts with deterministic simulation goals.

## Decision 6: initial Slice 6.1 safety envelope

### Option A: proof-bounded lifecycle profile, raised only in Slice 6.4 (recommended)

Construct each runtime with a role-specific limit profile, validated against
initial hard ceilings of one listener, eight simultaneous pending handshakes or
outgoing attempts, eight established connections, and 128 retained lifecycle
events. New work at capacity returns `AtCapacity` without creating a library
handle. The caller must supply event storage; a full retained-event budget or a
library callback drain that cannot be represented terminally fails the runtime,
aborts remaining handles, and reports one closed overflow result.

These are deliberately Slice 6.1 proof capacities, not the release player cap.
Slice 6.4 must review measured work/memory evidence and explicitly approve the
product server profile before increasing established connections toward Phase
5's canonical maximum of 256.

Advantages:

- starts from the already exercised eight-handshake/128-callback proof scope;
- gives lifecycle code a real fail-closed bound immediately; and
- avoids claiming unmeasured 256-peer transport capacity.

Tradeoffs:

- the first adapter slice is intentionally limited to small integration tests;
- reaching product capacity is explicitly deferred to Slice 6.4; and
- a terminal event-overflow policy favors correctness over availability.

### Option B: use the Phase 5 maximum immediately

Allow 256 established connections in Slice 6.1. This aligns with canonical
state capacity but lacks production transport memory/work evidence and pulls
Slice 6.4 policy into the lifecycle slice.

### Option C: caller-selected unbounded capacities

Accept arbitrary sizes and rely on allocation failure or library limits. This
is flexible but violates the hostile-input and bounded-work requirements.

## Recommendation

Approve Option A for Decisions 1–6:

1. preserve `tes3mp_transport` and add a private `tes3mp_transport_gns` adapter;
2. share one verified exact-lock provisioner between proof and production;
3. expose numeric IPv4/IPv6 endpoints only;
4. use an owning command/value-event runtime with distinct never-reused IDs;
5. confine it to an explicitly pumped caller-owned I/O context; and
6. begin with the proof-bounded 1/8/8/128 lifecycle profile until Slice 6.4
   reviews measured product limits.

This is the narrowest path that preserves every accepted dependency boundary,
turns library callbacks and handle reuse into deterministic owned values, and
does not preempt later channel, authentication, queue, telemetry, or gameplay
decisions.

## Proposed Slice 6.1 acceptance tests

1. `tes3mp_transport` and its public headers build without GNS, OpenMW, platform,
   renderer, test-support, or operating-system headers and libraries.
2. `tes3mp_transport_gns` is the only production target allowed to include or
   link GameNetworkingSockets; protocol, server core, and client session cannot
   acquire that dependency.
3. adapter configuration rejects absent, mismatched, unverified, plaintext, or
   wrong-profile dependency inputs before compiling production code.
4. verified exact pins build the adapter and one focused lifecycle executable;
   abstraction-only standalone builds remain network-dependency-free.
5. numeric IPv4 and IPv6 endpoints round-trip; invalid family/port values and a
   connect port of zero fail before a library call; listen port zero returns the
   actual bound port.
6. a listener and client establish a real loopback connection, and the owned
   event reports encrypted-but-unauthenticated transport without exposing a
   library state or handle.
7. cancelling a pending connection emits exactly one terminal cancellation and
   ignores its delayed connection-status callback.
8. stopping a listener prevents later accept events from targeting it while
   existing accepted connections retain independent identities.
9. graceful close, immediate abort, peer close, connection failure, and runtime
   shutdown each emit one ordered owned terminal event; repeated requests remain
   idempotent.
10. forced library-handle reuse and delayed callback injection cannot alias a
    new owned listener, attempt, connection, or runtime generation.
11. the initial listener/attempt/connection/event capacities reject the next
    item without partial ownership or unbounded allocation.
12. event-capacity overflow and counter exhaustion fail closed, release every
    library handle, and never invoke user code after terminal runtime failure.
13. all public adapter-factory headers compile in isolation without selected-
    library, OpenMW, platform, socket, or operating-system types.
14. focused lifecycle tests pass under applicable MSVC, ASan/UBSan, and TSan
    profiles; the complete hosted platform matrix remains the Phase 6 exit gate.

## Explicit non-decisions

- No protocol payload is sent or received in Slice 6.1.
- Reliable lanes and latest-wins delivery remain Slice 6.2.
- Join password and resume-token implementation remain Slice 6.3.
- Product queue sizes, rate limits, slow-peer eviction, and 256-peer capacity
  remain Slice 6.4.
- Stable detailed telemetry/disconnect catalogs remain Slice 6.5.
- DNS, discovery, NAT traversal, Steam services, certificates, and endpoint
  authentication remain out of scope.
- Reconnect grace, replacement, player visibility, and other gameplay behavior
  require their owning later ADR/GDR.

## Review and replacement triggers

Reopen this decision if the selected dependency or restricted build profile
changes; the explicit pump cannot bound callback work; numeric direct-IP is
insufficient for the approved product; product-capacity evidence cannot fit the
command/event contract; a second transport implementation becomes required; or
the adapter target needs an OpenMW, renderer, gameplay, scripting, persistence,
or test-support dependency.

## Owner approval gate

No option is accepted yet. Slice 6.1 production implementation must not begin
until the project owner explicitly approves or amends Decisions 1–6 and the
proposed acceptance tests. Acceptance will also record the narrow ADR-0014
topology amendment for the selected adapter target.
