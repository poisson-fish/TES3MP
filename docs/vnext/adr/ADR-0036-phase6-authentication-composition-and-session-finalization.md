# ADR-0036: Phase 6 authentication composition and session finalization

Status: **Proposed**

Date opened: 2026-08-28

Decision owner: project owner

Needed by: Phase 6 Slice 6.3 closure

## Decision question

How should the server session atomically pass typed join/resume input, admission
scope, and resume context through the approved limiter/providers; and how should
initial versus resumed session identity become final before an accepted response
is sent?

The current Phase 4 state machine accepts only untyped secret bytes through one
fixed provider and fixes `SessionGeneration` before the token is inspected.
Sideband configuration would create ordering/staleness paths, while authenticating
outside the state machine would bypass its timeout, cancellation, and stale-
completion contract. Production composition pauses for owner approval.

## Scenarios

1. Open and protected joins pass one global/source gate, compare fixed work, bind
   a newly allocated session, issue a token, and only then send accepted.
2. A resume passes the same gate, consumes once, preserves principal/session,
   installs the checked next generation, and sends the rotated token.
3. Wrong credentials, exhausted limits, full store, crypto failure, timeout,
   cancellation, and duplicate resume cannot partially expose a session.
4. Bind or initial-token issue failure closes the provisional session before a
   public accepted result; no tokenless established mode appears.

## Decision 1: typed session-to-authentication boundary

### Option A: one atomic server-core submission (recommended)

Replace the server state machine's secret-only submission with one move-only
server-core value containing `AuthenticationRequest`, `AdmissionScopeId`, and
`ResumeTokenContext`. A shared server-core authentication service accepts that
value through the existing pollable operation model. The session machine still
owns provider deadline, cancellation, attempt matching, and terminal state.

Tradeoff: the Phase 4 provider input API is narrowed to the real typed server
need and its tests must migrate.

### Option B: configure the existing provider by sideband

Set kind/scope/context on a per-connection provider before submitting bytes.
This minimizes signatures but creates a second mutable ordering and reuse path.

### Option C: authenticate outside the session machine

Let network composition invoke limiter/providers and inject only success/failure.
This avoids changing the provider API but duplicates or bypasses accepted timeout,
cancellation, and stale-completion behavior.

## Decision 2: shared routing and failure policy

### Option A: one process-wide bounded service (recommended)

The service shares the approved global/source limiter and resume store across all
connections. It gates exactly once before routing join to the fixed-work password
provider or resume to atomic token consumption. Denied credentials map to generic
denied; exhausted/untrackable limits, capacity, crypto, and internal failures map
to generic temporarily unavailable. No raw source, credential, store result, or
provider detail reaches public text or observations.

### Option B: one service per connection

This simplifies ownership but resets limiter/store state on reconnect and permits
the bypass ADR-0034 forbids.

### Option C: routing in the transport adapter

This has the source scope nearby but makes transport own application credentials
and conflicts with the accepted target boundary.

## Decision 3: session identity and accepted-response finalization

### Option A: state-machine-owned resume install, composed initial finalization (recommended)

On resume success, the state machine atomically installs the grant's existing
`SessionId` and checked next `SessionGeneration`, and retains its accepted message
for take-once composition. For an initial join, composition allocates and binds a
new session, then asks the shared service to issue its initial token. It sends
accepted only after both steps succeed. Bind/issue failure closes the provisional
session and returns no tokenless established state.

This does not attach a player or canonical entity. The pre-auth attempt generation
continues only to reject stale provider completions; the admitted generation is
the resume grant's checked value.

### Option B: composition installs resume claims through setters

Keep the state machine generic and let composition set session/generation after
success. This exposes partial-install ordering and mismatched-claim states.

### Option C: allow initial sessions without a resume token

Keep play available when issue fails. This weakens the accepted message contract
and creates a user-visible fallback policy not approved for this phase.

## Recommendation and acceptance tests

Approve Option A for all three decisions. Tests should cover typed atomic input,
one gate per attempt, global/source refill and reconnect resistance, open/correct/
wrong join, initial bind then issue ordering, every issue failure, resume identity
and generation install, rotated response take-once behavior, concurrent replay,
timeout/cancellation before consumption, closed public error mapping, no sideband
state, and credential/source canary absence.

## Owner approval gate

Pending explicit owner approval or amendment of Decisions 1 through 3 and the
focused tests. No dependent production composition is authorized yet.
