# ADR-0003: Hostile-internet threat model

Status: **Accepted**

Date opened: 2026-08-26

Date approved: 2026-08-26

Date amended: 2026-08-26

Decision owner: project owner

Needed by: Phase 2

## Decision summary

The project owner approved Option A on 2026-08-26. TES3MP vNext assumes a
hostile Internet and fully untrusted clients, including authenticated clients.
The dedicated server remains the only durable canonical-state writer. Transport
and application authentication properties are selected explicitly by ADR-0005;
neither a connection nor an authenticated principal receives gameplay authority
merely by existing.

On 2026-08-26 the owner approved an ADR-0005 exception for the first community
server milestone: transport encryption protects against passive observation but
does not authenticate the intended server, so active endpoint impersonation is
an accepted residual risk. This exception does not weaken hostile-input,
canonical-authority, resource-bound, redaction, replay, or session-lifecycle
requirements. Valuable account or administrative credentials remain out of
scope and trigger review of both ADRs.

The initial trusted computing base is the dedicated-server host and operating
system, the released server binary and pinned dependencies, and the operator's
validated configuration and secret sources. The server operator is trusted to
own and administer the world. Every remote client, client process, network path,
packet, command, content claim, reconnect claim, and later administrative
request is untrusted until the responsible boundary authenticates, authorizes,
bounds, and validates it.

This ADR selects a threat posture, not a schema, codec, transport, identity
provider, authority policy, or gameplay rule. Those choices remain gated by
ADR-0004 through ADR-0007 and later GDRs.

## Why this decision is needed now

Phases 3 through 7 create the first protocol, session, server-core, transport,
and headless multiplayer implementation. Their interfaces and tests need one
shared answer to questions such as:

- whether authenticated clients may be trusted to send valid state;
- where credentials and authorization claims may appear;
- which inputs need byte, allocation, collection, queue, and work limits;
- whether replay, duplication, reordering, and stale authority are adversarial
  cases or merely adverse-network cases;
- what must fail before canonical mutation; and
- which risks are explicitly deferred instead of accidentally ignored.

