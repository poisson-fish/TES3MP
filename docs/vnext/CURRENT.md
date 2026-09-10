# TES3MP vNext current implementation

- Updated: 2026-09-09
- Code snapshot inspected: `vnext` working tree based on `0641a1792d`
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
  objects (3), inventory (4), combat (5), and character creation (6).
  Content-manifest mismatch rejects before authentication. Combat (5) is
  offered only when combat content and authoritative contact history are both
  successfully composed; the packaged derived-vanilla default now composes both
  and offers it.
- Negotiation and authentication rejection payloads are sent before a bounded
  graceful close, so clients receive the exact public reason instead of an
  undifferentiated peer disconnect. Client and server pumps enforce their
  staged session deadlines and close stalled handshakes.
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
- Authentication requests associate players by username. The client stores a
  32-byte profile root derived as `SHA-256(lowercase(username) + ":" + password)`
  and derives the transmitted player credential from that root plus the
  normalized server endpoint. The reusable password is not persisted or sent.
  The server atomically stores the transmitted credential's SHA-256 digest with
  stable username, player/entity/appearance, and manifest identity.
- A new username is automatically registered on first join with its credential
  digest. Subsequent joins verify the provided credential against the stored
  digest; incorrect credentials or username collisions reject with `Denied`.
  Credential reattachment recreates the visible avatar with the same durable identity.
- The identity registry is bounded to 256 records. Its V5 atomic file stores
  only a fresh pre-chargen identity or a complete established profile paired
  with its canonical root and username. Intermediate chargen state and movement
  are never written. Credential reattachment restores established state with a new
  authority epoch; an incomplete character instead restarts from the fresh
  pre-chargen checkpoint even when replacing a hidden grace-period session.
  V1–V4 identity files are rejected. Broader world persistence is not yet
  present.

Primary sources: [`authentication.hpp`](../../components/tes3mp/include/tes3mp/authentication.hpp),
[`server_authentication.cpp`](../../components/tes3mp/server_core/server_authentication.cpp),
[`server_lifecycle.cpp`](../../components/tes3mp/server_core/server_lifecycle.cpp),
[`player_identity_file.cpp`](../../apps/tes3mp-server/player_identity_file.cpp), and
[`connection_session_coordinator.cpp`](../../apps/tes3mp-server/connection_session_coordinator.cpp).

### Main-menu entry and player continuation

- The stock main menu exposes a Profiles button opening a modal Profile Manager
  dialog supporting profile creation, local deletion, and active profile
  selection. Profiles are stored locally in `tes3mp/profiles.json` with
  client-side derived profile roots; plaintext files from the initial
  development implementation are rewritten on successful load. Server-side
  credential rotation and durable identity deletion are not yet exposed.
- The main menu Multiplayer button remains disabled until at least one profile
  exists.
- An explicitly configured multiplayer build exposes a stock-main-menu
  Multiplayer dialog accepting a DNS name, IPv4 address, bracketed IPv6
  address, `host:port`, or `tes3mp://` URI. The dialog displays the active
  profile username, and connects using the active profile's username and
  endpoint-derived credential. Legacy generated credentials remain stored per
  endpoint and are pruned only when that legacy credential was actually used.
- The dialog includes bounded password entry. Host starts the packaged
  dedicated-server configuration/content, waits for an explicit readiness
  signal, reports captured startup errors, then connects through the same
  client path. The child is stopped with the client.
- A terminal connection or game-start failure releases its client session and
  process-wide transport ownership. A later Join or Host request in the same
  OpenMW process starts with a fresh transport instead of reporting that the
  multiplayer transport is unavailable.
- Network/session admission is pumped while the main menu remains active, but
  no world provider is called before a game exists. Once the complete initial
  baseline is ready, OpenMW runs its normal new-game startup and the adapter
  applies an authoritative root only for established characters. Fresh and
  incomplete characters retain stock new-game intro placement. Existing
  command-line automation remains available.

