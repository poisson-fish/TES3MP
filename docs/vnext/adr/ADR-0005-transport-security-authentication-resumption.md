# ADR-0005: Transport, encryption, authentication, and session resumption

Status: **Accepted**

Date opened: 2026-08-26

Date approved: 2026-08-26

Date amended: 2026-08-26

Decision owner: project owner

Needed by: Phase 2

## Decision summary

TES3MP vNext will use standalone GameNetworkingSockets `v1.6.0` at commit
`2cb93a06350bb065db53abdb0d87cf297e0bfd34` for direct dedicated-server
connections. The selected profile uses GameNetworkingSockets' automatic basic
encryption and integrity protection in its supported unauthenticated direct-IP
mode. It does not require Steam, a certificate authority, operator-managed
certificates, trust anchors, certificate pinning, revocation lists, a private
GameNetworkingSockets API, or a maintained upstream patch.

The initial application authenticator is an optional shared server join
password. A client may submit it only after the transport reports an encrypted
connection as established. The password is a server-access secret, not a
general-purpose account credential, and operators and players must not reuse a
valuable password. A static `hash(password)` is not sent as a credential because
it would be replayable and would merely become a password equivalent.

Every successful join receives a cryptographically random, short-lived,
single-use application resume token. Reconnect always creates a fresh encrypted
transport connection before presenting the token. Token expiry, consumption,
replacement, and invalidation are automatic server behavior; they require no
operator key or certificate management.

This decision intentionally accepts one bounded risk for the first community
server milestone: without authenticated endpoint identity, an active on-path
attacker can impersonate a server and obtain a join password or resume token.
The selected transport protects against casual/passive eavesdropping and packet
modification after its handshake, but it does not prove that the remote endpoint
is the intended server. This limitation must be documented accurately and must
not be represented as authenticated transport security.

## Decision history

The owner first approved a restricted GameNetworkingSockets profile with
configured per-server endpoint trust. A source audit found no supported public
runtime API for that model in `v1.6.0`, so the ADR was reopened before dependency
or production work. The considered remedies were a maintained upstream patch,
a project-operated certificate authority, or a different transport.

After reviewing the operational cost and actual first-milestone needs, the owner
rejected those remedies as out of scope and approved the simpler profile above.
The audit remains retained as useful evidence in the
[endpoint-trust integration assessment](../proofs/gamenetworkingsockets/TRUST_INTEGRATION_ASSESSMENT.md),
but its failed gate no longer applies to the amended security objective.

## Approved dependency proof profile

On 2026-08-26 the owner approved dependency Option A for the selected-library
proof:

- GameNetworkingSockets `v1.6.0` at commit
  `2cb93a06350bb065db53abdb0d87cf297e0bfd34`;
- OpenSSL `3.5.8` LTS at commit
  `f4dc4d58b48d346a8270183f89acf826d459b0ca`;
- Protocol Buffers C++ `6.33.4` from release `v33.4` at commit
  `edaa823d8b36a8656d7b2b9241b7d0bfe50af878`; and
- its pinned local Abseil `20250512.1` dependency at commit
  `76bb24329e8bf5f39704eb10d21b9a80befa7c81`, with Protobuf's bundled
  `utf8_range` source.

The exact source-archive and license hashes are part of the repository lock.
The proof builds upstream sources without a carried patch or dependency-manager
resolution. It uses static libraries, the OpenSSL crypto backend, and disables
shared libraries, Steam services, P2P, relay, ICE, WebRTC, examples, upstream
tests, and tools. Windows additionally uses the static MSVC runtime and
OpenSSL's `no-asm` configuration. A pin, crypto backend, or restricted-profile
change requires the ADR review described below.

## Why this decision is needed now

Phase 3 needs stable owned transport and session boundaries, and Phase 6 needs a
maintained implementation that carries reliable operations and unreliable
latest-wins samples without exposing library types. The project also needs an
explicit answer for password handling and reconnect before those concerns are
accidentally embedded in gameplay or protocol code.

This ADR selects transport and initial security/session behavior. It does not
decide gameplay authority, the visible reconnect grace period, player
replacement behavior, discovery, persistent player accounts, administration,
or Steam integration. Those remain later ADR/GDR decisions.

## Approved constraints

1. Internet traffic uses GameNetworkingSockets encryption and integrity
   protection. Production configuration cannot enable unencrypted transport.
2. The initial direct-IP connection is not endpoint-authenticated. Code,
   documentation, logs, UI, and telemetry must not claim otherwise.
3. The standalone open-source library is used without Steam authentication,
   relay, P2P signaling, ICE/TURN, fake-IP, WebRTC, or Steam account services.
