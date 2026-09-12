# TES3MP vNext current implementation

- Updated: 2026-09-11
- Code snapshot inspected: `vnext` working tree based on `0641a1792d`
- OpenMW baseline: `f4bec41444214a7903bebd178389ca22ca13f646`

This status/backlog document treats “implemented” as production code plus
representative executable tests. Code and tests remain authoritative.

## Executable and library surface

| Surface | Current implementation |
|---|---|
| `tes3mp_protocol` | Strong value types, bounded frames, FlatBuffers codecs, negotiation, authentication and character-profile/dialogue-choice messages, reliable operations, canonical snapshots, actors, objects, inventory, melee combat, regional weather, world time, and VR pose |
| `tes3mp_transport` | Project-owned connection, channel, queue, lifecycle, reason, and telemetry interfaces |
| `tes3mp_transport_gns` | Private GameNetworkingSockets adapter with c-ares/OpenSSL dependency composition |
| `tes3mp_server_core` | Deterministic authentication, canonical worlds, fixed ticks, client and script command reduction, publication, checksums, lifecycle, resync, and a versioned server-scripting boundary |
| `tes3mp_client_session` | Caller-pumped negotiation, authentication, resume/resync, command output, snapshot ingestion, replication state, and locomotion reconciliation |
| `tes3mp_server` | Configuration/content loading and real-transport dedicated-server composition |
| `tes3mp_headless_client` | Scripted real-transport client for bounded integration scenarios |
| `openmw_tes3mp_adapter` | Shared OpenMW connection, stock-chargen, dialogue-choice, and wait/rest confirmation bridges, state application, reconnect, remote, canonical-weather, and canonical calendar/time presentation, object activation, inventory integration, and authoritative melee capture/presentation |

The target graph and boundary checks live in
[`components/tes3mp/CMakeLists.txt`](../../components/tes3mp/CMakeLists.txt) and
[`cmake/TES3MPVerifyTargetBoundaries.cmake`](../../cmake/TES3MPVerifyTargetBoundaries.cmake).
Public core headers expose only project-owned values.

## Implemented behavior

### Protocol and transport

- A 12-byte bounded frame separates message class and kind before payload
  allocation. Each payload is verifier-checked and semantically validated.
- The production server negotiates protocol major 1, minor 8. Defined
  optional capabilities are VR pose (1), actor replication (2), interactive
  objects (3), inventory (4), combat (5), character creation (6), dialogue
  choices (7), regional weather replication (8), world-time replication (9),
  and authoritative synchronized wait/rest (10).
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
- Player, actor, equipment, and combat latest snapshots have independent bounded,
  round-robin slots, preventing cross-family overwrite.
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
  V1–V4 identity files are rejected. Broader canonical gameplay state is stored
  separately in the manifest-bound V2 world prefix described below.

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

### Authoritative desktop dialogue choices

- A typed reliable command carries authenticated session/generation, global
  command order/identity, observed revision, and a manifest-bound choice ID.
  Text, voice, layout, actor presentation, and response rendering stay local.
- The server derives player/entity binding and uses canonical faction, rank, and
  reputation eligibility. Applied, unknown, ineligible, and rejected results
  finalize in global gameplay-command order.
- The initiator receives a reliable disposition only after atomic output
  admission and durability commit. Resume retries reuse the command ID; retained
  results prevent repeated events/consequences and are bounded by command history
  and resume grace.
- OpenMW intercepts before `questionAnswered`, shows pending/rejected markers,
  and advances stock dialogue only on a matching commit. Late results cannot
  execute against a different actor.