Primary sources: [`mainmenu.cpp`](../../apps/openmw/mwgui/mainmenu.cpp),
[`profiledialog.cpp`](../../apps/openmw/mwgui/profiledialog.cpp),
[`multiplayerdialog.cpp`](../../apps/openmw/mwgui/multiplayerdialog.cpp),
[`player_profile_manager.cpp`](../../apps/openmw/tes3mp/player_profile_manager.cpp),
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
- In multiplayer mode, the Census and Excise character name dialog is bypassed
  entirely; the character name is automatically set to the active profile's
  username (`charactername = username`).
- Typed reliable name, race/appearance, predefined/custom class, birthsign, and
  completion commands carry expected profile and canonical revisions. The
  server validates record IDs and phase and computes attributes, skills,
  spells, and starting inventory/equipment. Intermediate confirmations remain
  live-only. When stock `CharGenState` reaches `-1`, the completion command
  prepares the profile, configured post-boat root, catalog-validated starting
  inventory/equipment, and character-derived combat state as one operation.
  Persistence or publication failure leaves every domain unchanged.
- The focused OpenMW bridge intercepts each stock chargen choice, waits for
  confirmation, projects it through existing player mechanics, and advances
  the normal UI. Gameplay input and authoritative presentation are suppressed
  during chargen. After completion, the bridge waits for a newer safe-point
  snapshot before applying the root, using the canonical revision observed when
  completion was submitted so cross-channel delivery order cannot strand an
  already-arrived safe point. A process restart or credential reattachment
  restarts incomplete chargen; established players skip it and restore their
  saved root.

Primary sources: [`character_profile.hpp`](../../components/tes3mp/include/tes3mp/character_profile.hpp),
[`character_creation_protocol.cpp`](../../components/tes3mp/protocol/character_creation_protocol.cpp),
[`character_content.cpp`](../../apps/tes3mp-server/character_content.cpp), and
[`charactercreation.cpp`](../../apps/openmw/mwgui/charactercreation.cpp).

### Cells, interest, and resynchronization

- A bounded manifest declares typed interior/exterior cell spaces and exact
  allowed cells. Proposed transitions outside the catalog fail closed.
- The OpenMW adapter still encodes local transitions within a declared cell
  space when the exact cell is outside that catalog. This lets the server reject
  and correct transient stock new-game placement instead of terminating the
  multiplayer session as a content-mapping failure.
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

- Clients have authority over their own character's 3D movement and physics.
  Clients sample OpenMW local position, orientation, velocity, and locomotion mode
  (walk/run/sneak/jump) and submit them via locomotion proposals.
- Server-side Bullet physics and static blocked-volume collision simulation for
  players are deferred. The server ingests client-authoritative player transforms
  and updates canonical state without applying server movement kernels.
- Local desktop presentation retains native OpenMW player physics and does not
  override intra-cell player coordinates with server snapshots. Initial placement
  is applied on bootstrap and cell transition. Remote presentation uses bounded
  adaptive playback and canonical locomotion state.
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
  wander through the existing movement/collision boundary. An actor struck by a
  player acquires that player as its canonical aggression target, pursues using
  the run movement kernel, and stops within its configured melee reach. Exact
  cells without an active player otherwise freeze and retain actor state.
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
- Completed character profiles seed starting items with server-owned monotonic
  stack IDs, full catalog condition/charge, and declared equipment. The profile
  revision marks that initialization, so reattach and resume cannot duplicate
  the grant or silently apply a different character profile.
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
  seeds and attack profiles, weapon records, and a deterministic random seed.
  Startup rejects a missing, malformed, mismatched, or internally inconsistent
  configured file before exposing any of its state.
- Fresh joins establish the validated server baseline. Character completion and
  established reattachment derive strength, agility, luck, fatigue,
  hand-to-hand, weapon skills, and maximum encumbrance from the confirmed
  profile while preserving baseline resolver modifiers and weight scale. The
  profile revision makes that initialization idempotent. The carried-right
  inventory stack selects server weapon data and skill. Its canonical condition
  receives wear and breakage, including automatic unequip, in the same prepared
  commit as fatigue, damage, death, and command finalization. Relevant
  inventory/equipment changes advance the combat revision and recompute
  normalized encumbrance.
