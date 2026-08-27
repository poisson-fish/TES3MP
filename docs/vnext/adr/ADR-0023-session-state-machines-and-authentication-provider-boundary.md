# ADR-0023: Session state machines and authentication-provider boundary

Status: **Proposed**

Date opened: 2026-08-27

Decision owner: project owner

Needed by: Phase 4 Slice 4.3

## Decision questions

Where do the client and server session state machines live; which states,
events, and transition effects are public; how does the server invoke an
asynchronous authentication provider without callback lifetime hazards; how is
opaque authentication material bounded and redacted; where does the Phase 4
in-memory boundary stop before Phase 6 wire credentials and resumption; and how
do timeout, cancellation, stale completion, and illegal-transition behavior fail
closed?

These are architecture, identity, lifecycle, and security decisions. Production
session or authenticator code must not land until the project owner approves the
options below.

## Recommendation summary

Recommend Option A for Decisions 1 through 6:

1. put explicit role-specific event/action state machines in the existing
   `tes3mp_client_session` and `tes3mp_server_core` targets, without changing the
   ADR-0014 target graph;
2. require an explicit encrypted-transport-ready event before negotiation and
   use typed in-memory authentication events in Phase 4, leaving password/resume
   wire schemas to Phase 6;
3. use a pollable, session-owned asynchronous authentication operation rather
   than callbacks or a synchronous provider;
4. transfer one move-only, fixed-capacity, 256-byte opaque authentication value
   to the provider and return only a minimal nonzero principal identity or a
   closed rejection;
5. use injected monotonic time and a validated caller-supplied three-stage
   timeout policy, with each duration between 1 millisecond and 120 seconds; and
6. use closed enum/numeric transition outcomes and typed observability, with
   illegal or stale input causing no partial state change and no secret-bearing
   diagnostics.

This is a proposal, not approval. It implements already accepted ADR-0003,
ADR-0005, ADR-0014, ADR-0015, ADR-0017, ADR-0020, and ADR-0022 constraints. It
does not select a real password provider, resume-token representation, network
credential schema, release timeout defaults, player identity, gameplay
authority, canonical state scope, or replacement/reconnect behavior.

## Existing constraints

1. Authentication input occurs only after the composition layer reports an
   encrypted transport and version/capability negotiation succeeds.
2. Direct-IP transport is not endpoint-authenticated. No state or result may
   claim that the intended server identity was cryptographically verified.
3. Authentication establishes a minimal routing principal only. It grants no
   gameplay authority and creates no player or canonical entity.
4. Credentials stop at the provider. Session state may own an opaque value only
   long enough to transfer it exactly once and must not understand its format.
5. Timeout and cancellation use injected monotonic time; wall-clock time and
   provider callback timing cannot decide canonical state.
6. One session may retain at most one authentication operation and one bounded
   input. All inputs, events, actions, and diagnostics are owned values.
7. Delayed completions from an old attempt, session generation, or terminal
   state cannot establish a session.
8. Real join-password handling, rate limiting, encrypted transport integration,
   resume tokens, and credential wire encoding remain Phase 6 work under
   ADR-0005.
9. Session establishment remains outside canonical mutation. Phase 5 and later
   slices still validate every command, identity, revision, and authority epoch.
10. No OpenMW, selected transport-library, platform, test-support, or
    FlatBuffers-generated type may enter the public state-machine/provider APIs.

## Representative scenarios

1. The client and server receive encrypted-ready events, negotiate the current
   minor, submit an empty opaque input to an open-server fake provider, and both
   reach `Established` only after provider success.
2. A credential submission arrives before encrypted-ready or before successful
   negotiation. The transition fails, the provider is not invoked, and state is
   unchanged.
3. A provider returns `Pending`, then completes with a minimal principal. The
   server accepts exactly the matching attempt and generation.
4. Authentication is rejected. Both roles enter a terminal rejected state with
   a stable category and no credential, provider text, or partial identity.
