# ADR-0005: Transport, encryption, authentication, and session resumption

Status: **Proposed**

Date opened: 2026-08-26

Decision owner: project owner

Needed by: Phase 2

## Decision question

Which maintained transport and cryptographic dependency profile should carry
TES3MP vNext traffic, how will a client authenticate the server and then its own
principal, and how will a disconnected application session resume without
making transport connection state or replayable credentials authoritative?

This must be decided before Phase 3 fixes the transport boundary and before
Phase 6 adds real sockets. The choice affects hostile-input exposure, channel
semantics, queue ownership, supported platforms, operator certificate handling,
reconnect security, telemetry, and dependency maintenance.

It does not decide gameplay authority, the player-visible reconnect grace
period, player replacement behavior, discovery, account providers, or a Steam
service dependency. Those remain owned by ADR-0006, GDR-0001/GDR-0002, and
Phase 21 as applicable.

## Non-negotiable constraints

Every option must satisfy the accepted README, ADR-0002, ADR-0003, and ADR-0004
rules:

1. The Internet path is encrypted and integrity protected. A client
   authenticates the intended server against configured trust material before
   sending an account credential, resume token, or gameplay command.
2. Transport authentication proves an endpoint; application authentication
   produces a minimal owned principal result. Neither grants gameplay authority.
3. Transport-library connection, identity, buffer, clock, error, callback, and
   statistics types remain private to `multiplayer_transport`.
4. Reliable apply-once operations and unreliable latest-wins samples have
   distinct delivery and congestion behavior. A lost reliable operation cannot
   head-of-line block a newer movement or pose sample.
5. Application queues are bounded before calling the library. Library send and
   receive buffers, message sizes, fragment/segment work, rates, timeouts, and
   unauthenticated admission work are also bounded.
6. A latest-wins value is coalesced by stable semantic key before it reaches a
   transport queue. It is never silently promoted to reliable delivery.
7. A reconnect creates a fresh authenticated encrypted transport connection.
   Transport 0-RTT cannot carry credentials, resume commands, apply-once
   commands, or canonical mutations.
8. Application resumption uses a short-lived, revocable, single-use opaque token
   bound to the server, principal, session, protocol/content context, and resume
   generation. Raw reusable tokens are not logged, stored as canonical state,
   or exposed to the server core.
9. Cancellation, callback races, delayed packets, handle reuse, and shutdown
   cannot deliver data to a destroyed or replacement session.
10. Source, transitive dependencies, generated inputs, licenses, build options,
    and update policy are exactly pinned and proven on Windows, Linux, macOS
    arm64, and macOS x86-64. Android ARM64 feasibility is assessed without
    making it a Phase 6 deliverable.
11. No Steam account, Steam Datagram Relay, peer-to-peer signaling, NAT
    traversal, browser stack, or external identity service is required for the
    first dedicated-server vertical slice.
12. The project owns stable disconnect/rejection categories, redaction, and
    telemetry. Library strings are diagnostic inputs, not protocol or UI text.

## Representative scenarios

1. **Known server:** endpoint authentication and encryption finish before the
   application authenticator receives a credential.
2. **Wrong or rotated key:** an on-path peer, wrong host, expired certificate,
   or unexpected key rotation cannot impersonate the configured server.
   Accepting new trust is an explicit action outside the gameplay session.
3. **Two delivery classes under loss:** reliable join/control operations and
   unreliable movement snapshots share one connection. A delayed reliable
   fragment does not prevent a newer snapshot from arriving.
4. **Slow reader and flood:** one peer stops reading while another sends
   oversized messages, excessive small segments, or a sustained flood.
   Per-message, per-peer, admission-stage, and global budgets cap resources.
5. **Reconnect:** a client opens a new encrypted connection and presents its
   current application resume token. The server atomically consumes and rotates
   it. Duplicate, expired, revoked, wrong-context, and old-generation tokens fail
   without canonical mutation.
6. **Delayed old connection:** packets and callbacks from connection A arrive
   after connection B resumed the session. An owned connection generation
   discards A's work; a transport handle is never a durable identity.
7. **Credential failure:** an account provider times out or rejects opaque input.
   Raw credential and resume-token canaries do not appear in logs, metrics,
   captures, disconnect text, or replay.
8. **Configuration failure:** the server has no valid endpoint key,
   certificate, trust configuration, or secure backend. Startup fails closed.
9. **Desktop/VR composition:** desktop and PC VR use one owned API. VR pose is
   merely another unreliable latest-wins sample.
