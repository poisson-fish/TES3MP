# ADR-0034: Phase 6 credential and resumption boundary

Status: **Proposed**

Date opened: 2026-08-28

Decision owner: project owner

Needed by: Phase 6 Slice 6.3

## Decision question

How should the accepted optional join password and single-use resume token cross
the bounded protocol/session boundary; where should cryptographic work live;
how are new and resumed routing identities represented; and what bounded
rate-limit and token-lifetime policy belongs in Slice 6.3 without deciding
player replacement or reconnect gameplay?

Production implementation is gated on owner approval. This ADR refines accepted
ADR-0005 and reopens only the narrow provider-result part of ADR-0023 needed to
carry resume routing claims. It does not grant gameplay authority, select player
identity, attach canonical state, or decide visible reconnect/replacement rules.

## Recommendation summary

Recommend Option A for Decisions 1 through 5:

1. keep codecs in `tes3mp_protocol`, credential memory in the client-session
   boundary, and password/token services in `tes3mp_server_core`; use the exact
   verified OpenSSL crypto library privately in the real-network profile;
2. use three bounded reliable session-control messages with tagged join versus
   resume input, generic public rejection, and an exact 32-byte fresh token;
3. give each initial open/password join a never-reused process-local principal,
   preserve that principal plus session identity on resume, and extend the
   provider result only with minimal routing claims and take-once response data;
4. keep only SHA-256 token digests in a bounded in-memory store and atomically
   validate, consume, increment generation, and replace tokens under a
   caller-supplied 1-to-120-second lifetime with no release default; and
5. use a bounded global plus privacy-preserving source-scope token-bucket gate,
   configured by validated caller policy, with one authentication submission
   per encrypted connection and no blocking sleeps.

## Existing constraints

1. GameNetworkingSockets must report an encrypted established connection before
   negotiation, password, or token submission. Production cannot request its
   unencrypted mode.
2. The direct connection does not authenticate the intended server endpoint.
3. Passwords and tokens are at most 256 bytes at the existing provider boundary;
   reusable static password hashes are not accepted.
4. Authentication supplies routing identity only. It grants no player binding,
   command authority, canonical mutation, or gameplay permission.
5. Resume tokens are cryptographically random, finite-lifetime, single-use,
   process-bound, context-bound, and replaced atomically after successful use.
6. Credentials cannot enter canonical state, persistence, replay, logs, metrics,
   free-form errors, retained evidence, or raw stable transport APIs.
7. Session control uses the accepted reliable ordered channel. Slice 6.4 owns
   product queue/capacity policy and Slice 6.5 owns the detailed public
   disconnect/telemetry catalog.

## Representative scenarios

1. An open server accepts empty join material after negotiation, assigns a new
   routing principal, binds a new session, and returns a fresh token.
2. A protected server accepts the exact password and rejects wrong, missing,
   oversized, repeated, early, timed-out, and cancelled submissions without a
   secret-bearing or credential-specific public explanation.
3. A current token resumes the same principal/session context on a fresh
   encrypted connection, advances the checked session generation, and yields a
   replacement token.
4. Concurrent duplicate token uses race. Exactly one consumes and rotates it;
   every loser is denied without partial session attachment.
5. Expired, wrong-protocol, wrong-content-context, wrong-session, old-generation,
   and post-restart tokens are denied without canonical mutation.
6. A distributed password flood stays inside fixed source-table, global-bucket,
   comparison, event, and memory budgets. Legitimate traffic may be denied while
   the safety gate is exhausted, but the server does not block or grow work.
7. Password and token canaries traverse encrypted loopback but appear nowhere in
   captured wire bytes, typed observations, errors, test names, or evidence.
8. Token rotation succeeds but the accepted response is interrupted. The old
   token remains consumed; later replacement/rejoin UX stays a Phase 7/10 GDR
   decision rather than weakening single-use behavior here.

## Decision 1: ownership and cryptographic dependency

### Option A: existing targets plus private verified OpenSSL (recommended)

Put bounded request/result codecs in `tes3mp_protocol`. Put move-only client
credential retention in `tes3mp_client_session`. Put the concrete join-password
provider, principal allocator, and resume store in `tes3mp_server_core` beside
the existing authentication state machine.

