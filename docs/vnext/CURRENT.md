# TES3MP vNext current implementation

- Updated: 2026-09-09
- Code snapshot inspected: `vnext` working tree based on `8a954860e0`
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
| `tes3mp_protocol` | Strong value types, bounded frames, FlatBuffers codecs, negotiation, authentication and character-profile messages, reliable operations, canonical snapshots, actors, objects, inventory, melee combat, and VR pose |
| `tes3mp_transport` | Project-owned connection, channel, queue, lifecycle, reason, and telemetry interfaces |
| `tes3mp_transport_gns` | Private GameNetworkingSockets adapter with c-ares/OpenSSL dependency composition |
| `tes3mp_server_core` | Deterministic authentication, canonical worlds, fixed ticks, command intake/reduction, publication, checksums, lifecycle, resync, and sink boundaries |
| `tes3mp_client_session` | Caller-pumped negotiation, authentication, resume/resync, command output, snapshot ingestion, replication state, and locomotion reconciliation |
| `tes3mp_server` | Configuration/content loading and real-transport dedicated-server composition |
| `tes3mp_headless_client` | Scripted real-transport client for bounded integration scenarios |
| `openmw_tes3mp_adapter` | Shared OpenMW connection, stock-chargen confirmation bridge, state application, reconnect, remote presentation, object activation, inventory integration, and authoritative melee capture/presentation |

The target graph and include-boundary enforcement live in
[`components/tes3mp/CMakeLists.txt`](../../components/tes3mp/CMakeLists.txt) and
[`cmake/TES3MPVerifyTargetBoundaries.cmake`](../../cmake/TES3MPVerifyTargetBoundaries.cmake).
`components/tes3mp` public headers expose project-owned values and do not expose
OpenMW, renderer, OpenXR, operating-system, FlatBuffers-generated, or
GameNetworkingSockets types.

Root product presets build only the shipping OpenMW client and TES3MP dedicated
server. The headless client, focused TES3MP checks, standalone contracts, and
full upstream OpenMW baseline are separate opt-in build scopes; the Windows
wrapper selects the bounded product scope by default.

## Implemented behavior

### Protocol and transport

- A 12-byte bounded frame separates message class and kind before payload
  allocation. Each payload is verifier-checked and semantically validated.
- The production server negotiates protocol major 1, minors 2–3. Defined
  optional capabilities are VR pose (1), actor replication (2), interactive
  objects (3), inventory (4), combat (5), and character creation (6). Content-manifest mismatch rejects
  before authentication. The production server does not yet offer combat (5).
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
- The identity registry is bounded to 256 records. Its V4 atomic file stores
  only a fresh pre-chargen identity or a complete established profile paired
  with its canonical root. Intermediate chargen state and movement are never
  written. Credential reattachment restores established state with a new
  authority epoch; an incomplete character instead restarts from the fresh
  pre-chargen checkpoint even when replacing a hidden grace-period session.
  V1–V3 identity files are rejected. Broader world persistence is not yet
  present.

Primary sources: [`authentication.hpp`](../../components/tes3mp/include/tes3mp/authentication.hpp),
[`server_authentication.cpp`](../../components/tes3mp/server_core/server_authentication.cpp),
[`server_lifecycle.cpp`](../../components/tes3mp/server_core/server_lifecycle.cpp),
[`player_identity_file.cpp`](../../apps/tes3mp-server/player_identity_file.cpp), and
[`connection_session_coordinator.cpp`](../../apps/tes3mp-server/connection_session_coordinator.cpp).

### Main-menu entry and player continuation

- An explicitly configured multiplayer build exposes a stock-main-menu
  Multiplayer dialog accepting a DNS name, IPv4 address, bracketed IPv6
  address, `host:port`, or `tes3mp://` URI. Credentials are stored per endpoint.
- The dialog includes bounded password entry. Host starts the packaged
  dedicated-server configuration/content, waits for an explicit readiness
  signal, reports captured startup errors, then connects through the same
  client path. The child is stopped with the client.
- Network/session admission is pumped while the main menu remains active, but
  no world provider is called before a game exists. Once the complete initial
  baseline is ready, OpenMW runs its normal new-game startup and the adapter
  applies an authoritative root only for established characters. Fresh and
  incomplete characters retain stock new-game intro placement. Existing
  command-line automation remains available.