10. **Future Android assessment:** the selected source and crypto backend
    cross-compile for Android ARM64 without OpenMW, Steam, or desktop-only types.

## Options considered

### Option A: GameNetworkingSockets restricted dedicated-server profile (recommended)

Select `GameNetworkingSockets` `v1.6.0` at commit
`2cb93a06350bb065db53abdb0d87cf297e0bfd34`. Use its standalone
message-oriented UDP API, reliable and unreliable modes, a small fixed lane set,
encrypted certificate handshake, queue limits, and connection statistics. Use
the OpenSSL crypto backend and pin the current OpenSSL 3.5 LTS patch plus the
compatible Protocol Buffers dependency in the selection proof.

The restricted profile would:

- build the open-source standalone library, not the Steamworks SDK;
- enable direct dedicated-server IPv4/IPv6 connections only and disable Steam
  authentication, relay, P2P, signaling, ICE/TURN, fake-IP, and WebRTC surfaces;
- make unauthenticated/unencrypted development toggles unreachable from normal
  production configuration;
- supply certificate trust through an owned endpoint-trust interface; the proof
  uses repository test CA/server fixtures, while production issuance, pin
  distribution, and discovery UX remain separately approved operational work;
- keep client account authentication behind a separate asynchronous owned
  boundary after endpoint authentication;
- begin with one reliable ordered control/operation lane and one unreliable
  no-delay/latest-wins sample lane;
- set send bytes, receive bytes/messages, maximum message size, maximum segments
  per packet, send rate, initial/connected timeout, and callback-drain budgets;
- coalesce snapshots before transport submission and evict a peer that cannot
  recover within the approved backpressure policy; and
- bind every callback/message to an owned connection generation.

Why it is recommended:

- it directly provides the required delivery classes, message boundaries,
  fragmentation/reassembly, lanes, congestion control, encryption, queue
  limits, fault simulation, and detailed statistics;
- one encrypted association carries reliable operations and unreliable samples,
  avoiding a second UDP security association;
- it is C++/CMake based, BSD-3-Clause licensed, actively maintained, and has a
  current stable release with Windows, Linux, Apple, sanitizer, and mobile work;
  and
- P2P/Steam/relay features can be excluded behind a small dedicated-server
  adapter surface.

Tradeoffs and risks:

- its certificate format and trust store are GameNetworkingSockets-specific,
  not ordinary X.509/Web PKI. The proof must demonstrate configured
  per-deployment trust without a universal signing secret, Steam, or an invasive
  patch. Failure reopens this ADR;
- OpenSSL and Protocol Buffers enlarge the dependency/update surface;
- callback lifecycle is broad and stateful, so owned generation tests are
  mandatory;
- built-in limits do not replace semantic latest-wins coalescing or global
  server budgets;
- lane priority controls send scheduling, and only reliable messages within one
  lane have a strong order guarantee. Session logic still needs its own
  sequences, revisions, and message semantics; and
- transport reconnect is not application session resumption. vNext still owns
  replay defense, revocation, rotation, and resync.

Primary evidence:

