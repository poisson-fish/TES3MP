# Phase 14 interactive objects, locks, traps, and doors discovery

Date: 2026-09-07

Status: **Complete; Package A approved**

This pass traces OpenMW door, lock, trap, activation, cell, and renderer seams,
uses archived TES3MP 0.8.x behavior as requirements evidence, and defines the
smallest production interactive-object foundation. It changes no runtime,
protocol, authority, persistence, movement, or rendering behavior.

## Current boundary

| Area / Lane | Repository-backed OpenMW behavior | Current bound or gap |
|---|---|---|
| Door activation & motion | `MWClass::Door::activate` evaluates locks/keys/traps and returns `ActionTeleport` or `ActionDoor`. `World::activateDoor` toggles `DoorState` (`Idle`, `Opening`, `Closing`). `World::processDoors` advances rotation around the Z-axis by 90° at 90°/sec, checking actor collision (`CollisionType_Door` vs `CollisionType_Actor`). Inactive cells remove doors from `mDoorStates`. | In single-player, door rotation runs in the frame update loop. Multiplayer cannot allow client-local frame simulation to author canonical world state or desync door angles. |
| Locks and keys | `MWWorld::CellRef` stores `mLockLevel` (positive if locked, negative if unlocked, 0 if never locked), `mIsLocked` boolean, and `mKey` (`RefId`). `Door::activate` checks inventory for key; if present, it calls `unlock()` and disarms traps. `MWMechanics::Security::pickLock` calculates chance based on player stats and lockpick quality. | No server-side lock validation, key verification, or lock level tracking exists in vNext. Clients must not be trusted to report self-unlocked status. |
| Traps | `MWWorld::CellRef` stores `mTrap` (`RefId` of spell). `ActionTrap` casts the trap spell from the object onto the activating actor when triggered. `MWMechanics::Security::probeTrap` rolls a disarm check and clears `mTrap`. | Traps are currently entirely client-evaluated. Springing or disarming a trap must be an authoritative canonical outcome, not an unvalidated client assertion. |
| Activation intake | `ActionManager::activate` calls `Player::activate`, which finds the focused object via raycast (`getFocusObject`) with `hasToolTip`. `_runStandardActivationAction` checks `RefData::activate()` for mwscript `OnActivate` suppression before calling `Door::activate`. | In multiplayer, activation must become a typed, reliable client command carrying interaction origin/intent, validated against server-known distance, cell, and object state. |
| Legacy TES3MP 0.8.x | Archived multiplayer used client-authored packets: `PacketDoorState` (`ID_DOOR_STATE`), `PacketObjectLock` (`ID_OBJECT_LOCK`), `PacketObjectTrap` (`ID_OBJECT_TRAP`), and `PacketObjectActivate` (`ID_OBJECT_ACTIVATE`). Objects were identified by string cell name + `(refNum, mpNum)`. | Flawed authority model: clients unilaterally dictated door state, lock levels, and trap disarming. No server reach check, key check, or race-resolution existed, leading to desync and abuse. |
| Canonical server state | `CanonicalServerState` owns bounded player and actor entities/transforms. | There is no interactive object storage, cell object catalog, object revision counter, or interaction intake in vNext server core. |
| VR interaction & reach | PC-VR OpenMW-VR samples head and hand transforms for presentation (`LocalVrPose` / `vr_pose`). | In VR, physical reach could allow activation through walls or beyond intended distance if unconstrained. Pose samples must not author reach beyond a server-enforced root envelope. |
| Unloaded cells | In single-player OpenMW, inactive cells purge doors from `mDoorStates`. | The server must retain canonical object modifications (unlocked, open/closed, disarmed) when a cell has 0 players, freeze ticking without CPU waste, and send full baseline on entry. |

## Separation of lanes

To preserve vNext invariants, interactive objects require strict separation of
durable state, commands, canonical outcomes, presentation, and interest:

1. **Durable / Canonical object state (server-owned):**
   - Scoped object identity: `ObjectId` within an exact canonical `CellId`.
   - Discrete door state: `Closed` or `Open`.
   - Lock state: `Locked` (with lock level and matching `ContentKeyId`) or `Unlocked`.
   - Trap state: `Armed` (with `ContentSpellId`) or `Disarmed`.
   - Object revision: monotonically increasing `ObjectRevision` to sequence
     mutations and ensure idempotency.
   - Retained in canonical memory even when cells have 0 observers.

2. **Reliable interaction commands (client -> server):**
   - Bounded, authenticated `InteractObjectCommand` on the reliable channel.
   - Contains sender `PlayerId`, target `CellId`, target `ObjectId`, expected
     `ObjectRevision`, and interaction kind (`Activate`, `UnlockWithKey`).
   - Carries client interaction origin to support reach validation. A presented
     key ID is only a request hint and never proof of possession.

3. **Canonical outcomes (server reducer):**
   - Server verifies:
     - sender is in the exact same canonical `CellId` as the target object;
     - distance between player canonical root and object transform is within
       `MaxActivationReach` (e.g. 256 quanta plus bounded reach tolerance);
     - object exists and expected revision matches (or is idempotently resolved);
     - if locked, interaction fails unless the requested key matches and
       possession is independently verified from server-owned state;
     - if trapped and armed, springs the trap (emits trap outcome event).
   - Server atomically commits mutation: toggles `Closed <-> Open`, increments
     `ObjectRevision`, and broadcasts update.
   - Teleport doors: initiates canonical player cell transition to destination
     cell and transform.