5. Negotiation, authentication-input, or provider work crosses its configured
   deadline. The session times out once and cancels an active provider operation
   once.
6. The caller cancels while authentication is pending. A later completion for
   that attempt is stale and cannot resurrect the session.
7. Duplicate hello, authentication, completion, timeout, cancel, and close
   events exercise every illegal/terminal transition without partial mutation.
8. An oversized authentication value is rejected before the provider sees it.
   Empty input and exact 256-byte input remain valid interface values.
9. A secret canary is used as authentication material. State snapshots,
   transition errors, metrics, structured events, stream formatting, and test
   failure output contain no canary bytes.
10. An authenticated principal attempts a later command for another player.
    The session result supplies routing identity only; ordinary authority checks
    remain required and are outside this slice.

## Decision 1: state-machine ownership and transition model

### Option A: role-specific event/action machines in existing targets (recommended)

Implement the client machine in `tes3mp_client_session` and the server machine
in `tes3mp_server_core`. Each has a closed role-specific state enum, accepts one
owned typed event at a time, and returns a closed transition result plus bounded
effects for its composition layer. A legal transition commits atomically only
after all event validation succeeds; an illegal transition leaves the previous
state unchanged.

The server machine consumes protocol-owned negotiated values but does not depend
on transport or OpenMW. The client machine may continue using the existing
protocol/transport abstractions. This retains ADR-0014's target topology and
keeps state-machine behavior independently testable.

### Option B: add a new `tes3mp_server_session` target

Separate server connection/session lifecycle from the future canonical server
core. This is a clean conceptual split, but it reopens ADR-0014's exact target
set, dependency allowlist, aliases, boundary tests, and downstream build graph
before an actual coupling problem has appeared.

### Option C: one symmetric state machine in `tes3mp_protocol`

Share one machine and parameterize its role. This reduces types but places
authentication and lifecycle policy inside the codec library and makes invalid
client/server states representable.

## Decision 2: Phase 4 ordering and wire boundary

### Option A: explicit encrypted-ready prerequisite and in-memory auth events (recommended)

Model transport readiness, negotiation, authentication, establishment,
rejection, cancellation, and closure explicitly. Authentication material may be
submitted to the Phase 4 server machine only after an
`EncryptedTransportReady` event and successful negotiation. The in-memory test
composition delivers that typed value directly to the state-machine boundary.

Do not allocate new message-kind IDs or credential/resume FlatBuffer schemas in
Slice 4.3. Phase 6 owns the real encrypted transport mapping, join-password and
resume-token messages, rate limiting, and network redaction behavior. It must
adapt those decoded values into the same Phase 4 event without weakening the
ordering contract.

### Option B: add authentication wire messages in Slice 4.3

Reserve message IDs and schemas now. This makes the in-memory exchange resemble
the eventual network path, but prematurely chooses password/resume framing,
rate-limit interaction, and rejection behavior assigned to Phase 6.

### Option C: treat successful negotiation as authentication

Establish open-server sessions immediately after `ServerHello`. This avoids an
extra stage but cannot exercise the required provider boundary and would make
later protected servers a state-machine redesign.

## Decision 3: asynchronous provider and completion ownership

### Option A: pollable session-owned operation (recommended)

`AuthenticationProvider::begin` receives a validated attempt identity, session
generation, and moved opaque input, then returns one owned operation. The
operation reports either pending or one terminal minimal result when polled and
supports idempotent cancellation. The server session owns at most one operation
and destroys it only after cancellation or terminal completion.

The attempt identity and generation accompany every result. Polling avoids a
provider retaining a pointer or callback into destroyed session state, while
still allowing immediate fake providers and future asynchronous backends.

### Option B: provider callback into the session

Pass a completion callback/sink to the provider. This is conventional but
requires shared lifetime, executor/thread, reentrancy, callback-after-destroy,
and cancellation ordering rules before Phase 4 otherwise needs them.