Primary sources: [`dialogue_choice_protocol.hpp`](../../components/tes3mp/include/tes3mp/dialogue_choice_protocol.hpp),
[`server_command_reducer.cpp`](../../components/tes3mp/server_core/server_command_reducer.cpp),
[`server_application.cpp`](../../apps/tes3mp-server/server_application.cpp),
[`adapter.cpp`](../../apps/openmw/tes3mp/adapter.cpp), and
[`dialogue.cpp`](../../apps/openmw/mwgui/dialogue.cpp).

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
- Actor roots, velocities, entity revisions/change ticks, AI activity and
  waypoint progress are durable. Aggression, attack/death ticks, respawn
  baselines, and current death state are durable with the combat domain.

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
- Standard doors publish discrete state; OpenMW animates them locally. Teleport
  doors request a server-owned cell transition. Trap outcomes are canonical.
- Complete reliable object baselines are scoped by exact-cell interest and used
  for join, transition, resume, and resync.
- Door, lock, trap, revision, and change tick are durable. Restart requires a
  complete state vector matching the configured object catalog.
- Combat V8 exactly covers configured trap IDs. Server-resolved effects,
  damage/death/revision, and trap disarm share one durable commit.
- Capability 11 carries lockpick/probe intent only: object/tool identity plus
  object, inventory, and combat revisions. The server validates canonical tool
  ownership and quality, performs the stock-formula roll from its durable PRNG,
  and commits wear, Security progress, and lock/trap mutation atomically.

Primary sources: [`interactive_object_world.hpp`](../../components/tes3mp/include/tes3mp/interactive_object_world.hpp),
[`interactive_object_replication.cpp`](../../components/tes3mp/protocol/interactive_object_replication.cpp),
[`security.cpp`](../../components/tes3mp/server_core/security.cpp),
[`interactive_object_interest_projection.cpp`](../../apps/tes3mp-server/interactive_object_interest_projection.cpp),
and [`desktop_providers.cpp`](../../apps/openmw/tes3mp/desktop_providers.cpp).

### Inventory, equipment, containers, and ground items

- A manifest-scoped item catalog defines prototype category, weight, value,
  condition/charge bounds, equipment masks, stackability, optional key
  identity, and lockpick/probe quality. Canonical state owns private player
  backpacks, 19 equipment slots, exact-cell containers, and ground-item stacks.
- Take, put, drop, pickup, equip, and unequip are reliable atomic server
  transactions. Validation includes exact source identity/count, revisions,
  capacity, canonical same-cell/reach, and equipment compatibility. Whole-stack
  moves preserve globally unique identity; splits allocate a new monotonic ID.
- Held canonical keys feed the interactive-object validation context. A claimed
  client key ID is never sufficient.
- Completed profiles seed items with server-owned IDs and catalog state. Their
  revision makes initialization idempotent across reattach and resume.
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

- OpenMW keeps its normal attack animation and contact selection, but a
  negotiated session suppresses local mutation. The client submits bounded
  intent and observed tick/revisions; it cannot claim hit rolls, damage,
  resources, targets, or time.
- The server owns the deterministic resolver, PRNG, resources, death, cooldown,
  revisions, skill progress, recovery, aggression, respawn, blocking, armor,
  equipment wear, and supported direct-magic effects. Combat, inventory, death,
  revisions, and command finalization share one prepared atomic commit.
- Bounded `TES3MP_COMBAT_V8` content supplies manifest-scoped player and actor
  profiles, weapons, armor, resolver constants, progression/recovery values,
  random seed, and direct-magic data. Invalid or inconsistent configured content
  fails startup before state is exposed. Confirmed character profiles initialize
  combat state idempotently; canonical equipped items select weapons and armor
  and feed encumbrance.
- Validation rejects unknown or cross-cell targets, stale authority/revisions,
  replay, rate abuse, invalid observed ticks, absent history, excessive reach,
  and occluded contact before mutation. Production retains nine bounded
  server-tick position frames and uses stock base reach scaled by canonical
  weapon data and manifest collision solids.
- Private resources, twelve skill/progress counters, death, and revision use
  latest-wins snapshots. Server ticks handle actor attacks, recovery, respawn,
  and defensive gains. Capability 5 requires the complete combat graph.