Primary sources: [`mainmenu.cpp`](../../apps/openmw/mwgui/mainmenu.cpp),
[`multiplayerdialog.cpp`](../../apps/openmw/mwgui/multiplayerdialog.cpp),
[`client_connection.cpp`](../../apps/openmw/tes3mp/client_connection.cpp), and
[`player_identity_file.cpp`](../../apps/tes3mp-server/player_identity_file.cpp).

### Authoritative character creation

- Authentication and join results explicitly classify each identity as
  `NewCharacter`, `CreatingCharacter`, or `EstablishedCharacter` and carry the
  canonical profile revision. Entity presence is not a freshness signal.
- The packaged vanilla profile contains distinct stock pre-chargen Imperial
  Prison Ship and post-boat Seyda Neen safe-point transforms, plus bounded
  playable race/appearance, class, birthsign, spell, and starting-item declarations.
  Its appearance allowlist is the exact stock race-dialog-selectable set, and
  sex uses the same female-0/male-1 encoding as the protocol and OpenMW bridge.
  Startup validates its manifest, cell, and collision occupancy.
- Typed reliable name, race/appearance, predefined/custom class, birthsign, and
  completion commands carry expected profile and canonical revisions. The
  server validates record IDs and phase and computes attributes, skills,
  spells, and starting inventory/equipment. Intermediate confirmations remain
  live-only. When stock `CharGenState` reaches `-1`, the completion command
  atomically persists the complete profile with the configured post-boat root
  and publishes the matching canonical spatial revision.
- The focused OpenMW bridge intercepts each stock chargen choice, waits for
  confirmation, projects it through existing player mechanics, and advances
  the normal UI. Gameplay input and authoritative presentation are suppressed
  during chargen. After completion, the bridge waits for a newer safe-point
  snapshot before applying the root, so a stale boat snapshot cannot teleport
  the player backward. A process restart or credential reattachment restarts
  incomplete chargen; established players skip it and restore their saved root.

Primary sources: [`character_profile.hpp`](../../components/tes3mp/include/tes3mp/character_profile.hpp),
[`character_creation_protocol.cpp`](../../components/tes3mp/protocol/character_creation_protocol.cpp),
[`character_content.cpp`](../../apps/tes3mp-server/character_content.cpp), and
[`charactercreation.cpp`](../../apps/openmw/mwgui/charactercreation.cpp).

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

### Authoritative melee combat foundation

- OpenMW's normal player attack animation and contact selection remain the
  capture path. At the native hit key, a negotiated combat session suppresses
  local mutation and sends only attack type/strength, optional actor identity,
  last observed server tick, and expected combat revisions. Client hit rolls,
  damage, resources, targets, and wall-clock time are not accepted as outcomes.
- A shared engine-independent resolver is also used by OpenMW's hit chance,
  weapon damage, hand-to-hand damage, and fatigue helpers. The server owns the
  PRNG, attacker fatigue, actor health/fatigue/dead state,
  cooldown, and separate combat revisions. Damage and death commit atomically
  with command finalization.
- Optional bounded `TES3MP_COMBAT_V1` content supplies manifest-scoped resolver
  settings, a validated default player combat profile, exhaustive actor combat
  seeds, weapon records, and a deterministic random seed. Startup rejects a
  missing, malformed, mismatched, or internally inconsistent configured file
  before exposing any of its state.
- Fresh joins atomically initialize inventory and canonical combat state from
  that server profile and current carried weight; reattach/resume preserves the
  existing combatant. The carried-right inventory stack selects server weapon
  data and skill. Its canonical condition receives wear and breakage, including
  automatic unequip, in the same prepared commit as fatigue, damage, death, and
  command finalization. Relevant inventory/equipment changes advance the combat
  revision and recompute normalized encumbrance.
- Server validation rejects unknown or cross-cell targets, stale revisions,
  future/expired source ticks, missing contact history, failed authoritative
  contact/reach, and rate abuse before mutation. Existing session generation,
  command sequence/ID, entity binding, authority epoch, and canonical revision
  checks reject stale authority and replay.
