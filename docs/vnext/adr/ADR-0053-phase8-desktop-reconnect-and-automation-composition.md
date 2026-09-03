# ADR-0053: Phase 8 desktop reconnect and automation composition

Status: **Accepted**

Date opened: 2026-09-03

Date approved: 2026-09-03

Decision owner: project owner

Needed by: Phase 8 Slice 8.7

## Decision

The project owner approved Option A on 2026-09-03.

The app-local adapter coordinator becomes a bounded connection supervisor. It
owns one transport and clock, one current client runtime, one rotating
memory-only resume token, the server-granted resume lifetime, and the expected
session generation. It creates sequential runtimes around the same transport;
the reusable client runtime does not gain desktop retry policy and OpenMW gains
no reconnect callback or new hook.

After an established connection closes, the supervisor clears presentation and
starts one resume attempt immediately. At most one attempt is live. A failed
pre-authentication attempt may retry no faster than once per second while its
original deadline remains valid; a submitted token is never replayed after an
uncertain result. Authentication rejection, token expiry, continuity mismatch,
or failure after token submission closes terminally. Fresh join is never an
automatic fallback.

Desktop automation is compiled only with `BUILD_TESTING`. A fixed-role typed
driver uses the existing provider/coordinator boundary, and an external Python
harness launches the real server and two OpenMW processes and captures bounded,
credential-free artifacts. Release builds expose neither driver nor automation
options. ADR-0046 remains the fault authority: deterministic and encrypted
transport layers retain adverse-network coverage; no production fault control
or OS proxy is added.

## Alternatives

Putting retry policy in the shared runtime was rejected as premature general
client policy. Rebuilding the coordinator through a new Engine callback was
rejected because it expands the approved hook surface. OS input/window
automation was rejected as nondeterministic, and adapter-only tests were
rejected because they do not satisfy the real-desktop process gate.

## Acceptance

- only one runtime/attempt exists and retry work remains bounded per frame;
- tokens stay memory-only, rotate on accepted resume, and never enter evidence;
- retry cadence and expiry use the injected monotonic clock;
- a token submitted with an uncertain outcome is not replayed;
- resumed presentation waits for and validates the complete snapshot;
- identity, generation, entity revision, and acknowledgement continuity hold;
- `BUILD_TESTING=OFF` exposes no desktop automation input or option;
- the harness captures two-client flow, reconnect, queue/RSS, and soak evidence;
- existing disabled mode, frame order, shutdown order, target boundaries, and
  patch-registry checks still pass.

## Consequences and review triggers

Reconnect policy remains provisional app-local presentation policy. It adds no
canonical state, protocol field, persistence, gameplay authority, or OpenMW
hook. Reopen before persistent credentials, manual account selection, fresh
join fallback, concurrent paths, background threads, different retry cadence,
or Phase 10 lifecycle policy.

## Owner approval

Approved by the project owner on 2026-09-03: recommended Option A reconnect
ownership, bounded retry behavior, test-only typed driver, external harness, and
the proposed acceptance tests.
