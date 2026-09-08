# TES3MP vNext current implementation

- Updated: 2026-09-08
- Code snapshot inspected: `50ae4ef442`
- OpenMW baseline: `f4bec41444214a7903bebd178389ca22ca13f646`

This is the only status and backlog document. “Implemented” means production
code and representative executable tests exist in the named locations. It does
not mean the full game is playable or release-ready. “Partial” names a usable
foundation with known missing behavior. “Not implemented” means no production
package should be inferred from plans, experiments, or historical records.

Code and tests are authoritative when this document becomes stale.

## Executable and library surface

| Surface | Current implementation |
|---|---|
| `tes3mp_protocol` | Strong value types, bounded frames, FlatBuffers codecs, negotiation, authentication messages, reliable operations, canonical snapshots, actors, objects, inventory, and VR pose |
| `tes3mp_transport` | Project-owned connection, channel, queue, lifecycle, reason, and telemetry interfaces |
| `tes3mp_transport_gns` | Private GameNetworkingSockets adapter with c-ares/OpenSSL dependency composition |
| `tes3mp_server_core` | Deterministic authentication, canonical worlds, fixed ticks, command intake/reduction, publication, checksums, lifecycle, resync, and sink boundaries |
| `tes3mp_client_session` | Caller-pumped negotiation, authentication, resume/resync, command output, snapshot ingestion, replication state, and locomotion reconciliation |
| `tes3mp_server` | Configuration/content loading and real-transport dedicated-server composition |
| `tes3mp_headless_client` | Scripted real-transport client for bounded integration scenarios |
| `openmw_tes3mp_adapter` | Shared OpenMW connection, input capture, state application, reconnect, remote presentation, object activation, and inventory integration |

The target graph and include-boundary enforcement live in
[`components/tes3mp/CMakeLists.txt`](../../components/tes3mp/CMakeLists.txt) and
[`cmake/TES3MPVerifyTargetBoundaries.cmake`](../../cmake/TES3MPVerifyTargetBoundaries.cmake).
`components/tes3mp` public headers expose project-owned values and do not expose
OpenMW, renderer, OpenXR, operating-system, FlatBuffers-generated, or
GameNetworkingSockets types.

## Implemented behavior

### Protocol and transport

- A 12-byte bounded frame separates message class and kind before payload
  allocation. Each payload is verifier-checked and semantically validated.
- The production server negotiates protocol major 1, minors 2–3. Defined
  optional capabilities are VR pose (1), actor replication (2), interactive
  objects (3), and inventory (4). Content-manifest mismatch rejects before
  authentication.
- Reliable ordered operations, latest-wins canonical snapshots, and ephemeral
  pose samples use distinct delivery/queue semantics.
- The owned transport boundary supports lifecycle events, bounded outbound
  queues, slow-peer handling, stable disconnect reasons, telemetry, and
  deterministic fault scheduling. The GNS implementation remains private to
  `tes3mp_transport_gns`.

Primary sources: [`protocol_frame.hpp`](../../components/tes3mp/include/tes3mp/protocol_frame.hpp),
[`protocol_handshake.hpp`](../../components/tes3mp/include/tes3mp/protocol_handshake.hpp),
[`protocol_exchange.hpp`](../../components/tes3mp/include/tes3mp/protocol_exchange.hpp),
[`transport.hpp`](../../components/tes3mp/include/tes3mp/transport.hpp), and
[`transport_gns.cpp`](../../components/tes3mp/transport/transport_gns.cpp).

### Authentication, identity, and lifecycle

- Password authentication, per-source/global attempt limiting, random opaque
  resume tokens, disconnect grace, resume, expiration, and authenticated resync
  are composed in the server application.
- A fresh password join receives a 32-byte player credential. The client stores
  the secret; the server atomically stores its SHA-256 digest with stable
  player/entity/appearance and manifest identity. Credential reattachment
  recreates the visible avatar with the same durable identity.
- The identity registry is bounded to 256 records. It is identity persistence,
  not character or world persistence.

Primary sources: [`authentication.hpp`](../../components/tes3mp/include/tes3mp/authentication.hpp),
[`server_authentication.cpp`](../../components/tes3mp/server_core/server_authentication.cpp),
[`server_lifecycle.cpp`](../../components/tes3mp/server_core/server_lifecycle.cpp),
[`player_identity_file.cpp`](../../apps/tes3mp-server/player_identity_file.cpp), and
[`connection_session_coordinator.cpp`](../../apps/tes3mp-server/connection_session_coordinator.cpp).

### Cells, interest, and resynchronization

- A bounded manifest declares typed interior/exterior cell spaces and exact
  allowed cells. Proposed transitions outside the catalog fail closed.
- Active players observe active players in the same exact canonical cell. The
  server derives membership; the client cannot choose interest.