- [feature and security overview](https://github.com/ValveSoftware/GameNetworkingSockets/blob/v1.6.0/README.md)
- [v1.6.0 release](https://github.com/ValveSoftware/GameNetworkingSockets/releases/tag/v1.6.0)
- [connection lanes and lifecycle API](https://github.com/ValveSoftware/GameNetworkingSockets/blob/v1.6.0/include/steam/isteamnetworkingsockets.h)
- [limits, authentication, encryption, and fault controls](https://github.com/ValveSoftware/GameNetworkingSockets/blob/v1.6.0/include/steam/steamnetworkingtypes.h)
- [build requirements and crypto choices](https://github.com/ValveSoftware/GameNetworkingSockets/blob/v1.6.0/BUILDING.md)
- [security-reporting policy](https://github.com/ValveSoftware/GameNetworkingSockets/blob/v1.6.0/SECURITY.md)
- [OpenSSL 3.5 LTS lifecycle and current patch](https://www.openssl-library.org/source/)

### Option B: MsQuic streams plus QUIC datagrams

Pin MsQuic `v2.6.0`, map reliable operations to bounded QUIC streams, and map
samples to the negotiated QUIC DATAGRAM extension. Use TLS 1.3 server
certificates, disable 0-RTT application data, and resume the application above a
fresh QUIC connection.

Advantages:

- standardized QUIC/TLS 1.3 supplies encryption, X.509 endpoint authentication,
  congestion/flow control, migration, streams, and anti-amplification behavior;
- independent streams avoid connection-wide reliable head-of-line blocking,
  while QUIC DATAGRAM supplies secure non-retransmitted samples; and
- MsQuic is actively maintained and MIT licensed, with extensive settings,
  telemetry, sanitizer, and fuzz infrastructure.

Tradeoffs:

- MsQuic's official platform document currently lists Windows and Linux only.
  macOS and Android code exists, but vNext would own support for primary macOS
  targets without an upstream guarantee;
- vNext must add message framing, stream-count/lifetime policy, and careful
  callback/buffer ownership;
- QUIC DATAGRAM is negotiated and may be unavailable; vNext must fail the
  required capability rather than fall back to reliable snapshots; and
- Windows Schannel and non-Windows TLS backends complicate exact cross-platform
  dependency and behavior evidence.

Option B becomes the recommendation if Option A cannot prove project-owned
endpoint trust, or if MsQuic gains sustained official macOS support before
Phase 6.

Primary evidence:

- [streams and unreliable datagrams](https://github.com/microsoft/msquic/blob/v2.6.0/docs/API.md)
- [settings and flow-control limits](https://github.com/microsoft/msquic/blob/v2.6.0/docs/Settings.md)
- [official platform support](https://github.com/microsoft/msquic/blob/v2.6.0/docs/Platforms.md)
- [v2.6.0 release](https://github.com/microsoft/msquic/releases/tag/v2.6.0)
- [QUIC DATAGRAM, RFC 9221](https://www.rfc-editor.org/rfc/rfc9221)

### Option C: ENet plus a project-composed cryptographic session

Pin ENet for reliable UDP channels and compose it with maintained crypto
primitives and a project-owned handshake, certificate/trust, packet protection,
replay window, rekey, and resume protocol.

Advantages:

- ENet is small, MIT licensed, message/channel oriented, portable C, and easy to
  build on the required targets; and
- vNext would directly control wire overhead and channel policy.

Tradeoffs:

- ENet does not provide authenticated encryption or endpoint authentication;
  vNext would own handshake binding, downgrade prevention, nonces, packet
  numbers, replay windows, fragmentation, rekey, key erasure, and resumption;
- adopting primitives is not the same as adopting a reviewed secure transport,
  creating the largest bespoke audit, fuzz, interop, and incident burden; and
- queue, amplification, and unauthenticated admission defenses need more
  project code.

This is not recommended unless both maintained complete transports fail and the
owner explicitly accepts the security engineering cost.

Primary evidence:

- [ENet source, license, and history](https://github.com/lsalzman/enet)
- [ENet packet flags](https://github.com/lsalzman/enet/blob/master/include/enet/enet.h)
- [libsodium key exchange](https://doc.libsodium.org/key_exchange)

## Recommended authentication and resumption boundary

Approving only the library name is not sufficient. The recommendation includes:

1. **Endpoint authentication:** the adapter receives an owned trust policy and
   returns only an `AuthenticatedEndpoint` or stable failure. Missing, expired,
   mismatched, revoked, or untrusted material fails before credentials or
   protocol messages. There is no hidden trust-on-first-use fallback.
2. **Client authentication:** after endpoint authentication and negotiation, the
   client sends one bounded opaque credential to an asynchronous authenticator.
   A transport identity is not a player ID. Fake/real provider and guest/account
   policy remain separately gated.
3. **Transport reconnection:** every reconnect performs a new encrypted,
   endpoint-authenticated handshake. No application command uses 0-RTT.
4. **Application resumption:** the server issues a cryptographically random
   opaque token over the encrypted channel. It binds server instance, principal,
   session, protocol/content context, and a monotonic resume generation; the
   server retains only a keyed verifier and bounded metadata. Successful use
   atomically consumes and replaces it. Expiry duration and player-visible
   replacement behavior remain GDR-0001/GDR-0002 decisions, but expiry is finite
   and the token is revocable.
5. **Authority separation:** endpoint, principal, and resume success establish
   routing/session identity only. Commands still require normal authority,
   revision, idempotency, and semantic validation.

## Proposed acceptance tests

If the owner approves Option A and its complete profile, Slice 2.3 remains
**In Progress** until all of this selection evidence is reviewed:

1. An isolated proof builds exact GameNetworkingSockets, OpenSSL 3.5 LTS patch,
   compatible Protocol Buffers sources, and a tiny owned adapter on Windows
   MSVC 2022, Linux GCC 13/Clang 18, macOS arm64, and scheduled macOS x86-64. It
   also records an Android ARM64 source assessment.
2. A lock records tag/commit/archive hashes, licenses, transitive identities,
   build flags, disabled features, generated policy, vulnerability sources, and
   quarterly plus security-triggered review cadence.
3. Certificate fixtures accept valid trust and reject unknown root, wrong
   identity, expired/not-yet-valid or malformed certificate, revoked trust,
   absent key, and unexpected rotation before credentials are delivered.
4. Tests prove unauthenticated/unencrypted modes cannot be enabled through
   production configuration and downgrade attempts fail closed.
5. Under loss/reordering, reliable operations remain ordered within their lane
   while unreliable samples may drop/reorder and continue past a delayed
   reliable fragment.
6. A saturated latest-wins queue stays bounded, drops/coalesces older samples,
   delivers the newest sample, and never promotes it to reliable delivery.
7. Slow-reader, full-buffer, maximum-message, excessive-segment,
   handshake-flood, and disconnect-flood tests stay inside declared per-peer and
   global memory/work/callback/log budgets.
8. Lifecycle tests cover cancel/reject/close races, unread data, handle reuse,
   delayed callback/packet, replacement connection, and teardown under
   sanitizers where supported.
9. Resume tests cover valid single use/rotation, duplicate concurrent use,
   expiry, revocation, wrong context, old generation, interrupted rotation, and
   delayed old-connection traffic with no partial session attachment.
10. Secret-canary tests inspect logs, metrics, traces, captures, disconnect
    reasons, exceptions, and evidence for endpoint keys, credentials, and tokens.
11. Telemetry tests assert owned per-channel sent/received/dropped/retransmitted/
    queued categories, lifecycle, authentication, rate-limit, and disconnect
    outcomes without exposing library strings as stable API.
12. The proof supplies configured endpoint trust through supported integration
    hooks without a universal CA private key, Steam dependency, or invasive
    upstream patch. Failure reopens the choice.

The proof is disposable evidence. Production wrappers, budgets, authentication
providers, and the resume store remain Phases 4 and 6 work.

## Consequences if approved

- Phase 3 defines product connection, channel, backpressure, event, telemetry,
  and error interfaces without including GameNetworkingSockets.
- Endpoint trust, client principal, application session, connection generation,
  and canonical player identity remain distinct types and lifetimes.
- Movement and pose remain coalesced unreliable samples. Reliable operations
  keep explicit command IDs/revisions despite transport retransmission.
- OpenSSL and Protocol Buffers join the selected transport security/update
  surface and require exact proof evidence for every pin change.
- P2P, Steam, relay, NAT traversal, and ICE require a later ADR update.

## Failure modes and mitigations

- **Encrypted but unauthenticated:** require configured trust and negative trust
  tests; an IP identity or encryption flag is insufficient.
- **Certificate ecosystem cannot serve community servers:** fail the proof and
  reopen the ADR instead of shipping a universal secret or silent TOFU.
- **Reliable traffic starves samples:** isolate lanes, cap backlog, reserve
  measured sample opportunity, and evict a persistently slow peer.
- **Unreliable queue becomes stale:** coalesce before submission and cap in-flight
  sample count/age.
- **Callback after teardown:** bind callbacks to an owned generation and ignore
  stale events before releasing adapter state.
- **Resume replay/race:** keyed verifier, atomic rotation, generation binding,
  finite expiry, revocation, and one writer decide the winner.
- **Secret leakage:** opaque secret types, redacted formatting, canary tests, and
  no raw library diagnostic passthrough.
- **Dependency vulnerability:** exact pins, advisory monitoring, fuzz/sanitizer
  regression, emergency update procedure, and ADR review when required.

## Review and replacement triggers

Reopen this ADR if:

- the owner selects another option or changes the authentication/resume rules;
- endpoint trust requires a universal signing secret, Steam, invasive patch, or
  insecure first connection;
- a supported desktop proof or Android assessment fails;
- reliable traffic blocks required samples despite the bounded lane policy;
- a selected dependency loses support or lacks a timely security fix;
- browser/WebTransport, peer hosting, federation, managed identity, or mandatory
  relay enters scope;
- QUIC support changes enough to remove its current macOS disadvantage; or
- certificate, queue, telemetry, or update burden exceeds an approved budget.

## Owner approval

Pending. No transport, crypto backend, endpoint-trust model, authentication
boundary, or session-resumption mechanism is approved by this proposed record.

The recommendation is Option A with the complete restricted dedicated-server,
endpoint-trust, authentication, resumption, and acceptance-test profile above.
Dependent proof or production work must wait for explicit owner approval.