- Server validation rejects unknown or cross-cell targets, stale revisions,
  future/expired source ticks, missing contact history, failed authoritative
  contact/reach, and rate abuse before mutation. Existing session generation,
  command sequence/ID, entity binding, authority epoch, and canonical revision
  checks reject stale authority and replay.
- Private self health/dead/fatigue/revision and same-cell actor stats replicate
  through a latest-wins snapshot. Same-cell player and actor attack outcomes use
  a reliable event batch. OpenMW applies confirmed player resources and hit
  recovery, actor health/fatigue/dead state, native death/resurrection, actor
  attack/hit animations, and canonical inventory condition/breakage.
- Confirmed player contact assigns actor aggression. In-reach actors resolve one
  server-owned attack per configured tick interval using their baked stats and
  natural weapon, update canonical player health/death, and publish the result.
  Dead players and actors automatically respawn after 30 seconds at their
  current canonical root with baseline resources and cleared attack state.
- Production composition retains nine bounded server-tick frames of player and
  actor roots. Contact uses the client-observed historical tick, stock 128-unit
  base distance scaled by canonical weapon reach, exact-cell identity, and the
  collision catalog's obstructing solids. Missing frames, missing historical
  entities, excessive distance, and occlusion fail closed. Capability 5 is
  advertised only after this validator and the complete combat content graph
  exist.

Primary sources: [`melee_combat.cpp`](../../components/tes3mp/protocol/melee_combat.cpp),
[`combat_world.cpp`](../../components/tes3mp/server_core/combat_world.cpp),
[`combat_content.cpp`](../../apps/tes3mp-server/combat_content.cpp),
[`melee_contact_history.cpp`](../../apps/tes3mp-server/melee_contact_history.cpp),
[`combat_replication.cpp`](../../components/tes3mp/protocol/combat_replication.cpp),
[`combat_interest_projection.cpp`](../../apps/tes3mp-server/combat_interest_projection.cpp),
and [`character.cpp`](../../apps/openmw/mwmechanics/character.cpp).

### Repeatable content packs

- The offline V2 baker resolves ordered TES3 content through OpenMW `data`,
  `data-local`, and `content` configuration, hashes exact loadout bytes together
  with normalized server catalogs and complete client mappings, and emits a
  manifest-addressed server/client pack.
- It inspects load-order winners, including deletion, and rejects absent or
  wrongly hashed character, actor, item, cell, or appearance records. It also
  rejects incomplete public mapping sets and cross-catalog combat/inventory or
  actor mismatches before writing a pack.
- It validates bounded TES3 `MAST` declarations against the resolved order and
  records the checked direct-master graph in verified pack metadata. Missing or
  misordered dependencies fail before publication.
- Packs are immutable and carry per-artifact SHA-256 metadata. Successful
  publication atomically advances `CURRENT`; failure preserves the previously
  selected pack. Join-password and player-identity state remain outside the
  pack and are neither copied nor hashed.
- The derived-vanilla recipe replaces authored collision, actor, inventory,
  combat, client actor/item mapping, and starting-equipment records. Numeric
  item, weapon, actor, player-template, and combat-setting values come from the
  resolved TES3 load-order winners. Missing, deleted, ambiguous, malformed, or
  unsupported selected records reject before publication.

Primary sources: [`bake_tes3mp_content.py`](../../scripts/bake_tes3mp_content.py)
and [`test_bake_tes3mp_content.py`](../../scripts/tests/test_bake_tes3mp_content.py).

## Partial foundations and known limitations

- The OpenMW desktop, scripted headless, and shared PC-VR provider architecture
  exist, but representative PC-VR hardware budget capture remains deferred until
  a headset is available.
- Remote actor/root presentation exists. Articulated remote skeleton head/hands,
  improved pose-loss blending, pose compression hardening, and any pose-assisted
  gameplay reach are not implemented.
- Server-authoritative physics and collisions for player characters are deferred;
  clients are authoritative over their own character's movement. Static blocked-volume
  collision and wander kernels remain in place for server-simulated actors. Full
  server-side Bullet physics and terrain/mesh collision integration remain a future
  milestone.
- Actor AI is deliberately limited to idle/travel/wander and reactive pursuit of
  the player who struck it. There is no proactive detection, target selection,
  navmesh parity, schedules, needs, dynamic spawning/removal, or authority
  delegation.