4. **One-shot presentation effects (client-only presentation):**
   - Local OpenMW renderer plays door swinging mesh animation (90° rotation over
     1.0 second).
   - Audio manager plays open/close sounds, lock rattle, key jingle, or trap
     spring sounds.
   - Message box displays local notifications ("Door is locked", "Key used").
   - Presentation effects never author or feed back into canonical state.

5. **Exact-cell interest & resync:**
   - Interactive object state follows Phase 11 exact-cell equality.
   - Joining or entering a cell receives a reliable complete baseline of the
     cell's interactive objects.
   - State mutations broadcast reliably to all observers in that exact cell.
   - Cells with 0 players freeze simulation without data loss.

## Recommended production seam

1. **Manifest-bound object catalog:** Add a bounded manifest-scoped
   `ObjectCatalog` declaring interactive objects per cell (at most 512 objects
   per cell, 16,384 total). Each entry defines `ObjectId`, `CellId`, `ObjectKind`
   (StandardDoor, TeleportDoor), initial transform, default lock level/key,
   default trap spell, and teleport destination. OpenMW maps `ObjectId` locally
   to `ESM::CellRef` (`refNum`).
2. **Canonical object world:** Add immutable `CanonicalObjectState` to server
   core, tracking current door state (Closed/Open), lock state, trap state, and
   `ObjectRevision` per declared object.
3. **Server-evaluated interaction reducer:** Process `InteractObjectCommand`
   inside the fixed scheduler tick. Perform same-cell and bounded distance/reach
   checks before mutation. Reject out-of-reach, wrong-cell, or locked attempts
   fail-closed.
4. **Discrete canonical door state with client visual interpolation:** The
   server tracks discrete `Closed` and `Open` states. When toggled, the client
   plays the 1.0-second 90° swing animation locally. No intermediate rotation
   angles enter the network or server simulation.
5. **Freeze unloaded cells:** Cells with 0 active player observers freeze
   simulation immediately; any in-flight door transition snaps to its target
   discrete state. Canonical mutations persist in memory.
6. **Strict phase boundaries:** Inventory, containers, player combat/damage
   stats, server scripting, durable disk persistence, and client authority are
   strictly excluded from the Phase 14 package.

## Owner package choices

These choices materially affect architecture, network overhead, and authority.
Select one package before implementation:

- **Package A — server-authoritative discrete states with client-interpolated animation (recommended):**
  The server canonical world owns discrete object states (`Closed`/`Open`,
  `Locked`/`Unlocked`, `Armed`/`Disarmed`) and evaluates interaction commands
  against same-cell, reach, and lock invariants. The client interpolates visual
  door rotation and sound locally. Teleport doors initiate server-owned cell
  transitions. This minimizes network traffic, eliminates tick-rate door
  streaming, matches OpenMW's aesthetic animation design, and eliminates
  split-brain door desync.
- **Package B — continuous server-ticked door rotation with latest-wins streaming:**
  The server simulates door rotation angle tick-by-tick at 30 Hz and streams
  intermediate angles to clients via latest-wins snapshots. This adds significant
  CPU and network bandwidth overhead for every moving door, introduces jitter
  into visual door swings, and provides negligible gameplay value over discrete
  states.
- **Package C — client-leased interactive object simulation with server outcome validation:**
  The server leases door simulation to an observing client, which reports the
  completed swing or collision outcome. This reopens ADR-0006, introduces lease
  handoff complexity, suffers from latency when opening doors, and creates
  split-brain conflicts when multiple players interact simultaneously.

Package A is the recommendation. It establishes a robust, bounded,
server-authoritative foundation with zero unnecessary network traffic.

## Owner decision

The project owner approved Package A on 2026-09-07. The durable decision is
recorded in [ADR-0061](adr/ADR-0061-phase14-server-authoritative-interactive-objects.md)
and [GDR-0021](gdr/GDR-0021-phase14-interactive-objects-locks-traps-doors.md).

## Named proof scenarios

The canonical-core pass covers scenarios 1-10. Server composition and baseline
replication complete scenarios 11-12 in the next Phase 14 slice.

1. `interactive_object_catalog_is_manifest_bound_bounded_and_unique`
2. `object_identity_is_scoped_to_cell_and_disjoint_from_entity_id`
3. `server_is_sole_authority_for_lock_trap_and_door_mutation`
4. `interaction_rejected_when_actor_is_not_in_same_exact_cell`
5. `interaction_rejected_when_distance_exceeds_maximum_activation_reach`
6. `interaction_rejected_when_locked_and_key_not_present`
7. `interaction_with_armed_trap_springs_trap_outcome_reliably`
8. `unlocked_standard_door_toggles_open_closed_state_with_incremented_revision`
9. `teleport_door_initiates_canonical_cell_transition_for_activating_player`
10. `stale_or_replayed_interaction_command_is_rejected_or_idempotent`
11. `unloaded_cell_preserves_canonical_object_modifications_without_ticking`
12. `late_join_and_resync_receive_complete_cell_interactive_object_baseline`