4. No vNext certificate service, operator-managed transport certificate,
   trust-anchor store, pinning workflow, revocation list, private upstream API,
   or carried GameNetworkingSockets source patch is introduced.
5. Transport-library connection, identity, buffer, clock, error, callback, and
   statistics types remain private to `multiplayer_transport`.
6. Authentication remains an owned application interface. A transport identity
   is not a principal, player ID, session ID, or grant of gameplay authority.
7. The optional initial credential is a bounded shared server join password. It
   is sent only after the encrypted transport connection is established and is
   never logged, persisted by vNext, reflected in an error, or placed in replay,
   metrics, captures retained as evidence, or canonical state.
8. A static password hash is never accepted as a network bearer credential.
   Failed join attempts are rate-limited, comparison avoids secret-dependent
   early exit, and temporary credential buffers are released promptly.
9. Reliable apply-once operations and unreliable latest-wins samples have
   distinct delivery and congestion behavior. A delayed reliable operation
   cannot head-of-line block a newer movement or pose sample.
10. Application queues and library buffers are bounded. Message size,
    fragment/segment work, rates, timeouts, admission work, and callback draining
    receive explicit budgets.
11. Reconnect creates a new encrypted connection. Application resumption uses a
    random, finite-lifetime, single-use opaque token bound to the server process,
    principal/session, protocol/content context, and resume generation.
12. Successful token use atomically consumes and replaces the token. Expired,
    duplicate, wrong-context, and old-generation tokens fail without canonical
    mutation. Server restart may invalidate all outstanding initial-milestone
    tokens.
13. Cancellation, delayed packets, callback races, handle reuse, and shutdown
    cannot deliver data to a destroyed or replaced application session.
14. Exact sources, transitive dependencies, generated inputs, licenses, build
    options, and update policy are pinned and proven on Windows, Linux, macOS
    arm64, and macOS x86-64. Android ARM64 feasibility is assessed separately.
15. Stable owned disconnect/rejection categories, redaction, and telemetry are
    required. Raw library strings are diagnostic inputs, not protocol or UI
    contracts.

## Representative scenarios

1. **Open server:** GameNetworkingSockets establishes an encrypted connection;
   the absent join password is handled by the application authenticator and the
   client proceeds to session creation.
2. **Password-protected server:** the client completes the encrypted connection
   before sending a bounded join password. The server validates it, rate-limits
   failures, emits no secret-bearing diagnostics, and returns only a minimal
   authentication result to session code.
3. **Passive observer:** captured traffic does not expose the join password,
   resume token, application messages, or canonical state.
4. **Active impersonator:** an on-path attacker may terminate separate encrypted
   connections and impersonate the server. This is an explicitly accepted
   limitation, not a passing endpoint-authentication test.
5. **Two delivery classes under loss:** reliable join/control operations and
   unreliable movement snapshots share one connection. A delayed reliable
   fragment does not prevent delivery of a newer snapshot.
6. **Slow reader and flood:** oversized messages, excessive small segments,
   sustained traffic, abandoned handshakes, and a stalled reader stay inside
   per-message, per-peer, admission-stage, and global budgets.
7. **Reconnect:** a new encrypted connection presents the current application
   resume token. The server atomically consumes and rotates it; duplicate,
   expired, wrong-context, and old-generation uses fail.
8. **Delayed old connection:** callbacks and packets from connection A arrive
   after connection B resumed the session. An owned generation rejects A's work.
9. **Credential failure:** password and token canaries do not appear in logs,
   metrics, traces, errors, replay, or retained captures.
10. **Crypto/configuration failure:** the required crypto backend is unavailable
    or unencrypted mode is requested. Production startup or connection setup
    fails closed.

## Options considered

### Option A: standalone GameNetworkingSockets with automatic basic encryption

This is the approved option. Pin GameNetworkingSockets `v1.6.0`, use its
message-oriented direct-IP API, reliable and unreliable delivery, a small fixed
lane set, automatic encrypted handshake, queue controls, and connection
statistics. Use the OpenSSL backend and a compatible pinned Protocol Buffers
dependency in the selection proof.

Advantages:

- encryption is automatic for clients and server operators;
- no certificate lifecycle, central service, dependency fork, or custom crypto
  protocol is required;
- the library directly provides the message boundaries and delivery classes the
  game needs; and
- it is C++/CMake based, BSD-3-Clause licensed, cross-platform, and maintained.

Tradeoffs:

- default encryption prevents casual eavesdropping but does not authenticate
  the intended server, so active man-in-the-middle attacks remain possible;