Keep portable interfaces and deterministic fake crypto buildable without the
real transport. In the verified real-network profile, compile a private
OpenSSL-backed implementation using the already pinned OpenSSL 3.5.8 archive
and manifest. Use `RAND_bytes`, SHA-256, `CRYPTO_memcmp`, and cleansing helpers;
no OpenSSL or GameNetworkingSockets type enters a public header. This adds no
new permanent project target and does not make transport identity a principal.

Tradeoff: the server-core implementation gains a private production crypto
dependency in the real-network build, though canonical reducer APIs and tests
remain independent of it.

### Option B: add a session-security target

Add a new engine-independent production library between protocol and server
composition. This isolates crypto linkage, but reopens ADR-0014's exact target
set, aliases, dependency allowlist, install graph, and test-support composition.

### Option C: put authentication inside the transport adapter

Reuse its existing OpenSSL linkage. This is smaller in CMake, but makes a
selected transport adapter understand application credentials and conflicts
with ADR-0005's owned authentication boundary.

## Decision 2: wire messages and public results

### Option A: typed request, accepted, and rejected messages (recommended)

Reserve three session-control kinds carried only on `ReliableOrdered`:

- `AuthenticationRequest`: a closed `JoinPassword` or `ResumeToken` tag plus
  move-only opaque bytes; join material is 0 through 256 bytes and a resume
  token is exactly 32 bytes;
- `AuthenticationAccepted`: exactly one fresh 32-byte resume token and its
  relative lifetime in milliseconds, but no principal, player, entity,
  authority, address, or raw provider data; and
- `AuthenticationRejected`: only a generic denied/temporarily-unavailable
  category sufficient for bounded session behavior.

Malformed framing remains a protocol failure. Incorrect password, absent
required password, invalid/expired/replayed/wrong-context token, and unknown
credential all use the same public denial. Detailed closed internal outcomes
may be observed without secrets. A relative lifetime avoids exposing or relying
on server wall-clock time.

### Option B: separate password and resume message families

Use request/result kinds for each credential. This makes routing obvious, but
enlarges the pre-session state matrix and creates more externally distinguishable
failure paths without a second authentication protocol.

### Option C: one untagged opaque credential

Infer empty/password/token from byte length. This minimizes schemas, but makes
credential meaning ambiguous and prevents safe future extension.

## Decision 3: principal, session, and provider-result scope

### Option A: process-local principal plus minimal resume grant (recommended)

Allocate a checked, never-reused nonzero `PrincipalId` for each successful
initial open/password join. It is routing identity for this server process, not
an account or player. Resume returns the same principal and `SessionId`, with
the next checked `SessionGeneration`; restart invalidates tokens and later joins
receive new routing identities.

Narrowly amend ADR-0023's principal-only success result to an
`AuthenticatedAdmission`: principal plus an optional resume grant containing
only `SessionId`, prior generation, next generation, and take-once fresh response
material. The state machine may expose these claims to composition but cannot
bind a player, mutate canonical state, or treat them as authority. Initial joins
receive a token only after composition allocates and binds their session.

Tradeoff: the result is no longer literally principal-only, but the extra data
is the minimum needed to preserve routing identity and rotate a token without a
concrete-provider side channel.

### Option B: principal-only provider plus concrete side channel

Keep ADR-0023 unchanged and make composition query the selected provider for a
resume grant by attempt ID. This avoids changing the generic result but couples
composition to a concrete provider and creates a second lifetime/staleness path.

### Option C: treat every reconnect as a new principal/session

Validate the token but allocate new routing identity. This defeats the approved
resume binding and makes delayed old-session rejection harder to prove.

## Decision 4: token representation, store, and lifetime

### Option A: digest-keyed transactional in-memory store (recommended)

Generate 32 random bytes with the production CSPRNG. Return raw bytes only to
the client and retain only SHA-256 digest keys server-side. Each bounded record
contains principal, session, generation, negotiated-protocol context digest,
caller-supplied content-context digest, and monotonic expiry. Memory-only
storage makes restart invalidation automatic.

Use a hard 256-record Slice 6.3 ceiling, matching but not merging with the
canonical active-session safety ceiling. A validated caller policy chooses a
token lifetime from 1 through 120 seconds; this slice defines no release
default. Phase 7 measures and obtains gameplay approval for the visible grace
and replacement flow.

