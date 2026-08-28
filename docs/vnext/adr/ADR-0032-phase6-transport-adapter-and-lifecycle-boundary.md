# ADR-0032: Phase 6 transport adapter and lifecycle boundary

Status: **Accepted**

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

## Decision history

On 2026-08-28 the project owner approved Option A for Decisions 1, 2, 4, 5,
and 6 plus their proposed acceptance coverage. The owner did not approve
Decision 3 Option A: hostname/DNS resolution must be available from the first
connection slice, with a connection host string and a separate port number.

ADR-0032 therefore remains **Proposed**. The five accepted decisions are stable,
but Slice 6.1 production remains gated until the amended Decision 3 resolver,
address-selection, bounds, dependency, and failure behavior is approved.

Later on 2026-08-28, the project owner approved Option A for amended Decisions
3.1–3.3 and their expanded acceptance coverage. All six architecture decisions
are now approved. Decision 3.2 authorizes only a disposable exact-pin c-ares
selection proof; ADR-0032 remains **Proposed** and Slice 6.1 production remains
gated until that proof passes and the owner separately accepts the exact c-ares
dependency profile.

The authorized proof candidate was then implemented without changing production
targets. Its exact c-ares 1.34.8 release archive, MIT license, restricted static
profile, local loopback DNS fixture, and 13 public-API scenarios pass locally on
MSVC 19.44 and in the complete supported five-job hosted matrix on exact candidate
`6dcea1b4afe2da9ce6005ec80cdb897d98f6739d`. Retained-artifact consistency review
passes. On 2026-08-28 the project owner accepted this exact dependency profile
and authorized Slice 6.1 production integration under the approved boundaries.

## Exact-pin resolver proof candidate

The disposable [c-ares selection proof](../proofs/cares/README.md) records:

- tag `v1.34.8` at commit `63a4c4c71b86e448bcc1c55287c35aa4aa0f4246`;
- release archive SHA-256
  `c222b6d681096f9444d2c4863d2c1174019e27cacca0a4a5c114d36dd7d7bf78`;
- MIT `LICENSE.md` SHA-256
  `460f5e768fda3752ca2169a95df062578a10fb126bfd65f3b9b1a1bed2f84807`;
- static-only, shared/tools/tests/install-off configuration with the c-ares
  query cache disabled and no use of `ARES_OPT_EVENT_THREAD`;
- caller-owned pumping through the socket-state callback and
  `ares_process_fds`; and
- loopback-only success, failure, cancellation, destruction, duplicate, and
  over-bound answer scenarios plus numeric bypass and separate-port checks.

