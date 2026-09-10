# TES3MP vNext durable decisions

This compact ledger preserves non-obvious rules that remain relevant to future
code. Implementation details already enforced by source and tests are not
repeated. Change an entry only through an explicit project decision and update
the affected code/tests in the same milestone.

## Product and compatibility

- **Clean break.** vNext does not preserve TES3MP 0.8.x wire, server API,
  CoreScripts, persistence, RakNet/CrabNet, mixed-peer, save, or patch-set
  compatibility. Legacy code is requirements evidence, not an implementation
  template.
- **Release order.** Windows/Linux/macOS desktop and PC VR share the primary
  release boundary. Standalone Quest is a conditional later product and cannot
  delay that release.
- **Pinned base.** The active engine base is OpenMW 0.51.0 commit
  `f4bec41444214a7903bebd178389ca22ca13f646`. Differences and patch ownership are
  maintained by machine registries rather than narrative provenance.
- **Modpack is the compatibility unit.** A server selects one immutable,
  manifest-addressed modpack. The ordered exact bytes of every gameplay plugin
  and any resource that can affect canonical outcomes belong to that identity;
  additional client content is allowed only when explicitly classified as
  presentation-only. Canonical state is derived from load-order winners, not
  from a preferred source plugin. Unsupported records or scripts fail baking or
  remain explicitly non-canonical; running them locally never grants authority.
  Packs reference third-party content and do not imply permission to redistribute it.
- **Multiplayer game entry.** A menu connection does not load a client-authored
  OpenMW save. After authentication and the complete initial baseline, OpenMW
  runs its stock new-game initialization and the adapter then applies the
  server-owned root. The local profile stores a username-bound credential root;
  the offered durable player credential is derived from that root and the
  normalized server endpoint so one server's identity is never intentionally
  offered to another. Plaintext profile passwords are not retained.
- **Server-owned chargen with stock presentation.** Freshness is explicit
  state (`NewCharacter`, `CreatingCharacter`, or `EstablishedCharacter`), never
  inferred from the current entity set. OpenMW retains the prison-ship dialogue,
  UI, and animation sequence. Confirmed intermediate choices remain live-only;
  the complete profile and post-boat canonical root become durable together
  only after stock chargen exits. A process restart or credential reattachment
  of an incomplete character deliberately restarts from the fresh pre-chargen
  checkpoint. Only established profiles restore a saved canonical root.

## Authority and state

- **Single canonical writer.** The server alone commits durable gameplay state.
  Clients, rendering, physics presentation, VR tracking, scripts, and admin
  tools submit typed intent or consume immutable results.
- **Atomic failure.** Decode, validation, admission, reducer, lifecycle,
  persistence, and publication failures cannot partially commit a logical
  operation. Cross-domain operations either publish together or not at all.
- **Stable identity.** Durable entities use typed, nonzero, manifest-scoped
  identities and revisions. IDs are not recycled after committed allocation.
  Player, actor, item, object, session, and connection identity are distinct.
- **Revision is not time.** Canonical revisions/state versions describe committed
  state ordering; server ticks describe simulation time. Neither substitutes
  for the other.
- **Interest is server-owned.** Current visibility is exact canonical-cell
  membership. A client cannot select its own interest set or use presentation
  pose to expand gameplay reach.
- **No actor delegation yet.** Canonical actor simulation stays server-owned.
  Any future delegation requires a measured decision, finite epoch-bound lease,
  complete atomic handoff, revocation, and deterministic server fallback.
- **Disconnect checkpoint.** Resume-token recovery keeps the same live canonical
  session. Credential reattachment of an established character supersedes the
  hidden principal/session binding and preserves its current canonical root;
  reattachment after expiration or restart uses its last atomic server-side
  checkpoint. Reattachment advances entity revision and authority epoch,
  rebases the spatial tick, and clears velocity. Incomplete chargen is the
  deliberate exception: it restarts at the pre-chargen safe point.
- **Character save clean break.** Identity file V5 stores either a fresh
  pre-chargen identity with no root or an established complete profile with its
  canonical root. It cannot represent a half-created durable character. V1–V4
  identity files are rejected without migration.
- **Character bootstrap transaction.** Chargen completion binds the established
  profile, post-boat root, starting inventory/equipment, and derived combat
  state in one prepared operation. Inventory and combat record the profile
  revision that initialized them, making resume/reattach idempotent and
  preventing a different profile from silently replacing live state. The
  operator combat template retains resolver modifiers and the weight-unit
  scale; confirmed character attributes and skills override player-specific
  fields.