Primary sources: [`melee_combat.cpp`](../../components/tes3mp/protocol/melee_combat.cpp),
[`combat_world.cpp`](../../components/tes3mp/server_core/combat_world.cpp),
[`direct_magic.cpp`](../../components/tes3mp/server_core/direct_magic.cpp),
[`combat_content.cpp`](../../apps/tes3mp-server/combat_content.cpp),
[`melee_contact_history.cpp`](../../apps/tes3mp-server/melee_contact_history.cpp),
[`combat_replication.cpp`](../../components/tes3mp/protocol/combat_replication.cpp),
[`combat_interest_projection.cpp`](../../apps/tes3mp-server/combat_interest_projection.cpp),
and [`character.cpp`](../../apps/openmw/mwmechanics/character.cpp).

### Deterministic server-scripting foundation

- Optional bounded `TES3MP_SCRIPT_PACKAGES_V2` content loads manifest-bound
  package/version/load/API/ABI declarations, exact module SHA-256 and entrypoint
  bindings, execution budgets, and typed persistent-variable catalogs.
  Production config contains one executable package and one declared integer variable.
  Declarations are canonically sorted; malformed, oversized, unknown-package,
  wrong-API, or manifest-mismatched input fails startup.
- Script API V6 projects committed publications into immutable bounded events
  plus copied typed-global, quest/journal, and per-player faction rank and
  reputation reads. It includes committed dialogue-choice identity and
  per-region current/target weather, transition timing, and revision; execution
  and generated commands retain replay-stable order.
- Output is staged behind callback, publication, pending, and tick bounds. Any
  callback or queue failure discards the publication output and terminates the
  runtime instead of exposing a partial result.
- Typed next-tick commands cover safe points, time, globals, quests, journals,
  faction rank, reputation, weather targets, and atomic compare-and-set of
  package-scoped variables. Catalogs, eligibility, types, ranks, and independent
  revisions validate before the prepared durability commit.
- Production registers the exact package catalog before opening V2 persistence,
  then restores and binds catalog-compatible state before callbacks can run.
  Undeclared or version-conflicting callback registration fails. A callback
  sees only its package's immutable state.
- The bounded V1 module loader rejects missing, oversized, hash-mismatched,
  malformed, wrong-API/ABI, missing-entrypoint, resource-invalid, or
  catalog-invalid artifacts before startup. Predicates cover typed globals,
  quest/journal state, dialogue identity, faction rank, reputation, and current
  or target weather; revision-checked actions cover quest/journal, faction, and
  weather consequences. A committed weather change also creates an immutable
  `weather_changed` callback event.
  Referenced IDs, stages, ranks, choices, ownership, and types validate against
  manifest catalogs before registration. The packaged module demonstrates both
  quest and dialogue-driven cross-domain consequences.

Primary sources: [`script_state.hpp`](../../components/tes3mp/include/tes3mp/script_state.hpp),
[`server_scripting.hpp`](../../components/tes3mp/include/tes3mp/server_scripting.hpp),
[`script_package_content.cpp`](../../apps/tes3mp-server/script_package_content.cpp),
[`script_module.cpp`](../../apps/tes3mp-server/script_module.cpp), and
[`server_command_reducer.cpp`](../../components/tes3mp/server_core/server_command_reducer.cpp).

### Canonical weather

`TES3MP_WORLD_V4` binds weather identities, regional eligibility/timing, and an
RNG seed. The server persists current/target weather, transition and selection
ticks, revisions, and RNG state; catalog-order fixed ticks replay identically.
Manual and automatic changes share the atomic durability path and emit ordered
immutable records.

Capability 8 sends complete typed baselines on join, resume, and resync, then
revisioned changed-region updates after output admission and durability. Payloads
include current/target weather, transition/selection ticks, and region/server/
canonical revisions. Atomic chunk assembly rejects stale, contradictory, gapped,
malformed, or misbound input.

