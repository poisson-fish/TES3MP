# TES3MP vNext current implementation

- Updated: 2026-09-12
- Code snapshot inspected: `vnext` working tree based on `0641a1792d`
- OpenMW baseline: `f4bec41444214a7903bebd178389ca22ca13f646`

This status/backlog document treats “implemented” as production code plus
representative executable tests. Code and tests remain authoritative.

## Executable and library surface

| Surface | Current implementation |
|---|---|
| `tes3mp_protocol` | Strong value types, bounded frames, FlatBuffers codecs, negotiation, authentication and character-profile/dialogue-choice messages, reliable operations, canonical snapshots, actors, objects, inventory, melee and instantaneous-magic combat, regional weather, world time, and VR pose |
| `tes3mp_transport` | Project-owned connection, channel, queue, lifecycle, reason, and telemetry interfaces |
| `tes3mp_transport_gns` | Private GameNetworkingSockets adapter with c-ares/OpenSSL dependency composition |
| `tes3mp_server_core` | Deterministic authentication, canonical worlds, fixed ticks, client and script command reduction, publication, checksums, lifecycle, resync, and a versioned server-scripting boundary |
| `tes3mp_client_session` | Caller-pumped negotiation, authentication, resume/resync, command output, snapshot ingestion, replication state, and locomotion reconciliation |
| `tes3mp_server` | Configuration/content loading and real-transport dedicated-server composition |
| `tes3mp_headless_client` | Scripted real-transport client for bounded integration scenarios |
| `openmw_tes3mp_adapter` | Shared OpenMW connection, stock-chargen, dialogue-choice, and wait/rest confirmation bridges, state application, reconnect, remote, canonical-weather, and canonical calendar/time presentation, object activation, inventory integration, and authoritative melee/magic capture and presentation |

The target graph and boundary checks live in
[`components/tes3mp/CMakeLists.txt`](../../components/tes3mp/CMakeLists.txt) and
[`cmake/TES3MPVerifyTargetBoundaries.cmake`](../../cmake/TES3MPVerifyTargetBoundaries.cmake).
Public core headers expose only project-owned values.

## Implemented behavior

### Protocol and transport

- A 12-byte bounded frame separates message class and kind before payload
  allocation. Each payload is verifier-checked and semantically validated.
- The production server negotiates protocol major 1, minor 9. Defined
  optional capabilities are VR pose (1), actor replication (2), interactive
  objects (3), inventory (4), combat (5), character creation (6), dialogue
  choices (7), regional weather replication (8), world-time replication (9),
  authoritative synchronized wait/rest (10), authoritative security (11), and
  authoritative instantaneous magic (12).
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

- The stock main menu manages local username/profile roots in
  `tes3mp/profiles.json`; Multiplayer activates once a profile exists. Its Join
  flow accepts DNS, IPv4, bracketed IPv6, `host:port`, or `tes3mp://`, while
  Host launches the packaged server, waits for readiness, and uses the same
  connection path. Terminal failure releases transport ownership for retry.
- Session admission runs at the menu without touching a nonexistent world.
  Complete initial baselines start normal OpenMW new-game flow; only established
  characters receive the durable authoritative root.

### Authoritative character creation

- Join explicitly reports `NewCharacter`, `CreatingCharacter`, or
  `EstablishedCharacter`. Manifest content bounds selectable appearance, class,
  birthsign, spells, items, equipment, and the pre/post-chargen safe points;
  multiplayer character name equals the active username.
- Reliable choices carry expected profile/canonical revisions. The server
  validates phase and records, computes derived attributes/skills/spells, and
  atomically commits the completed profile, post-boat root, inventory/equipment,
  and combat state. The OpenMW bridge waits for each confirmation while retaining
  stock UI. Incomplete chargen restarts; established identities restore directly.

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
- Combat V9 exactly covers configured trap IDs. Server-resolved effects,
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

### Authoritative melee and instantaneous magic

- OpenMW keeps its normal attack animation and contact selection, but a
  negotiated session suppresses local mutation. The client submits bounded
  intent and observed tick/revisions; it cannot claim hit rolls, damage,
  resources, targets, or time.
- The server owns the deterministic resolver, PRNG, resources, death, cooldown,
  revisions, skill progress, recovery, aggression, respawn, blocking, armor,
  equipment wear, and supported direct-magic effects. Combat, inventory, death,
  revisions, and command finalization share one prepared atomic commit.