- Interactive traps publish bounded outcomes but full spell-effect resolution
  does not exist. Lockpicking and probe disarming do not exist.
- Inventory does not include barter/trade, merchant stock/restocking, disk
  persistence, or repair commands.
- The packaged default is intentionally narrow: one vanilla dagger, one rat,
  one starting loadout, and pre-inflated collision boxes for the four initial
  cells. The baker derives selected gameplay values from ESM records, but does
  not extract arbitrary NIF/terrain geometry or broad world catalogs.
  Historical contact uses canonical root distance and static collision
  occlusion, not rewound animation volumes or per-bone weapon traces. Combat
  state is not persisted.
- The current resolver covers direct player-versus-server-actor weapon and
  hand-to-hand hit, fatigue, resistance, critical/knockdown multipliers, weapon
  wear, damage, death, reactive actor attacks, hit animations, and timed in-place
  respawn. PvP/P2P, proactive AI aggression, blocking decisions, difficulty
  scaling, resource recovery, skill advancement, hit sounds, on-strike
  enchantments, elemental shields, disease, Lua hit callbacks, and general
  magic remain unimplemented.
- Established root checkpoints and complete character profiles survive restart;
  canonical dynamic world/object/actor state does not. Chargen is the only
  scripted sequence with explicit server safe points today. Other cutscenes,
  quest/script progress, and their intermediate state are not yet canonical or
  durable and must be added through the future server-scripting/persistence
  boundary. Starting inventory/equipment is modeled in the profile, while
  broader inventory persistence remains unfinished.
- The packaged default is the verified installed vanilla manifest. Other
  loadouts still require bounded content generation and local record mappings;
  server discovery/history remain unfinished. The V2 baker binds TES3 content
  plugins but does not yet bind archives or loose resources, so it cannot claim
  complete modpack parity when external assets affect canonical behavior.
- Content packs are deterministic and loadout-bound. Broad record selection and
  placed-reference selection, full geometry extraction, and deterministic
  handling of mod scripts remain unfinished.
- Test and fixture paths demonstrate subsystem behavior, but release-quality
  real-game coverage, packaging, performance budgets, and soak evidence remain
  unfinished.

## Work still required

### Next milestone: combat stats, blocking, and recovery

Add authoritative maximum/current stat derivation and recovery, player blocking,
difficulty scaling, and the remaining direct-combat presentation feedback
against the packaged derived pack. General magic and deterministic server
scripting remain later work.

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
7. Broader content/world coverage, load-order-winner/reference extraction,
   modpack resource identity, content tooling, failure recovery, security
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

The derived-combat-pack working tree passed the following on
2026-09-09:

- the bounded Windows product `checks` graph, including relinking the shipping
  client and dedicated server and running the adapter, server-application, and
  historical-melee-contact contracts;
- the pinned FlatBuffers selection proof, including exact regeneration checks
  for every production protocol header;
- the opt-in Windows headless-client build;
- all 194 repository Python tests and patch-registry verification;
- deterministic bake and verification against the installed 79,837,557-byte
  `Morrowind.esm` with SHA-256
  `5c3c8c2cbd20e25901b59b3ece33d36b7ef0e3d60ad8d11828bcc61a5ead1647`,
  producing and verifying pack
  `bfbfad7ef8c111d2995a61cbdf01a47b3cbebac3bf95f010215ef798d793bfca`;
  and
- a dedicated-server start/readiness/interrupt-stop smoke using the packaged
  default configuration.

The standalone aggregate also passed. The Linux-only sanitizer/fuzzer execution
profile, non-Windows product builds, PC-VR hardware checks, a full upstream
OpenMW baseline, and a human-driven visible OpenMW
walkthrough were not performed. The product build uses the newest installed
MSVC so its STL matches the provisioned protobuf/Abseil libraries. The
repository baseline verifier still cannot attest the current dirty tree because
pre-existing vNext additions and registry changes remain unmatched.

Use [DEVELOPMENT.md](DEVELOPMENT.md) for commands and record only the newest
relevant verification here after behavior changes.