Content packs bind canonical weather and region IDs to local OpenMW names. The
desktop waits for spatial and weather baselines, suppresses local selection under
multiplayer authority, and feeds confirmed state to WeatherManager. OpenMW retains
interpolation, sky/fog, sound, wind, and particles. Missing local records fail closed.

The real `Morrowind.esm` two-region capture produced identical client transitions
at ticks 600–660/revision 2. A mid-transition disconnect resumed at tick 722 on
completed revision 3 with no reselection or duplicate presentation. A 1.5-second
presentation stall recovered with drained queues and bounded RSS. The runner
writes the baked pack, bounded logs, telemetry, and `summary.json`.

Primary sources: [`weather_replication.hpp`](../../components/tes3mp/include/tes3mp/weather_replication.hpp),
[`weather_projection.cpp`](../../apps/tes3mp-server/weather_projection.cpp),
[`adapter.cpp`](../../apps/openmw/tes3mp/adapter.cpp), and
[`weather.cpp`](../../apps/openmw/mwworld/weather.cpp).

### Canonical world time and calendar presentation

Capability 9 sends full bounded time snapshots on join/resume/resync and about
once per second. They carry the fixed 30-day/12-month calendar, sub-day time,
scale/remainder, and ordering revisions. Full replacement converges across
skipped revisions; stale data is ignored and same-tick contradictions fail.
Desktop readiness waits for this baseline, disables local advancement, applies
confirmed calendar/time globals, and restores local authority on clear.

Capability 10 intercepts the stock OpenMW wait/rest confirmation before local
clock or resource mutation. Requests are bounded to one through 24 hours and
retain their wait-versus-rest mode as consent for the current session generation.
Every active capable player must submit an exact match. The commit rechecks that
all are alive and no live same-cell actor is targeting any of them, advances the
fixed calendar once, restores fatigue for wait or fatigue/health/magicka for
rest, and durably installs and publishes the clock and combat resources as one
transaction. A disconnect removes that session from the active vote; resume
receives the full post-jump clock and resource baselines.

Primary sources: [`world_time_replication.hpp`](../../components/tes3mp/include/tes3mp/world_time_replication.hpp),
[`world_time_projection.cpp`](../../apps/tes3mp-server/world_time_projection.cpp),
[`adapter.cpp`](../../apps/openmw/tes3mp/adapter.cpp), and
[`worldimp.cpp`](../../apps/openmw/mwworld/worldimp.cpp).

### Transactional gameplay persistence and replay envelope

- The V2 envelope's format version 3 binds each durable prefix to
  configuration/content, scripts, seeds,
  ordering, and normalized command results. One checksum covers all canonical
  player, inventory, object, actor/combat, clock/global, quest/journal, faction,
  script-variable, weather, and RNG state. Dialogue choices retain command order.
- Identity includes the exact script API, ordered packages, and typed variable
  catalog. Missing, extra, reordered, or retyped state rejects before install.
- The reducer offers a fully prepared immutable candidate to the durability
  port before installing it or publishing it to replay, scripts, metrics, or
  clients. Only `Committed` acknowledges durability. Rejection or I/O failure
  leaves the previous canonical state and publication installed.
- The production V2 adapter stores a checkpoint plus at most 32 journal records
  in `players.txt.world-v2`, rebasing on overflow. Atomic replacement makes each
  crash cut select the previous or new complete tick. V1 is not migrated.
- Restart validates the bounded prefix and configured catalogs before restoring
  every durable domain and counter. Counts, order, typed IDs, stages, and types
  are atomic; actor roots must pass occupancy. Live sessions are not restored.
  Replay begins from the verified checkpoint, consumes tail ordering one tick
  at a time, and requires the full reconstructed durable checksum at every
  record. Only established-character player state is retained; incomplete
  chargen still restarts fresh.