### Option C: synchronous authentication only

Return the final result directly from `authenticate`. This is small but permits
a slow provider to block the session/composition loop and makes the later async
boundary a breaking redesign.

## Decision 4: opaque material and minimal result

### Option A: fixed-capacity move-only secret and principal-only result (recommended)

Define a move-only authentication-material value containing 0 through 256
bytes. It has no copy, equality, stream, string, generic diagnostic, or public
serialization operation. Move and destruction clear owned storage on a
best-effort basis; tests prove state and diagnostic values cannot expose it.
The provider receives it exactly once. Empty material represents a provider
that requires no submitted credential.

A successful provider result contains one new nonzero `PrincipalId` strong
identity and no roles, player ID, session ID, authority grant, provider claims,
or display data. Rejection is a closed category such as invalid input, denied,
provider unavailable, or cancelled, with no provider text in the stable result.

### Option B: provider-defined type-erased credential objects

Let each provider allocate an arbitrary private credential object. This offers
backend flexibility but weakens uniform byte/work limits, redaction auditing,
and deterministic fake-provider tests.

### Option C: strings and extensible claim maps

Pass strings/maps through session state. This is convenient but lets backend
format, unbounded allocation, secrets, account concepts, and authority-like
claims escape the provider boundary.

## Decision 5: deadlines, cancellation, and stale completions

### Option A: injected time with bounded caller-supplied policy (recommended)

Use the existing injected `MonotonicClock`. A validated policy supplies separate
transport/negotiation, authentication-input, and provider-operation durations.
Each must be between 1 millisecond and 120 seconds. Slice 4.3 defines no release
default; the Phase 6/7 composition chooses reviewed values within those bounds.
Deadline addition is checked for overflow.

A timeout or caller cancellation is terminal, produces one bounded action, and
cancels an active provider operation at most once. A completion is accepted only
for the active attempt and session generation while authentication is pending;
all others are stale and cannot change the state.

### Option B: compile-time timeout defaults

Hard-code release-like values now. This is easy to test but selects operator and
player-visible network timing before the real transport and vertical-slice
conditions exist.

### Option C: provider/session-embedding timeouts

Let each provider and transport decide its own timeout. This fragments lifecycle
behavior and prevents the state machine from proving bounded abandoned work.

## Decision 6: errors, terminal behavior, and observability

### Option A: closed enum/numeric results with atomic transitions (recommended)

Expose closed state, event, effect, rejection, timeout-stage, cancellation, and
transition-error enums with bounded numeric context only. A failed transition
returns the unchanged state and no externally visible effect. Terminal reject,
cancel, timeout, and close events are idempotent; no later input can establish
the session.

Add typed metric/event keys for transition outcomes, stage timeouts,
authentication outcomes, stale completions, and cancellation. Dimensions are
closed low-cardinality values. No event accepts free-form text, raw bytes,
provider errors, addresses, principal IDs, or authentication material.

### Option B: exceptions and diagnostic strings

Throw on illegal transitions and propagate provider text. This makes atomicity,
allocation, redaction, and stable test assertions depend on every caller.

### Option C: silently ignore invalid or stale events

This keeps the API small but hides lifecycle defects and attacks, and makes
timeout/cancellation behavior difficult to diagnose without packet logging.

## Proposed acceptance tests and demo

1. Exhaustive state/event matrix tests prove every legal transition and prove
   every illegal transition leaves state and effects unchanged.
2. Authentication cannot begin before encrypted-ready and successful protocol
   negotiation; neither failure path invokes the provider.
3. Immediate and delayed fake-provider success establish both role machines and
   retain the immutable negotiated version/capability set.
4. Provider denial, unavailable, malformed-input, timeout, cancellation, and
   close paths enter the correct terminal state without partial identity.
5. Empty, exact-256-byte, and 257-byte material cases enforce the approved bound
   before provider invocation.