- Bounded `TES3MP_COMBAT_V9` content supplies manifest-scoped player and actor
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
- Private resources, nineteen skill/progress counters, death, and revision use
  latest-wins snapshots. Server ticks handle actor attacks, recovery, respawn,
  and defensive gains. Capability 5 requires the complete combat graph.
- Capability 12 intercepts stock spell release and enchanted-item use before
  local mutation. Clients submit only the mapped spell or canonical item stack,
  target identity, observed tick, and combat/inventory revisions. The server
  validates known spells, ownership, same-cell/touch targeting, resource cost,
  replay/rate bounds, and player or actor revisions; it owns cast success,
  resistances, magnitudes, and every random draw.
- Successful instantaneous effects may damage or restore health, fatigue, and
  magicka on self, player, or actor targets. Spell magicka or item charge,
  durable PRNG state, effect application, death/aggression, school or Enchant
  progress, inventory/combat revisions, persistence, and reliable event plus
  latest-state replication share one prepared commit. Failed spell rolls still
  consume magicka but apply no effect or progression.

Primary sources: [`melee_combat.cpp`](../../components/tes3mp/protocol/melee_combat.cpp),
[`magic_use.cpp`](../../components/tes3mp/protocol/magic_use.cpp),
[`combat_world.cpp`](../../components/tes3mp/server_core/combat_world.cpp),
[`direct_magic.cpp`](../../components/tes3mp/server_core/direct_magic.cpp),
[`combat_content.cpp`](../../apps/tes3mp-server/combat_content.cpp),
[`melee_contact_history.cpp`](../../apps/tes3mp-server/melee_contact_history.cpp),
[`combat_replication.cpp`](../../components/tes3mp/protocol/combat_replication.cpp),
[`combat_interest_projection.cpp`](../../apps/tes3mp-server/combat_interest_projection.cpp),
and [`character.cpp`](../../apps/openmw/mwmechanics/character.cpp).

### Deterministic server-scripting foundation

- Bounded `TES3MP_SCRIPT_PACKAGES_V2` binds package/version/order, API/ABI,
  module hashes/entrypoints, budgets, and typed persistent variables to the
  manifest. The loader and callback registry fail closed on mismatches.
- Script API V6 supplies immutable committed events and copied typed reads for
  globals, quest/journal, dialogue, factions, reputation, and weather. Bounded
  next-tick commands mutate those domains or compare-and-set package variables
  only after catalog, eligibility, type, ownership, and revision validation.
- Output remains staged until the durability commit; callback or queue failure
  exposes no partial result. Package state restores before callbacks, and all
  execution/generated-command order is replay stable.

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

- The V2 envelope's format version 4 binds each durable prefix to
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
- Traps, spells, and on-strike/when-used enchantments cover bounded instantaneous
  direct effects, but durations, area effects, summons, projectiles, and other
  general spell semantics remain absent. Inventory still lacks trade, restocking,
  and repair.
- The packaged default remains a narrow four-cell fixture. Broad geometry and
  rewound or per-bone contact are not implemented.
- Combat covers direct player/actor melee, reactive attacks, resources,
  death/respawn, skills, difficulty, blocking, mitigation/wear, feedback, and
  instantaneous player/actor magic targeting. Actor armor, melee PvP, proactive
  aggression, extended magic semantics, and Lua hit callbacks remain absent.
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

Focused authoritative-magic protocol, world, reducer, replication, persistence,
server-app, interest-projection, adapter, content-baker, schema, patch-registry,
and documentation gates passed on Windows on 2026-09-12. The product build
linked `openmw` and `tes3mp_server`. A live real-`Morrowind.esm` enchanted-item
run submitted intent only, consumed one charge, applied an actor effect, advanced
Enchant, resumed once, reconverged every affected baseline, and drained all
queues. Evidence is in `build/magic-use-evidence-v3/summary.json`. Earlier live
lockpick and probe evidence remains in `build/security-evidence-v17/summary.json`.

The baseline provenance verifier remains red against the broader working tree:
its registry omits many existing vNext files and still expects retired workflow
files. Sanitizers/fuzzers, non-Windows builds, PC-VR hardware, the upstream
baseline, and a visible walkthrough were not run.

Use [DEVELOPMENT.md](DEVELOPMENT.md) for commands and record only the newest
relevant verification here after behavior changes.