Consumption prepares the replacement and checked next generation before
mutation, then commits erase-plus-insert under one writer-confined operation.
Failure leaves the old record unchanged; success makes the old digest unusable
before returning the replacement. Concurrent calls serialize at this boundary.

### Option B: retain raw bearer tokens

Use raw tokens as map keys. Lookup is simple, but a memory diagnostic exposes
immediately reusable credentials and redaction auditing is harder.

### Option C: self-contained signed tokens

Encode claims into an authenticated token. This avoids per-token records, but
single-use consumption still needs server state and introduces key lifecycle,
format versioning, and more cryptographic surface.

## Decision 5: password and token attempt limiting

### Option A: global plus opaque source-scope token buckets (recommended)

The transport adapter derives an owned, process-keyed `AdmissionScopeId` from a
normalized remote scope (IPv4 address or IPv6 /64) and never exposes or logs the
address through the authentication API. Gate every credential attempt before
secret comparison with both a process-global bucket and one source bucket.

Use fixed-capacity storage for at most 256 source buckets. Validated caller
policy supplies a per-source burst from 1 through 16, a global burst from 1
through 256, and refill intervals from 100 milliseconds through 120 seconds;
Slice 6.3 defines no release defaults. Exhausted or untrackable scopes fail
closed with a generic temporary denial. The session machine already permits only
one submission per connection. Time is injected monotonic time; no worker
sleeps, unbounded maps, secret-dependent delay, or success-based limit reset
exists.

### Option B: process-global bucket only

This is small and bounds aggregate comparison work, but one attacker can spend
the whole server budget and there is no source isolation.

### Option C: per-connection failures only

This requires no remote scope, but reconnecting bypasses the limit and does not
satisfy the hostile-Internet guessing scenario.

## Proposed acceptance tests and demo

1. Build/public-header checks prove exact target placement, private OpenSSL use,
   and no selected-library or secret-bearing public type leakage.
2. All three codecs cover empty, exact limits, exact-plus-one, wrong tag/length,
   unknown enum, truncation, trailing bytes, golden corpus, and fuzz input.
3. A credential cannot be encoded/sent or accepted before encrypted-ready and
   successful compatible negotiation; an unencrypted production request fails.
4. Open, correct-password, wrong/missing/oversized password, cancellation, and
   each exact timeout edge produce the approved state/effect trace.
5. Rate tests cover global/source exhaustion, refill edges, source-table
   saturation, IPv4/IPv6 scope stability, no connection-reset bypass, bounded
   work, and generic public rejection.
6. Token tests cover initial issue, valid rotation, same principal/session,
   checked generation, duplicate and concurrent replay, expiry, wrong protocol/
   content/session/generation, store full, random failure, digest collision,
   interrupted response, and restart invalidation.
7. Failure before commit preserves the old token; successful rotation makes the
   old token unusable and returns exactly one replacement.
8. Client token storage is move-only, in-memory, bounded, replace-on-success,
   clear-on-expiry/rejection/shutdown, and absent from formatting/equality APIs.
9. Password/token canaries are absent from typed observations, logs, metrics,
   errors, disconnect text, exceptions, canonical values, replay, captures, and
   retained test evidence while encrypted loopback still exchanges them.
10. Focused loopback, state-machine, protocol, fuzz registration, sanitizers,
    target boundaries, baseline provenance, formatting, and repository policy
    checks pass. The full hosted matrix remains the Phase 6 exit gate.

The owner demo should show protected join, rate exhaustion/refill, successful
single-use resume/rotation, a concurrent replay loser, expiry/context rejection,
an interrupted rotation response, and a complete canary scan. Slice 6.3 remains
**Not Started** until this ADR is accepted and production work begins, and
cannot become **Implemented** before demo acceptance.

## Consequences and deferred behavior

- Tokens resume routing/session identity only; ordinary authority, revision,
  idempotency, and command validation remain mandatory.
- No persistent account, valuable reusable credential, player identity,
  canonical replacement, reconnect grace default, or fallback UX is introduced.
- A lost post-rotation response can require a fresh join. Phase 7/10 must decide
  the visible replacement/recovery behavior without making tokens reusable.
- Slice 6.4 may replace provisional caller policy with measured product rate and
  capacity values while preserving these hard ceilings and failure semantics.

## Owner approval

Pending explicit owner approval or amendment of Decisions 1 through 5 and the
proposed acceptance tests.