- Join, resume, cell transition, and resync deliver reliable membership
  baselines plus revision-fenced canonical snapshots. A client becomes ready
  only after the required complete lanes arrive.
- OpenMW record names are local adapter mappings and do not enter canonical or
  wire state.

Primary sources: [`content_identity.hpp`](../../components/tes3mp/include/tes3mp/content_identity.hpp),
[`canonical_resync.hpp`](../../components/tes3mp/include/tes3mp/canonical_resync.hpp),
[`interest_projection.cpp`](../../apps/tes3mp-server/interest_projection.cpp), and
[`client_session.cpp`](../../components/tes3mp/client_session/client_session.cpp).

### Player movement and presentation

- Clients send bounded semantic locomotion input. A deterministic server-owned
  fixed-tick kernel is the only producer of canonical player-root movement.
- Manifest-scoped movement profiles define walk/run/sneak/jump parameters.
  Server content collision checks attempted root segments against fixed blocked
  volumes; clients cannot submit collision results.
- Local presentation keeps bounded input history and reconciles by replay.
  Remote presentation uses bounded adaptive playback and canonical locomotion
  state. Reliable one-shot events remain separate.
- Optional root-relative head/hand pose travels on its own latest-wins lane.
  Pose cannot move the canonical root, prove reach, or author gameplay.

Primary sources: [`movement_kernel.hpp`](../../components/tes3mp/include/tes3mp/movement_kernel.hpp),
[`server_command_reducer.cpp`](../../components/tes3mp/server_core/server_command_reducer.cpp),
[`client_locomotion.cpp`](../../components/tes3mp/client_session/client_locomotion.cpp),
[`remote_motion.cpp`](../../apps/openmw/tes3mp/remote_motion.cpp), and
[`content_collision.cpp`](../../apps/tes3mp-server/content_collision.cpp).

### Server-owned actors

- Manifest content creates stable actors with separate actor/prototype/entity
  identities. Actors never receive fake player or session identities.
- The server simulates idle, ordered waypoint travel, and deterministic looping
  wander through the existing movement/collision boundary. Exact cells without
  an active player freeze and retain actor state.
- Capability-gated reliable interest baselines and latest-wins actor snapshots
  drive renderer-only desktop/PC-VR presentation. Clients have no actor
  simulation lease or canonical authority.

Primary sources: [`actor_catalog.hpp`](../../components/tes3mp/include/tes3mp/actor_catalog.hpp),
[`actor_simulation.cpp`](../../components/tes3mp/server_core/actor_simulation.cpp),
[`actor_interest_projection.cpp`](../../apps/tes3mp-server/actor_interest_projection.cpp), and
[`desktop_providers.cpp`](../../apps/openmw/tes3mp/desktop_providers.cpp).

### Interactive objects

- Manifest content defines standard and teleport doors, discrete open/closed
  state, locks, keys, and traps in exact cells.
- Reliable interaction commands validate authenticated session, expected
  revision, canonical player cell/root, bounded reach, object state, and
  server-verified key ownership before an atomic commit.
- Standard doors publish discrete state; OpenMW performs the visual 90-degree
  interpolation and sound locally. Teleport doors request a server-owned player
  cell transition. Trap outcomes are canonical events, not client claims.
- Complete reliable object baselines are scoped by exact-cell interest and used
  for join, transition, resume, and resync.

Primary sources: [`interactive_object_world.hpp`](../../components/tes3mp/include/tes3mp/interactive_object_world.hpp),
[`interactive_object_replication.cpp`](../../components/tes3mp/protocol/interactive_object_replication.cpp),
[`interactive_object_interest_projection.cpp`](../../apps/tes3mp-server/interactive_object_interest_projection.cpp),
and [`desktop_providers.cpp`](../../apps/openmw/tes3mp/desktop_providers.cpp).

### Inventory, equipment, containers, and ground items

- A manifest-scoped item catalog defines prototype category, weight, value,
  condition/charge bounds, equipment masks, stackability, and optional key
  identity. Canonical state owns private player backpacks, 19 equipment slots,
  exact-cell containers, and ground-item stacks.
- Take, put, drop, pickup, equip, and unequip are reliable atomic server
  transactions. Validation includes exact source identity/count, revisions,
  capacity, canonical same-cell/reach, and equipment compatibility. Whole-stack
  moves preserve globally unique identity; splits allocate a new monotonic ID.
- Held canonical keys feed the interactive-object validation context. A claimed
  client key ID is never sufficient.
- Owning clients receive reliable private inventory. Same-cell clients receive
  bounded container/ground views and public equipment snapshots only.
- The OpenMW adapter consumes local pre-mutation inventory actions, sends a
  proposal, and rebuilds presentation from confirmed authoritative state.
  Remote replicated actors receive public equipment presentation only.