The exact lock, runner, safe archive extraction, license verification, supported
five-job desktop workflow, Linux Clang 18 ASan+UBSan profile, and retained JSON
evidence are part of the proof. It deliberately makes no production integration,
DNSSEC, encrypted-DNS, application-cache, or connection-race claim. The
[supported proof run](https://github.com/poisson-fish/TES3MP/actions/runs/33202049447)
and its five retained artifacts pass consistency review; exact profile acceptance
remains a separate owner gate.

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

## Decision 3: connection host, DNS, and address selection

The owner requires a separate connection host string and port from Slice 6.1.
GameNetworkingSockets' public `SteamNetworkingIPAddr` parser accepts numeric IP
addresses and optional ports; it does not provide the bounded asynchronous DNS,
cancellation, and multi-address connection policy required here. The remaining
decision therefore has three linked parts.

### Decision 3.1: public connection input

#### Option A: bounded ASCII host plus separate port (recommended)

Expose `ConnectionEndpoint` as an owned host string plus a `uint16_t` port. The
host is 1–253 ASCII bytes, contains either a numeric IPv4/IPv6 literal or a DNS
name with labels of 1–63 bytes, and contains no scheme, credentials, path, query,
fragment, whitespace, or embedded port. A DNS trailing root dot is accepted and
normalized; ASCII letters are compared case-insensitively. Unicode input is
rejected, while caller-supplied valid ASCII IDNA A-labels (`xn--...`) are
accepted without project-owned Unicode conversion. Connect rejects port zero.

Numeric literals take a no-DNS fast path. DNS names resolve A and AAAA records
asynchronously. Listener configuration remains a numeric owned bind address plus
port; wildcard/port-zero listen behavior is unchanged and does not perform DNS.

This keeps IPv6 colons unambiguous, bounds untrusted text before allocation, and
does not turn a connection endpoint into a URI or discovery record.

#### Option B: combined `host:port` connection string

Accept one string and parse brackets, IPv6, port, and host together. This is
convenient for copy/paste but adds ambiguous formatting and duplicates a port
field the owner explicitly requested.

#### Option C: URI-style endpoint

Accept a URI with scheme and optional path/query data. This anticipates future
discovery but adds unused syntax, normalization, and credential-confusion risks.

### Decision 3.2: resolver implementation

#### Option A: prove and pin c-ares behind the GNS adapter (recommended)

Authorize a disposable dependency-selection proof for current c-ares before
production integration. The candidate is c-ares `1.34.8`, an MIT-licensed,
actively maintained asynchronous resolver with Windows, Linux, macOS, Android,
and other supported builds. It supplies asynchronous `getaddrinfo`, explicit
cancellation, and caller-event-loop integration. If its exact pin, archive and
license hashes, restricted static build, sanitizer/fuzz evidence, and supported
desktop matrix pass, extend the Decision 2 verified lock/provisioner and link it
privately only into `tes3mp_transport_gns`.

Use c-ares without its event thread: socket-state notifications and
`ares_process_fds` integrate with the already approved caller-confined pump, so
Decision 5 remains unchanged. Selected-library address/socket/callback/status
types remain private. Each connect attempt owns a separately cancellable resolver
channel or equivalent isolated query owner so cancelling one attempt cannot
cancel another.

This option is not final dependency approval. Exact source and proof evidence
must return to the owner before c-ares-dependent production code lands.

Primary research evidence:

- [c-ares 1.34.8 release](https://github.com/c-ares/c-ares/releases/tag/v1.34.8)
- [c-ares project, maintenance, license, and platform overview](https://github.com/c-ares/c-ares)
- [`ares_getaddrinfo` asynchronous address API](https://c-ares.org/docs/ares_getaddrinfo.html)
- [`ares_process_fds` caller-event-loop integration](https://c-ares.org/docs/ares_process_fds.html)
- [`ares_cancel` cancellation behavior](https://c-ares.org/docs/ares_cancel.html)

#### Option B: three native asynchronous resolver backends

Implement and maintain separate cancellable Windows, Linux, and macOS resolver
adapters. This avoids a new library but creates three platform code paths,
different callback/lifetime behavior, and a larger cross-platform test surface.

#### Option C: blocking system `getaddrinfo`

Resolve on the transport pump thread. This adds no dependency, but one DNS
lookup can stall every connection and lifecycle callback, cannot be bounded or
cancelled portably, and conflicts with the approved explicit-pump lifecycle.

### Decision 3.3: multi-address connection policy

#### Option A: bounded Happy Eyeballs v2 subset (recommended)

Resolve IPv6 and IPv4 asynchronously, preserve the resolver's RFC 6724 ordering,
retain at most eight unique numeric addresses, and use one owned
`ConnectAttemptId` for all candidates. Start the first viable candidate without
waiting for every DNS answer, allow at most two simultaneous GNS candidate
handles, use a 50 ms IPv6 resolution preference delay and a 250 ms stagger
between connection candidates, and cancel/close every losing candidate when the
first encrypted connection succeeds.

The existing eight-pending Slice 6.1 ceiling counts the logical connect attempt,
while each attempt has a separate hard ceiling of two live candidate handles.
Cancellation, timeout, shutdown, or terminal DNS failure closes all candidates
and emits exactly one owned terminal event. DNS uses the host's configured
resolver and makes no DNSSEC, DoH, endpoint-authentication, or confidentiality
claim. Slice 6.1 adds no application-level DNS cache; every new logical attempt
resolves again, subject to bounded resolver-internal behavior proven with the
selected dependency.

The initial delay and racing policy follows
[RFC 8305 Happy Eyeballs v2](https://www.rfc-editor.org/rfc/rfc8305.html).

#### Option B: sequential resolver-order attempts

Try one returned address at a time. This is simpler and uses one candidate
handle, but a broken first IPv6 or IPv4 path can impose the full connection
timeout before fallback.

#### Option C: first address only

Use only the first DNS answer. This has the smallest implementation but makes
multi-address and dual-stack hosts unnecessarily fragile.

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

The owner has approved Option A for Decisions 1, 2, 4, 5, and 6. For the amended
Decision 3, recommend Option A for Decisions 3.1–3.3:

1. preserve `tes3mp_transport` and add a private `tes3mp_transport_gns` adapter;
2. share one verified exact-lock provisioner between proof and production;
3. accept a bounded host string and separate port, authorize a c-ares selection
   proof, and use a bounded Happy Eyeballs v2 subset if that proof is accepted;
4. use an owning command/value-event runtime with distinct never-reused IDs;
5. confine it to an explicitly pumped caller-owned I/O context; and
6. begin with the proof-bounded 1/8/8/128 lifecycle profile until Slice 6.4
   reviews measured product limits.

This preserves every accepted dependency boundary while providing hostname
connections without blocking the deterministic transport pump. The c-ares
candidate remains research-only until its exact dependency proof and owner
review pass.

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
5. valid numeric IPv4/IPv6 literals, ordinary DNS names, trailing-root names,
   and valid ASCII IDNA A-labels pair with a separate nonzero port; empty,
   oversized, invalid-label, Unicode, URI-like, embedded-port, whitespace, and
   zero-port inputs fail before resolver or library work.
6. numeric input bypasses DNS; named input resolves bounded A/AAAA results and a
   listener still reports its actual numeric bound address and port.
7. same-seed fake-resolver tests reproduce exact DNS completion, address-order,
   stagger, cancellation, winning-candidate, and terminal-event traces.
8. the real resolver proof covers success, NXDOMAIN/no-data, timeout, malformed
   response, cancellation, destruction, duplicate addresses, more than eight
   answers, IPv4-only, IPv6-only, and dual-stack cases without leaking resolver
   types or unbounded work.
9. the Happy Eyeballs path starts at most two candidate handles, uses the
   approved delays, closes every loser after one encrypted winner, and emits one
   terminal result when all candidates fail.
10. a listener and client establish a real loopback connection, and the owned
   event reports encrypted-but-unauthenticated transport without exposing a
   library state or handle.
11. cancelling a DNS or connection attempt emits exactly one terminal
   cancellation, closes every resolver/candidate handle, and ignores delayed DNS
   and connection-status callbacks.
12. cancelling a pending numeric connection emits exactly one terminal cancellation and
   ignores its delayed connection-status callback.
13. stopping a listener prevents later accept events from targeting it while
   existing accepted connections retain independent identities.
14. graceful close, immediate abort, peer close, connection failure, and runtime
   shutdown each emit one ordered owned terminal event; repeated requests remain
   idempotent.
15. forced library-handle reuse and delayed callback injection cannot alias a
    new owned listener, attempt, connection, or runtime generation.
16. the initial listener/attempt/connection/event and per-attempt candidate
    capacities reject the next
    item without partial ownership or unbounded allocation.
17. event-capacity overflow and counter exhaustion fail closed, release every
    library handle, and never invoke user code after terminal runtime failure.
18. all public adapter-factory headers compile in isolation without selected-
    library, OpenMW, platform, socket, or operating-system types.
19. focused resolver/lifecycle tests pass under applicable MSVC, ASan/UBSan, and TSan
    profiles; the complete hosted platform matrix remains the Phase 6 exit gate.

## Explicit non-decisions

- No protocol payload is sent or received in Slice 6.1.
- Reliable lanes and latest-wins delivery remain Slice 6.2.
- Join password and resume-token implementation remain Slice 6.3.
- Product queue sizes, rate limits, slow-peer eviction, and 256-peer capacity
  remain Slice 6.4.
- Stable detailed telemetry/disconnect catalogs remain Slice 6.5.
- Service discovery, SRV/SVCB policy, custom DNS servers, DNSSEC validation,
  DoH/DoT, NAT traversal, Steam services, certificates, and endpoint
  authentication remain out of scope. Ordinary A/AAAA hostname resolution is
  now required by Decision 3.
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

Option A is owner-approved for Decisions 1–6, including amended Decisions
3.1–3.3 and their expanded acceptance tests. The authorized disposable c-ares
proof passes locally and across its complete supported five-job hosted matrix;
the five retained artifacts agree on the exact dependency, license, build
profile, budgets, and 13-scenario test contract. On 2026-08-28 the project owner
explicitly accepted the exact c-ares 1.34.8 dependency profile and authorized
dependent Slice 6.1 production integration. This ADR is **Accepted**.