6. Poll, cancel, completion, and destruction tests prove at most one operation,
   exactly-once cancellation, and no callback/session lifetime dependency.
7. Wrong-attempt, wrong-generation, duplicate, delayed-after-cancel, and
   delayed-after-close completions never establish or resurrect a session.
8. Manual-clock tests cover each stage at deadline-minus-one, exact deadline,
   and checked-addition overflow.
9. Secret canaries are absent from public state, transition results, effects,
   metrics, structured events, stream output, and test failure diagnostics.
10. A provider result carries only `PrincipalId`; compile-time/public-header
    checks exclude gameplay authority, player/canonical state, OpenMW,
    FlatBuffers-generated, selected transport, platform, and test-support types.
11. Deterministic in-memory tests reproduce the same transition/effect trace
    under identical event order and manual-clock values.
12. All standalone C++ contracts, repository policy tests, runtime-safety
    registration, baseline provenance, legacy exclusion, JSON, Markdown-link,
    formatting, and staged-diff gates remain green.

The implementation demo should show a successful open-provider path, delayed
success, denial, each timeout stage, cancellation plus stale completion, an
illegal transition with unchanged state, exact secret bounds, and a canary scan.
Owner demo acceptance remains required before Slice 4.3 becomes
**Implemented**.

## Consequences of the recommendation

- Phase 4 gains deterministic lifecycle behavior without selecting a real
  transport, password encoding, resume token, player identity, or gameplay
  authority rule.
- Polling adds an explicit pump step but removes session callback ownership and
  reentrancy hazards.
- A 256-byte interface cap is intentionally small and uniform. A future
  authenticator that genuinely needs larger structured input must reopen this
  decision rather than silently expanding pre-authentication work.
- Existing target topology remains intact, but the server core owns both
  pre-canonical session admission and later canonical behavior as separate
  modules. Evidence of coupling would trigger reconsideration of a server-
  session target.
- Phase 6 must add wire messages and adapt them into this interface without
  weakening encrypted-before-secret, timeout, generation, and redaction rules.

## Failure modes and mitigations

- **Credential sent too early:** the server accepts authentication material only
  after encrypted-ready and successful negotiation states.
- **Callback after teardown:** the recommended provider exposes no callback;
  session-owned polling and cancellation define the lifetime.
- **Stale completion establishes a replacement session:** attempt identity,
  session generation, pending state, and terminal-state checks all must match.
- **Slow provider stalls admission forever:** explicit provider deadline and
  idempotent cancellation bound retained work.
- **Secret copied into state or diagnostics:** move-only fixed storage, one-way
  transfer, no formatting, typed observability, and canary scans enforce the
  boundary.
- **Authentication confused with authority:** the result contains only a
  principal identity and every later command still requires normal authority.
- **Target coupling grows:** public-header and CMake boundary checks remain; a
  need for server-core-to-transport linkage reopens target placement.
- **Phase 4 accidentally defines Phase 6 protocol:** no auth/resume kind or
  schema lands in this slice under the recommendation.
- **Timeout arithmetic wraps:** policy validation and checked deadline addition
  fail before changing state.

## Review and replacement triggers

Reopen this ADR if:

- server session lifecycle requires a direct transport dependency or materially
  couples to canonical reducers;
- a callback/executor model becomes necessary and can prove teardown safety;
- a provider needs more than one concurrent operation or more than 256 bytes;
- persistent accounts, third-party identity, roles, administrative claims, or
  valuable reusable credentials enter scope;
- release testing shows the 120-second hard stage ceiling is insufficient;
- resume/replacement behavior requires additional states before its GDR; or
- any secret-canary, stale-completion, timeout, or atomic-transition test fails.

## Owner approval

Pending. The project owner has not yet selected Decisions 1 through 6.

Approval of this ADR would not approve a real authentication backend, network
credential schema, join-password rate limits, resume-token behavior, player or
canonical identity, gameplay authority/state scope, or release timeout defaults.