Primary sources: [`item_catalog.hpp`](../../components/tes3mp/include/tes3mp/item_catalog.hpp),
[`inventory_world.cpp`](../../components/tes3mp/server_core/inventory_world.cpp),
[`inventory_replication.cpp`](../../components/tes3mp/protocol/inventory_replication.cpp),
[`inventory_interest_projection.cpp`](../../apps/tes3mp-server/inventory_interest_projection.cpp),
and [`desktop_providers.cpp`](../../apps/openmw/tes3mp/desktop_providers.cpp).

## Partial foundations and known limitations

- The OpenMW desktop, scripted headless, and shared PC-VR provider architecture
  exist, but representative PC-VR hardware budget capture remains deferred until
  a headset is available.
- Remote actor/root presentation exists. Articulated remote skeleton head/hands,
  improved pose-loss blending, pose compression hardening, and any pose-assisted
  gameplay reach are not implemented.
- Static blocked-volume collision is a bounded authoritative seam, not OpenMW
  physics parity. It does not model gravity, slopes, dynamic bodies, capsules,
  general pathfinding, or arbitrary cell traversal.
- Actor AI is deliberately limited to idle/travel/wander. There is no navmesh
  parity, schedules, needs, dynamic spawning/removal, or authority delegation.
- Interactive traps publish bounded outcomes but full spell-effect resolution
  does not exist. Lockpicking and probe disarming do not exist.
- Inventory does not include barter/trade, merchant stock/restocking, durability
  damage from combat, or disk persistence.
- The identity file survives restart; canonical world, object, actor, and
  inventory state do not.
- Content is supplied through bounded hand-authored server artifacts. General
  extraction/baking tooling and broad production-world content are unfinished.
- Test and fixture paths demonstrate subsystem behavior, but release-quality
  real-game coverage, packaging, performance budgets, and soak evidence remain
  unfinished.

## Work still required

### Next milestone: combat foundation

The next implementation milestone is discovery and selection of a bounded
combat package covering stats, attacks, damage, magic, death, resurrection, and
respawn. Before production code lands it must identify the relevant OpenMW
seams and specify:

- semantic client intents versus canonical outcomes and presentation events;
- canonical actor/player stat ownership and revision boundaries;
- server-known target, cell, reach, timing, resource, cooldown, and authority
  validation;
- lag policy without trusting client timestamps;
- deterministic damage/effect ordering and atomic death transitions;
- rate abuse, forged targets, impossible reach, stale authority, resource
  bypass, and replayed-command tests; and
- the smallest real OpenMW round trip that proves authoritative behavior.

This milestone should update this section directly. It should not create a
separate discovery report or multi-phase diary.

### Required before the desktop/PC-VR release

1. Combat, stats, magic, death, resurrection, and respawn.
2. Dialogue, journals, quests, factions, reputation, and their durable
   consequences.
3. Canonical time, weather, globals, and durable world-state transitions.
4. A versioned deterministic server-scripting boundary with immutable inputs
   and queued typed commands; the legacy CoreScripts API is not reused.
5. Transactional persistence and deterministic replay for domain state,
   configuration, content identity, script/API versions, seeds, and command
   ordering. TES3MP 0.8.x saves are not migrated.
6. Administration, moderation, discovery, health, metrics, and privileged
   operational interfaces without exposing runtime internals.
7. Broader content/world coverage, content tooling, failure recovery, security
   hardening, packaging, upgrades, cross-platform validation, performance
   budgets, regression assets, and long-running soak evidence.
8. Deferred PC-VR hardware measurement and final desktop/VR interoperability
   closure.

### Optional after the release boundary

Evaluate standalone Quest feasibility against measured performance, dependency,
maintenance, and packaging constraints. A Quest port proceeds only after an
explicit go decision. Device-specific types remain in a provider/platform leaf
and do not enter protocol or canonical state.

## Verification snapshot

The Phase 15 closeout recorded the following on 2026-09-08; these are historical
last-known product results, not a guarantee about later commits:

- standalone MSVC C++20 protocol and server-app runners passed;
- full RelWithDebInfo `openmw` and `openmw_tes3mp_adapter_tests` built and the
  adapter executable passed;
- all 176 repository Python tests passed;
- patch-registry, legacy-exclusion, target-boundary, forbidden-include, and
  indexed baseline-provenance checks passed; and
- provenance accounted for 478 intentional differences and 95 dependency
  declarations.

The documentation consolidation subsequently passed all 177 repository Python
tests and indexed baseline verification with 395 intentional differences and
the same 95 dependency declarations. Product C++ binaries were not rebuilt for
the prose-only consolidation.

Use [DEVELOPMENT.md](DEVELOPMENT.md) for commands and record only the newest
relevant verification here after behavior changes.
