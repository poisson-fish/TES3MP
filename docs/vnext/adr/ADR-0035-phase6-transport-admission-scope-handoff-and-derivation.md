# ADR-0035: Phase 6 transport admission-scope handoff and derivation

Status: **Accepted**

Date opened: 2026-08-28

Decision owner: project owner

Date approved: 2026-08-28

Needed by: Phase 6 Slice 6.3 closure

## Decision question

How should the real transport hand its approved privacy-preserving source scope
to authentication composition, and how should it derive that opaque value from
the private peer address?

ADR-0034 already approves one process-keyed scope per IPv4 address or IPv6 /64,
with no address exposed or logged. The current owned transport API has no scope
handoff, and the exact keyed derivation was not selected. Production work pauses
at this boundary until the owner approves an option.

## Scenarios

1. Two connections from one IPv4 address receive the same scope in one runtime;
   a different IPv4 address receives a different scope.
2. IPv6 addresses in one /64 share a scope; addresses in different /64s do not.
3. A new runtime key changes every derived value, so a scope is not a stable
   cross-process identifier.
4. Only an accepted inbound encrypted connection exposes a scope. Outbound
   success, failures, closure, and all public text expose none.
5. Key generation or derivation failure fails closed before an inbound
   connection can reach authentication.

## Decision 1: owned handoff

### Option A: scope on the accepted-connection event (recommended)

Add an optional opaque `AdmissionScopeId` to `TransportEvent`, populated only
for `ConnectionAccepted`. The connection ID, encrypted-ready state, and scope
arrive atomically in one bounded value event. No address or adapter query enters
composition.

Tradeoff: the common lifecycle event gains one authentication-admission field,
though it remains selected-library-free and has no byte accessor.

### Option B: runtime query by connection ID

Add `admissionScope(connection)` to `TransportRuntime`. This keeps the lifecycle
event smaller, but creates a second stateful call with unknown/stale-ID behavior
and a timing edge between acceptance and close.

### Option C: separate admission event/object

Emit a second scope event or return an owned connection-admission object. This
separates concerns but adds ordering, retention, and partial-delivery states to
the already bounded event contract.

## Decision 2: private derivation

### Option A: HMAC-SHA-256 with a runtime key (recommended)

At real-runtime creation, generate a fresh 32-byte key with the exact verified
OpenSSL profile. HMAC a fixed domain tag, family tag, and canonical source
prefix: four address bytes for IPv4 or the first eight bytes for IPv6 /64. Use
the full 32-byte result as `AdmissionScopeId`; cleanse the key and temporary
input/output buffers. A random or HMAC failure makes runtime creation or the
affected inbound admission fail closed. No OpenSSL type crosses a public header.

Tradeoff: `tes3mp_transport_gns` directly uses the already verified private
OpenSSL dependency for this narrow adapter service.

### Option B: keyed SHA-256 concatenation

Hash a random key plus canonical source bytes. This is smaller, but invents a
custom keyed construction where a standard MAC is available.

### Option C: random IDs in a fixed address map

Retain private normalized addresses in a fixed table and assign random IDs.
This avoids keyed hashing, but duplicates the limiter table, adds eviction and
lifetime policy, and retains raw addresses longer.

## Recommendation and acceptance tests

Approve Option A for both decisions. It preserves the accepted command/value-
event model, makes acceptance atomic, uses a standard keyed primitive, and adds
no address, account, canonical state, authority, release default, or gameplay
behavior.

Focused tests should prove event-field legality, deterministic injected-key
IPv4 and IPv6-/64 stability/separation, new-key unlinkability, key/derivation
failure closure, no outbound scope, handle-reuse isolation, public-header and
target boundaries, cleansing, and absence of address/canary text from owned
events and captured diagnostics.

## Owner approval gate

Approved by the project owner in the 2026-08-28 working session: Option A for
Decisions 1 and 2 and the proposed focused tests without amendment.