## Protocol, transport, and security

- **Bound before allocation.** Frame, schema, count, byte, numeric, enum, queue,
  and rate limits are checked before allocation or mutation. Invalid inputs fail
  closed with stable owned errors.
- **Owned public types.** FlatBuffers, GameNetworkingSockets, OpenSSL, OpenMW,
  OpenXR, platform, and renderer types remain behind their private adapters.
- **Separate traffic semantics.** Reliable apply-once operations carry identity
  and revision context. Canonical sampled state is latest-wins. VR pose is
  ephemeral latest-wins presentation. Queues may not silently substitute one
  class for another.
- **Capability negotiation.** Additive optional domains are negotiated explicitly.
  Required-capability, protocol-range, and exact content-manifest mismatches
  reject before gameplay admission.
- **Baked content identity.** A production content manifest identifies the
  ordered exact bytes of the resolved OpenMW loadout together with normalized
  canonical server catalogs and complete client record mappings. Baked packs
  are immutable and manifest-addressed; publication advances a small atomic
  pointer rather than rewriting a live pack in place.
- **Authentication separation.** Routing principals, process-local resume tokens,
  durable player credentials, sessions, and canonical player identity have
  separate lifetimes. Servers store player-credential digests, not reusable
  client secrets.
- **Transport trust is limited.** The current direct-IP profile requires
  GameNetworkingSockets encryption against passive observation but does not
  authenticate server endpoint identity. It has no project CA, certificate
  management, or private trust-store patch; active endpoint impersonation is an
  explicitly accepted first-release risk until a later security decision.
- **No secrets in evidence.** Credentials, resume tokens, passwords, and
  unfiltered user data do not enter logs, metrics, fixtures, captures, or
  retained debugging artifacts.
- **Backpressure is correctness.** Outbound queues are bounded. Admission of a
  required multi-message canonical result is atomic, and slow peers receive a
  stable policy outcome rather than causing unbounded memory growth.

## Simulation and presentation

- **Semantic controls.** Desktop and VR produce the same semantic gameplay
  commands. Fork-specific OpenXR tracking stays in the provider leaf.
- **Client-authoritative player movement.** Server-side Bullet physics and
  collision simulation for player movement are deferred. The client is authoritative
  over its own character's 3D position, orientation, velocity, and locomotion mode.
  The server ingests client transform proposals, records them into canonical state,
  and relays them to peers; server movement simulation kernels are skipped for
  client-authoritative players. Intra-cell client presentation does not override
  local physics with server snapshots.
- **Prediction is presentation.** Local input replay and correction cannot create
  new input. Remote interpolation, door animation, actor animation, sounds, and
  pose degradation do not change canonical outcomes.
- **Historical melee contact.** A melee claim is checked against bounded
  server-captured player and actor roots at the client's observed server tick.
  Stock base distance, canonical weapon reach, exact-cell membership, and
  server collision occlusion decide contact; missing history fails closed.
- **Freeze inactive cells.** Current actors, objects, and inventories retain
  canonical in-memory state while their exact cell has no active player; they do
  not simulate client-owned background results.
- **Private inventory.** A player's backpack is delivered only to its owner.
  Other clients receive scoped container/ground views and public equipment, not
  another player's private contents.

## Future systems

- **Scripting.** Server scripts receive immutable bounded values/events and queue
  typed commands for deterministic ticks. They never receive packet buffers or
  direct mutable canonical references. Callback and generated-command ordering
  must be replay-stable. Reusing Lua does not imply legacy API compatibility.
- **Persistence.** Persist canonical domain records, not protocol payloads,
  OpenMW pointers, renderer objects, or transport types. Define the durability
  acknowledgement point and atomically bind replay to configuration, content,
  script/API versions, deterministic seeds, and command ordering.
- **Administration.** Public health and privileged operational detail remain
  separate. Administrative APIs do not expose scripting/runtime internals.
- **Quest isolation.** A future Quest implementation reuses the platform-neutral
  protocol/client core. Android, headset, and OpenXR device concepts remain in a
  platform/provider layer.

## Decision process

Add a new entry only when a choice is expensive to reverse or changes
architecture, authority, durable state, compatibility, security, persistence,
or player-facing semantics. Record the chosen rule and the reason in at most a
short paragraph. Options analysis and review dialogue stay in the issue or Git
history; routine choices stay in code and tests.