The model follows the repository's authoritative-server and bounded-protocol
direction. It also follows the security-property separation and Internet threat
categories described by [RFC 3552](https://www.rfc-editor.org/rfc/rfc3552), the
data-flow and trust-boundary approach in the
[OWASP Threat Modeling Cheat Sheet](https://cheatsheetseries.owasp.org/cheatsheets/Threat_Modeling_Cheat_Sheet.html),
and the design-review and risk-tracking practices in
[NIST SP 800-218](https://csrc.nist.gov/pubs/sp/800/218/final).

## Security objectives and protected assets

In priority order for the first vertical slice, vNext protects:

1. **Canonical-state integrity:** only validated, authorized commands may cause
   atomic server-state transitions; clients cannot directly write durable state.
2. **Process and host safety:** network input must not produce memory corruption,
   unsafe code execution, path traversal, uncontrolled process invocation, or
   access to host secrets.
3. **Availability and bounded resources:** one peer or input must not create
   unbounded bytes, allocations, objects, work, queues, logs, snapshots, or
   retained session state.
4. **Identity and session integrity:** within the connected server's trust
   boundary, authentication, authorization, session resume, command identity,
   revision, and authority epoch cannot be confused, forged, replayed, or
   transferred implicitly. Initial clients do not receive cryptographic proof
   that the contacted endpoint is the intended server.
5. **Credential and private-data confidentiality:** reusable credentials,
   secret-source values, session-resume secrets, and private operational data
   must not enter packet-independent core state, diagnostics, metrics, replay,
   persistence, or client-visible errors.
6. **Protocol and evidence integrity:** negotiated versions/capabilities,
   canonical changes, audit events, metrics, and replay inputs must describe
   what actually occurred without trusting client-provided labels.
7. **Recovery:** disconnect, malformed input, dependency failure, and detected
   divergence must fail closed or converge through an explicit bounded recovery
   path without partial durable mutation.

Game content and public server metadata are not assumed confidential. Traffic
analysis resistance and hiding that a client is connected are not initial
objectives.

## Trust boundaries and data flows

```text
untrusted client process / OpenMW adapter
                  |
          hostile network path
                  |
        transport adapter boundary
    encryption, peer/session plumbing,
      channel limits, backpressure
                  |
      protocol + session boundary
 complete bounded decode, negotiation,
 authentication result, state machine
                  |
       authoritative server core
 deterministic validation, authorization,
 atomic mutation, canonical revisions
          /          |           \
  script API   persistence API   metrics/admin APIs
  (future)       (future)          (future)
```

Rules at these boundaries are:

- Transport-library connection, identity, buffer, and error types stop at the
  transport adapter.
- Authentication credentials stop at the authentication provider. Protocol and
  server-core code may receive only an owned, validated principal/session result
  with the minimum claims required for authorization.
- A successfully authenticated client remains untrusted for message shape,
  ordering, rate, state values, content identity, command intent, and authority.
- Protocol decoding completes into bounded temporary values before a session or
  reducer receives a message.
- The server core is the single canonical writer. Scripting, persistence,
  replay, metrics, and administration use typed interfaces and cannot receive a
  mutable canonical-state view or packet buffer.
- Client-side prediction and presentation never become evidence that a
  canonical mutation occurred.

## Trusted and untrusted actors

### Trusted for the initial vertical slice

- The dedicated-server host, operating system, process isolation, and local
  filesystem permissions.
- The project-released server executable and dependencies whose versions and
  provenance satisfy the accepted build policy.
- The operator who selects configuration, secret sources, content, and future
  script bundles for their own server.
- Correctly implemented cryptographic primitives in the later selected transport
  and security dependencies.

Trust does not remove defensive checks between server components. A defect in a
trusted component remains in scope for tests, fuzzing, sanitizers, and failure
isolation.

### Untrusted or potentially hostile

- Unauthenticated and authenticated clients, including modified clients.
- All network packets, timing, ordering, duplication, loss, addresses, and
  disconnect behavior.
- Client-supplied names, IDs, transforms, velocities, cell claims, revisions,
  acknowledgements, capabilities, timestamps, and content claims.
- Authentication attempts and opaque provider inputs before validation.
- Resume, retry, idempotency, and delegated-authority claims.
- Future script commands, persistence records loaded from storage, admin
  requests, discovery inputs, and external metrics consumers at their ingress
  boundaries.
- Logs, diagnostics, and replay records when later imported from outside the
  process; they are evidence, not authority.

## Attacker capabilities

The design assumes an attacker can:

- inspect, generate, truncate, corrupt, replay, duplicate, delay, reorder, and
  flood traffic from one or many client processes;
- modify or replace the client executable and bypass every client-side check;
- authenticate legitimately and then act maliciously;
- open, abandon, and resume connections repeatedly; race commands; guess IDs;
  send stale revisions/epochs; and exploit wraparound or ordering edge cases;
- advertise unknown, contradictory, or oversized protocol capabilities;
- send values containing invalid encodings, non-finite numbers, extreme
  coordinates, unexpected content identities, or pathological collections;
- attempt to amplify CPU, memory, bandwidth, storage, log volume, or downstream
  sink work with small inputs;
- cause adverse-network conditions intentionally and observe externally visible
  timing and error behavior; and
- steal or misuse a user's credential outside vNext and present it to the owned
  authentication boundary.

The attacker is not assumed able to defeat sound cryptography, compromise the
dedicated-server host/OS, replace the released server binary without operator
action, or compel a trusted operator to disclose secrets.

## Scenarios the threat model must cover

1. **Malformed framing:** an unauthenticated peer sends truncated, oversized,
   recursively shaped, or allocation-amplifying input. Decode fails within
   declared byte/work budgets and no session or canonical object is partially
   created.
2. **Authenticated cheating:** a valid principal sends impossible movement,
   another player's ID, an unauthorized cell transition, or a fabricated state
   snapshot. Authentication supplies identity only; normal command authority and
   validation reject the mutation.
3. **Replay and stale state:** an attacker replays an apply-once command, old
   resume proof, acknowledgement, revision, sequence, or authority epoch. The
   receiver rejects or idempotently recognizes it without repeating effects.
4. **Resource exhaustion:** peers flood handshakes, reliable operations,
   latest-wins samples, reconnect attempts, or expensive invalid commands.
   Per-input, per-peer, per-stage, and global budgets bound retained work and
   produce observable backpressure/disconnect outcomes.
5. **On-path interference:** a passive observer cannot read application traffic,
   join passwords, or resume tokens. ADR-0005 explicitly accepts active server
   impersonation for the first community-server milestone; protocol code must
   not claim endpoint authentication or invent a second ad hoc cryptographic
   layer.
6. **Credential disclosure attempt:** credentials or resume secrets appear in a
   malformed input or provider failure. Structured errors, logs, metrics,
   captures, replay, and disconnect reasons redact them.
7. **Authority confusion:** delayed traffic arrives after disconnect, reconnect,
   resync, or delegated-authority handoff. Connection identity, session identity,
   command IDs, revisions, and authority epochs prevent the old sender from
   mutating current state.
8. **Slow or failed consumer:** a client, script sink, persistence sink, metrics
   sink, or admin consumer stalls. It cannot hold the canonical writer or create
   an unbounded queue; the selected failure policy is explicit and tested in its
   owning phase.
9. **Administrative abuse:** an unauthenticated or insufficiently privileged
   principal attempts a future admin command, while a valid privileged operator
   performs a destructive action. Phase 21 must enforce authentication,
   authorization, least privilege, audit, and reason fields; prevention of a
   trusted world owner's deliberate action is not promised.
10. **Dependency or configuration failure:** a required security dependency,
    secret source, content manifest, or configuration is absent, mismatched, or
    corrupt. Startup or the affected operation fails closed with actionable,
    non-secret diagnostics.

## Options considered

### Option A: hostile Internet and untrusted clients (approved)

Treat the network and every client as hostile, keep the server host/operator in
the initial trusted computing base, and add least-privilege typed boundaries for
future subsystems. This matches the authoritative-server product direction and
keeps the first implementation testable without pretending to defend against a
compromised host.

### Option B: cooperative authenticated clients

Trust authenticated clients to send structurally valid or partially
authoritative state. This reduces early validation work but makes a modified or
compromised client capable of corrupting shared state and conflicts with the
accepted single-writer, bounded-input direction.

### Option C: hostile server host and operator

Attempt to protect world state and credentials even from the machine or operator
running the server. Meaningful protection would require remote attestation,
external trust roots, encrypted-state policies, and a different operational and
administrative model. It would not protect availability from the host and would
block the first vertical slice without satisfying its primary risks.

## Required mitigations and owning phases

| Threat | Required control | Owning phase(s) |
|---|---|---|
| Malformed/oversized input | Complete bounded decode; byte, string, collection, allocation, depth, and work budgets; structured errors | 3–4 |
| Spoofing/on-path tampering | Maintained encrypted transport; explicit initial active-impersonation exception; owned application-authentication boundary; no library-type leakage | 2, 4, 6 |
| Replay/duplication/stale authority | Command IDs, bounded idempotency windows, sequences, revisions, session binding, authority epochs | 3–7 |
| Unauthorized mutation | Server-only canonical writer; explicit command authorization and validation; atomic reducers | 4–5 |
| Resource exhaustion | Per-stage/per-peer/global quotas, latest-wins coalescing, bounded queues, backpressure, timeouts, disconnect reasons | 3–7 |
| Secret disclosure | Opaque credentials, explicit secret sources, redacted structured logging/metrics/errors, negative tests | 3–6, 21 |
| Numeric/state abuse | Checked numeric types, finite/range/normalization rules, deterministic validation before mutation | 3–5 |
| Slow/failing sinks | Immutable committed records, bounded asynchronous delivery, explicit fail-closed/degraded policy per sink | 5, 19–21 |
| Supply-chain/configuration risk | Exact dependency identities, integrity/license evidence, validated configuration, explicit update review | 1–2, 21–22 |
| Administrative abuse | Separate authenticated principals, least-privilege roles, authorization, immutable audit events, private-by-default exposure | 20–21 |

No later ADR may weaken these controls implicitly. A required exception must
reopen this ADR, identify affected assets/scenarios, and receive owner approval.

## Required verification

Later implementation slices must make the approved scenarios executable. At a
minimum, the combined Phase 3–7 evidence must include:

- unit and property tests for all declared bounds and checked value types;
- corpus-backed fuzz targets for every decoder and negotiation surface;
- tests proving complete decode/validation occurs before session or canonical
  mutation;
- authenticated-client tests showing identity does not grant state authority;
- replay, duplicate, stale revision, stale sequence, stale session, and stale
  epoch tests with apply-once assertions;
- seeded latency/loss/jitter/duplication/reordering/stall/disconnect simulations;
- per-peer and global flood/backpressure tests with bounded queue, memory, work,
  and log assertions;
- secret-canary tests across errors, logs, metrics, captures, replay, and
  disconnect reasons;
- sanitizer-backed malformed-input and lifecycle suites; and
- metrics and structured-event assertions for rejection category, budget
  exhaustion, rate limiting, authentication failure, resync, and disconnect,
  without reusable credentials or raw secret-bearing payloads.

Phase 21 must add separate administrative authentication, authorization,
confused-deputy, revocation, audit, and exposure tests. Phases 19 and 20 must add
script and persistence isolation/failure tests before those boundaries become
trusted production dependencies.

## Explicitly deferred or out of scope

- Compromise of the dedicated-server host, OS, hypervisor, operator account, or
  released server binary is outside the initial protection boundary.
- Volumetric distributed denial of service beyond application-level admission,
  amplification avoidance, rate limiting, and bounded resource use requires
  deployment/network mitigation and is not promised by the server process.
- Traffic-flow confidentiality, anonymity, anti-correlation, and hiding server
  participation are not initial requirements.
- Client anti-cheat, DRM, memory inspection prevention, and protection of game
  assets on a player's machine are out of scope. Server-side validation remains
  mandatory regardless.
- Non-repudiation of player or operator actions is not promised. Audit integrity
  and attribution within the server's trusted boundary remain Phase 21 work.
- Security compatibility with TES3MP 0.8.x credentials, protocol, config,
  scripts, persistence, or admin surfaces is explicitly out of scope.
- A sandbox capable of containing arbitrary native code selected by the server
  operator is not promised. Phase 19 must separately decide the scripting
  runtime, API, limits, and failure isolation.

Deferral does not authorize an insecure placeholder. A deferred surface remains
absent or inaccessible until its owning phase supplies an approved boundary.

## Consequences

- Protocol and server-core work carries validation and resource-budget costs
  from its first message rather than adding them after gameplay expands.
- Client-side checks improve usability but never substitute for server checks.
- The approved transport security properties do not decide gameplay authority
  or canonical ownership. Initial encryption does not authenticate the intended
  server.
- Interfaces must carry explicit validated identities, revisions, epochs, and
  bounded values instead of ambient connection or engine state.
- Some attacks can still deny service within finite budgets. The product promises
  bounded degradation and recovery, not perfect availability against unlimited
  distributed resources.
- Future scripting, persistence, and administration phases cannot expose direct
  mutable-state or packet-buffer shortcuts even to operator-selected extensions.

## Review and replacement triggers

Reopen this ADR if:

- the project offers managed hosting or promises protection from a server
  operator, host, or infrastructure administrator;
- peer hosting, client authority, listen servers, federation, or cross-server
  trust is proposed;
- a selected schema, transport, authentication provider, scripting runtime,
  persistence store, admin interface, or discovery service cannot satisfy a
  required boundary or mitigation;
- credentials, personally identifying data, voice, payments, or other protected
  data enter scope;
- a security incident or test demonstrates that an attacker capability or asset
  is missing; or
- a later GDR requires client-originated durable authority that conflicts with
  the server-only canonical writer.

## Owner approval

Approved by the project owner in the 2026-08-26 working session: Option A,
hostile Internet and fully untrusted clients with the dedicated-server
host/operator as the initial trusted boundary.

The approval does not select a schema, codec, transport, authentication backend,
subsystem authority policy, OpenMW hook surface, or gameplay behavior. Those
remain separate owner-gated decisions.