Primary sources: [`canonical_persistence.hpp`](../../components/tes3mp/include/tes3mp/canonical_persistence.hpp),
[`canonical_persistence.cpp`](../../components/tes3mp/server_core/canonical_persistence.cpp),
[`canonical_persistence_file.cpp`](../../apps/tes3mp-server/canonical_persistence_file.cpp),
[`world_state.cpp`](../../components/tes3mp/server_core/world_state.cpp), and
[`server_command_reducer.cpp`](../../components/tes3mp/server_core/server_command_reducer.cpp).

### Repeatable content packs

- The V2 baker hashes ordered TES3 loadout bytes, normalized server catalogs,
  and complete client mappings into a manifest-addressed pack. It validates
  load-order winners, direct `MAST` order, cross-catalog references, mappings,
  and script-package catalogs before publication.
- Packs are immutable, artifact-digested, and atomically selected through
  `CURRENT`; failure preserves its predecessor. Secrets and player identity
  remain outside the pack.
- The derived recipe uses load-order winners, derives declared trap profiles,
  and rejects missing, ambiguous, malformed, or unsupported records.

Primary sources: [`bake_tes3mp_content.py`](../../scripts/bake_tes3mp_content.py)
and [`test_bake_tes3mp_content.py`](../../scripts/tests/test_bake_tes3mp_content.py).

## Partial foundations and known limitations

- Desktop/headless/PC-VR providers exist; PC-VR hardware evidence, articulated
  remote head/hands, pose-loss refinement, and pose-assisted reach remain absent.
- Player movement remains client-authoritative. Server actors use bounded-volume
  collision; full Bullet terrain/mesh physics is deferred.
- Actor AI is limited to idle/travel/wander and reactive pursuit; detection,
  schedules, spawning, and delegation remain absent.
- Traps cover bounded instantaneous direct effects, but durations, area effects,
  and other general spell semantics remain absent. Inventory still lacks trade,
  restocking, and repair.
- The packaged default remains a narrow four-cell fixture. Broad geometry and
  rewound or per-bone contact are not implemented.
- Combat covers direct player/actor melee, reactive attacks, resources,
  death/respawn, skills, difficulty, blocking, mitigation/wear, feedback, and a
  narrow baked magic subset. Actor armor, PvP, proactive aggression, general
  casting/magic, and Lua hit callbacks remain absent.
- Only declared package variables survive restart. General VM logic and
  inventory/combat/magic scripting remain absent.
- Other loadouts require generated mappings. The baker does not bind archives or
  loose assets; broad references/geometry/mod scripts, discovery, packaging, and
  release-scale performance/soak remain unfinished.

## Work still required

### Required before the desktop/PC-VR release

1. Broaden gameplay and durable world transitions.
2. Expand immutable typed scripting without legacy API reuse.
3. Add administration, moderation, discovery, health, and privileged operations.
4. Finish content/resource identity, recovery, security, packaging, platform,
   performance, regression, soak, and PC-VR evidence.

### Optional after the release boundary

Evaluate standalone Quest only after measured release evidence and an explicit
go decision. Device types remain provider-local.

## Verification snapshot

All 224 Python contracts and the focused protocol, server-logic/server-app, and
adapter gates passed on Windows on 2026-09-11. The desktop-evidence build linked
`openmw` and `tes3mp_server`. Live real-`Morrowind.esm` lockpick and probe runs
each submitted intent only, consumed one of 25 tool uses, advanced Security,
mutated the mapped door, resumed once, reconverged every affected baseline, and
drained all queues. Evidence is in `build/security-evidence-v17/summary.json`.

The baseline provenance verifier remains red against the broader working tree:
its registry omits many existing vNext files and still expects retired workflow
files. Sanitizers/fuzzers, non-Windows builds, PC-VR hardware, the upstream
baseline, and a visible walkthrough were not run.

Use [DEVELOPMENT.md](DEVELOPMENT.md) for commands and record only the newest
relevant verification here after behavior changes.