- Private self fatigue/revision and same-cell actor stats replicate through a
  latest-wins snapshot. Same-cell hit/death facts use a reliable event batch.
  OpenMW applies confirmed fatigue, actor health/fatigue, native dead state, and
  deterministic death/resurrection presentation to renderer-only actors.

Primary sources: [`melee_combat.cpp`](../../components/tes3mp/protocol/melee_combat.cpp),
[`combat_world.cpp`](../../components/tes3mp/server_core/combat_world.cpp),
[`combat_content.cpp`](../../apps/tes3mp-server/combat_content.cpp),
[`combat_replication.cpp`](../../components/tes3mp/protocol/combat_replication.cpp),
[`combat_interest_projection.cpp`](../../apps/tes3mp-server/combat_interest_projection.cpp),
and [`character.cpp`](../../apps/openmw/mwmechanics/character.cpp).

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
- Inventory does not include barter/trade, merchant stock/restocking, disk
  persistence, or repair commands.
- The dedicated server can load and compose the combat bootstrap but
  deliberately does not advertise capability 5. The profile and manifest ID
  are currently operator-authored rather than derived and hashed from the
  actual OpenMW content loadout; one default profile initializes every new
  player, character combat state is not persisted, and production contact/reach
  validation remains fail-closed. Enabling combat under those conditions would
  not prove client/server record equivalence or historical contact.
- The current resolver covers direct player-versus-server-actor weapon and
  hand-to-hand hit, fatigue, resistance, critical/knockdown multipliers, weapon
  wear, damage, and death. Actor attacks, PvP/P2P, blocking decisions, difficulty
  scaling, skill advancement, AI aggression, hit reactions/sounds, on-strike
  enchantments, elemental shields, disease, Lua hit callbacks, general magic,
  canonical resurrection/respawn policy, and client weapon-wear presentation
  remain unimplemented.
- Established root checkpoints and complete character profiles survive restart;
  canonical dynamic world/object/actor state does not. Chargen is the only
  scripted sequence with explicit server safe points today. Other cutscenes,
  quest/script progress, and their intermediate state are not yet canonical or
  durable and must be added through the future server-scripting/persistence
  boundary. Starting inventory/equipment is modeled in the profile, while
  broader inventory persistence remains unfinished.
- The packaged default is the verified installed vanilla manifest. Other
  loadouts still require bounded content generation and local record mappings;
  server discovery/history remain unfinished.
- Content is supplied through bounded hand-authored server artifacts. General
  extraction/baking tooling and broad production-world content are unfinished.
- Test and fixture paths demonstrate subsystem behavior, but release-quality
  real-game coverage, packaging, performance budgets, and soak evidence remain
  unfinished.

## Work still required

### Next milestone: content-specific character state integration

Bind the confirmed character-specific derived stats, spells, and starting
inventory into the broader canonical inventory/combat bootstrap and add a
repeatable content baker for non-vanilla loadouts. Production combat admission
follows that character-specific foundation; general deterministic server
scripting remains later work.

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

The chargen save-boundary and build-consolidation working tree passed the
following on 2026-09-09:

- the standalone MSVC C++20 aggregate, including character-profile, protocol
  golden-vector/malformed-input, lifecycle, authentication, client-session,
  persistence, and server-application contracts;
- a fresh bounded Windows MSVC/Ninja product build of `openmw` and the linked
  `tes3mp_server`, followed by the focused OpenMW adapter and server-application
  contract executables;
- all 182 repository Python tests and patch-registry verification; and
- focused server-application coverage of both packaged chargen safe points,
  strict rejection of legacy character/persistence files, and atomic file
  replacement; and
- focused authoritative-join coverage of disconnect, process restart,
  credential reattach, and successful chargen completion.

The Linux-only sanitizer/fuzzer execution profile, non-Windows product builds,
PC-VR hardware checks, a full upstream OpenMW baseline, and a human-driven
visible OpenMW chargen walkthrough were not performed. The bounded Windows
product link uses the newest installed MSVC so its STL matches the provisioned
protobuf/Abseil libraries. The repository baseline verifier also cannot attest
the current dirty tree because pre-existing vNext additions remain untracked.

Use [DEVELOPMENT.md](DEVELOPMENT.md) for commands and record only the newest
relevant verification here after behavior changes.