- a shared join password must be treated as a low-value server-specific secret;
- OpenSSL and Protocol Buffers enlarge the dependency/update surface; and
- vNext still owns semantic queues, generations, application authentication,
  replay defense, resumption, telemetry, and stable errors.

Primary evidence:

- [feature and security overview](https://github.com/ValveSoftware/GameNetworkingSockets/blob/v1.6.0/README.md)
- [public API statement on default encryption and MITM limitations](https://github.com/ValveSoftware/GameNetworkingSockets/blob/v1.6.0/include/steam/isteamnetworkingsockets.h)
- [v1.6.0 release](https://github.com/ValveSoftware/GameNetworkingSockets/releases/tag/v1.6.0)
- [limits and transport controls](https://github.com/ValveSoftware/GameNetworkingSockets/blob/v1.6.0/include/steam/steamnetworkingtypes.h)
- [build requirements and crypto choices](https://github.com/ValveSoftware/GameNetworkingSockets/blob/v1.6.0/BUILDING.md)
- [OpenSSL source and support lifecycle](https://www.openssl-library.org/source/)

### Option B: authenticated GameNetworkingSockets endpoint certificates

Use per-server trust anchors through a new upstream API patch, or operate a
vNext certificate authority compatible with the library's existing model.

This could defend against active endpoint impersonation. It was rejected because
`v1.6.0` has no suitable supported public runtime trust API, a patch would become
security-sensitive maintenance, and a central CA would impose certificate
issuance, key custody, rotation, availability, and operator-support obligations
far beyond the first community-server milestone.

### Option C: change to a transport with Web-PKI endpoint authentication

Use MsQuic or another TLS/QUIC transport and ordinary server certificates.

This provides standardized endpoint authentication but makes certificates and
server-name/discovery policy part of setup, and the reviewed MsQuic release does
not officially cover all primary vNext platforms. It is not justified for the
approved initial threat scope.

### Option D: password-authenticated key exchange

Add a reviewed PAKE such as OPAQUE or SPAKE2 so a password-protected server and
client can establish mutual proof without certificates or revealing a reusable
password.

This is operationally attractive if valuable per-user accounts enter scope, but
it adds another security-sensitive protocol and dependency. It is deferred
rather than improvised from hashes, nonces, or ad hoc challenge-response logic.

## Approved authentication and resumption boundary

1. **Transport connection:** establish GameNetworkingSockets encryption. The
   result is an `EncryptedConnection`, not an authenticated server identity.
2. **Protocol negotiation:** exchange only bounded non-secret version,
   capability, and content context required before authentication.
3. **Join authentication:** pass one optional bounded password to an asynchronous
   owned authenticator. The initial provider compares it to the operator's
   configured shared join password and returns a minimal principal result or a
   stable rejection category.
4. **Session creation:** create a distinct application session and random resume
   token only after successful authentication. Transport handles and addresses
   never become player or durable session identity.
5. **Resumption:** after a fresh encrypted reconnect and compatible negotiation,
   atomically consume and rotate the application token. Exact expiry duration
   and player-visible replacement behavior remain GDR decisions.
6. **Authority separation:** connection, password acceptance, principal, and
   resume success establish routing/session identity only. Every command still
   requires ordinary authority, revision, idempotency, bounds, and semantic
   validation.

Persistent player accounts, email identities, valuable reusable account
passwords, administrative credentials, and third-party identity providers are
not approved by this ADR. Adding any of them requires a new owner-reviewed
authentication decision.

## Approved acceptance tests

Slice 2.3 remains **In Progress** until its disposable selection proof retains
and passes this evidence:

1. Exact GameNetworkingSockets, OpenSSL, and compatible Protocol Buffers sources
   plus a tiny owned adapter build on Windows MSVC 2022, Linux GCC 13/Clang 18,
   macOS arm64, and scheduled macOS x86-64. Android ARM64 source feasibility is
   recorded.
2. A dependency lock records tag, commit, archive hashes, licenses, transitive
   identities, build flags, disabled features, generated policy, vulnerability
   sources, and update cadence.
3. A client and server connect in the supported unauthenticated mode with
   encryption active. Requests for unencrypted production operation fail.
4. A passive packet capture containing password and resume-token canaries does
   not reveal either canary. The proof makes no endpoint-authentication claim.
5. Authentication ordering tests prove no credential or resume token is emitted
   before the encrypted connection is established and compatible negotiation
   completes.
6. Password tests cover absent, correct, incorrect, oversized, repeated, timed
   out, cancelled, and redaction cases. Static password-hash bearer credentials
   are not a supported mode.
7. Under loss and reordering, reliable operations remain ordered within their
   lane while unreliable samples may drop/reorder and continue past a delayed
   reliable fragment.
8. A saturated latest-wins queue stays bounded, coalesces older samples,
   delivers the newest sample, and never promotes it to reliable delivery.
9. Slow-reader, full-buffer, maximum-message, excessive-segment,
   handshake-flood, authentication-flood, and disconnect-flood tests stay inside
   declared memory, work, callback, and log budgets.
10. Lifecycle tests cover cancellation, rejection, close races, unread data,
    handle reuse, delayed callback/packet, replacement connection, and teardown
    under sanitizers where supported.
11. Resume tests cover valid single use and rotation, duplicate concurrent use,
    expiry, server invalidation, wrong context, old generation, interrupted
    rotation, and delayed old-connection traffic without partial attachment.
12. Secret-canary tests inspect logs, metrics, traces, retained captures,
    disconnect reasons, exceptions, and evidence for join passwords and tokens.
13. Telemetry tests assert stable owned per-channel and lifecycle categories
    without exposing raw library strings or secrets as stable API.

The proof is selection evidence, not the production wrapper. Production
interfaces, budgets, authentication provider, and resume store remain Phases 4
and 6 work.

## Consequences of the approved decision

- Server owners configure, at most, an optional join password. They do not
  generate, obtain, install, rotate, or revoke transport certificates.
- Clients receive encryption automatically but no cryptographic proof of server
  identity. UI and documentation must not display a misleading secure-identity
  claim.
- Players must use a server-specific join password and must not reuse an account,
  email, storefront, or administrator password.
- Phase 3 defines owned connection, channel, backpressure, event, telemetry, and
  error interfaces without including GameNetworkingSockets types.
- Movement and pose remain coalesced unreliable samples. Reliable operations
  keep command IDs and revisions despite transport retransmission.
- OpenSSL and Protocol Buffers remain in the selected dependency proof and
  update surface.
- Steam, P2P, relay, authenticated discovery, persistent accounts, and
  administrative authentication require later owner-approved decisions.

## Failure modes and mitigations

- **Passive credential capture:** require encrypted connection state before
  credentials and verify with packet-capture canaries.
- **Active server impersonation:** explicitly accepted for the first milestone;
  warn against password reuse and reopen the decision if valuable credentials or
  stronger endpoint identity enter scope.
- **Replayable password hash:** do not implement static hash-as-credential
  authentication. Use the password only inside the encrypted channel.
- **Password guessing:** bound input, rate-limit failures, avoid detailed
  rejection differences, and permit operators to disable password access.
- **Reliable traffic starves samples:** isolate lanes, cap backlog, preserve
  sample opportunity, and evict a persistently slow peer.
- **Unreliable queue becomes stale:** coalesce before submission and cap
  in-flight sample count and age.
- **Callback after teardown:** bind callbacks to an owned generation and ignore
  stale events before releasing adapter state.
- **Resume replay or race:** random finite-lifetime token, atomic single-use
  rotation, generation/context binding, and one writer decide the winner.
- **Secret leakage:** opaque secret types, redacted formatting, canary tests, and
  no raw diagnostic passthrough.
- **Dependency vulnerability:** exact pins, advisory review, sanitizer/fuzz
  regression, emergency update procedure, and ADR review when required.

## Review and replacement triggers

Reopen this ADR if:

- persistent player accounts, valuable reusable credentials, administrative
  credentials, payments, or private player data enter the transport flow;
- authenticated server discovery, verified server identity, federation, or
  cross-server trust becomes a product requirement;
- an incident or demonstrated attack makes the accepted active-MITM exposure
  unacceptable;
- Steam authentication, relay, P2P, NAT traversal, or browser transport is
  proposed;
- GameNetworkingSockets loses required maintenance, platform, encryption,
  delivery, lifecycle, or bounded-resource properties;
- the selected dependency set cannot pass the approved platform and fault proof;
  or
- exact dependency pins, crypto backend, or restricted feature profile changes.

Normal GameNetworkingSockets/OpenSSL/Protocol Buffers patch updates require the
dependency review and proof policy. They require an ADR amendment only when they
change a decision above.

## Owner approval

The project owner approved the amended Option A on 2026-08-26 after reviewing
its automatic encryption, shared join-password handling, automatic resume-token
behavior, lack of operator certificate work, lack of upstream patches, and
explicit active-man-in-the-middle limitation.

The owner separately approved the exact dependency proof profile recorded above
as Option A on 2026-08-26.

The approval rejects the prior trust-anchor patch and certificate-authority
paths. It does not approve persistent player accounts, administrative
credentials, discovery identity, gameplay authority, reconnect grace duration,
or player replacement behavior.
