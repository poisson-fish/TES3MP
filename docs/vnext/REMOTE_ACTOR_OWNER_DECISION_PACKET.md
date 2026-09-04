# Remote actor owner decision packet

Status: **C-R1 approved and implemented; owner acceptance remains gated**

Prepared: 2026-09-03

Decision owner: project owner

Governing records: [ADR-0007](adr/ADR-0007-openmw-hook-patch-queue-policy.md),
[ADR-0050](adr/ADR-0050-phase8-cell-and-remote-presentation.md), and
[GDR-0013](gdr/GDR-0013-phase8-cell-transition-presentation.md)

## Decision requested

When another player is present in the authoritative observed set, what kind of
OpenMW object should represent that player, and which engine subsystems may see
it?

This must be decided now because the current Phase 8 implementation is neither
the renderer-only object described by ADR-0050 nor a normal actor with understood
side effects. It constructs normal NPC mechanics and inventory data, consumes
the game PRNG, installs inventory listeners, and exposes a normal `MWWorld::Ptr`
to renderer picking. The real-content two-client run then closes with the
coarse reason `remote player presentation failed`. Continuing without a role
decision would let an implementation detail silently choose gameplay,
interaction, scripting, and persistence behavior.

At preparation time, no option was approved by its presence in this packet.
The explicit C-R1 approval and implementation result are recorded below.

## Retained decisions and non-decisions

The following remain accepted and are not reopened:

- the server is authoritative and an OpenMW object is client-local presentation,
  never canonical state;
- remote visibility is the authoritative observed set intersected with the
  local player's authoritative cell;
- `EntityId` is the adapter/session presentation key;
- all creation, update, and removal occurs on the OpenMW main thread after
  correction and before ordinary world advancement;
- observation removal, disconnect, cell discontinuity, and adapter teardown
  clear remote presentation;
- protocol, canonical state, authority, and the engine-independent client
  session do not gain OpenMW types;
- the approved fixture remains NPC record `player`; changing that record is a
  separate owner decision; and
- Phase 8 stays **In Progress** and Phase 9 stays gated.

At preparation time, the following were not approved: the replacement actor role, any new engine
hook, actor collision, mechanics participation, Lua/script visibility,
activation, equipment behavior, locomotion animation semantics, persistence,
or fallback to a different content record.

## OpenMW 0.51 boundary audit

The audit used the pinned OpenMW 0.51.0 baseline at
`f4bec41444214a7903bebd178389ca22ca13f646`, the current vNext tree at
`c507eb01fd3424c7b158a13b538b518b955038c4`, and read-only inspection of the
permanent `tes3mp-0.8.1-archive` tag.

### Normal actor creation and registration

A normal dynamically placed NPC crosses several boundaries as one operation:

1. [`Npc::copyToCellImpl`](../../apps/openmw/mwclass/npc.cpp) inserts a
   `LiveCellRef<ESM::NPC>` into a `CellStore`.
2. [`Class::copyToCell`](../../apps/openmw/mwworld/class.cpp) assigns a generated
   reference number and registers the pointer in `WorldModel`.
3. [`World::initObjectInCell`](../../apps/openmw/mwworld/worldimp.cpp) sends an
   active-cell object through `Scene::addObjectToScene` and adds its local
   script.
4. [`Scene::addObject`](../../apps/openmw/mwworld/scene.cpp) fans the same pointer
   into rendering, mechanics, water effects, looping particles, physics,
   navigation, and Lua object-added events.
5. [`CellStore::writeReferences`](../../apps/openmw/mwworld/cellstore.cpp)
   serializes cell reference collections. Merely iterating mutable cell
   references also marks the cell stateful.

Registration is therefore not a neutral lookup convenience. `CellStore`,
`WorldModel`, scene membership, mechanics, physics/navigation, scripts/Lua,
renderer picking, and save participation are coupled defaults.

### Current renderer-only proxy

[`TransientActorPresentation`](../../apps/openmw/mwrender/transientactorpresentation.cpp)
does avoid `CellStore`, `WorldModel`, `Scene`, mechanics, physics, navigation,
and Lua registration for the avatar itself. It creates a `ManualRef`, points it
at a cell only for rendering, calls `Objects::insertNPC`, and removes the object
through `RenderingManager` on destruction.

That narrow route still has material hidden effects:

- [`Npc::ensureTransientPresentationData`](../../apps/openmw/mwclass/npc.cpp)
  invokes the same initialization used by gameplay NPCs except for registering
  the actor pointer. It creates `NpcStats`, `CreatureStats`, spells, race powers,
  AI packages/settings, an `InventoryStore`, and auto-equipment.
- Inventory fill registers the generated inventory item pointers in
  `WorldModel` and consumes the world PRNG, so the route is not isolated from
  world identity or deterministic single-player state.
- [`Objects::insertNPC`](../../apps/openmw/mwrender/objects.cpp) installs both
  inventory and container listeners.
- the common object insertion prefix attaches a renderer `PtrHolder`. Normal
  focus rays can therefore return the remote avatar as an activatable NPC even
  though GDR-0013 says it has no interaction.
- `NpcAnimation` obtains gameplay stats and inventory through `MWWorld::Class`.
  Its head blink timer also consumes the world PRNG.
- because the proxy never enters `MWMechanics::Actors`, it has no
  `CharacterController` to choose and advance normal idle/locomotion animation.
  Reusing `NpcAnimation` alone does not provide the normal actor animation
  lifecycle.
- all construction failures are collapsed by the provider to
  `PresentationFailed`, losing whether the record, body-part data, skeleton,
  mesh, listener setup, or lifecycle invariant failed.

### Legacy normal-actor evidence

TES3MP 0.8.1 placed remote players as dynamic normal NPC references. That made
rendering, equipment, animation, collision, cell movement, spells, combat, and
map integration immediately available, but local simulation then had to be
constrained with direct dedicated-player classification throughout the engine.
Read-only inspection finds the checks across the class, mechanics, combat,
spellcasting, physics, renderer, script, inventory, projectile, cell, and world
layers. The pattern is evidence about the cost of implicit participation, not
code to copy.

## The three models

| Dimension | A — retain current renderer proxy | B — normal actor with exclusions | C — first-class ephemeral replicated actor |
|---|---|---|---|
| Authority | Adapter overwrites pose; hidden NPC data can still imply local gameplay state | Normal actor state competes with authoritative state unless every local mutation is suppressed | Role contract states that supplied presentation is authoritative input and never a local simulation result |
| Identity | Adapter `EntityId` map plus an unregistered `ManualRef`; inventory creation still consumes generated `RefNum`s | `EntityId` must be associated with a generated OpenMW `RefNum`/`Ptr`, creating two identity systems | Adapter maps `EntityId` to a non-copyable engine-local replica handle; no content or save identity is assigned |
| Lifetime | RAII and observed-set cleanup are good, but renderer/cell-root and listener lifetime assumptions are implicit | Cell and world own the actor beyond observation unless deletion and unload paths gain special handling | Adapter/provider owns the handle; observation, disconnect, resync, cell discontinuity, and teardown are explicit idempotent removal points |
| Registration | Avatar skips world registration, but its inventory items enter `WorldModel` | Enters `CellStore`, `WorldModel`, active scene, and all normal registries; exclusions must undo defaults | Separate bounded replica collection owned by the presentation seam; no `CellStore` or `WorldModel` entry |
| Rendering | Reuses full NPC renderer but with gameplay prerequisites and normal picking metadata | Fully supported normal NPC render path | Reuses factored NPC appearance assembly through an explicit replica insertion path with no gameplay pointer metadata |
| Animation | `NpcAnimation` exists but no mechanics controller owns normal animation advancement | Full `CharacterController`, along with unwanted mechanics state and side effects | A renderer-local presentation controller advances only owner-approved visual groups; proposed Phase 8 profile is neutral idle only |
| Mechanics | Not registered, although mechanics-shaped stats/AI are allocated and initialized | Registered by default; AI, combat, stats, death, spells, and movement each need exclusions | Default-deny; no `MWMechanics::Actors`, AI, combat, stats, spells, or movement entry |
| Physics/collision | No Bullet actor and no navigation agent | Present by default; special movement and collision ownership are required | Default-deny; visual avatar neither blocks nor pushes, is not a projectile target, and does not affect navigation |
| Scripts/Lua | No scene-added event, but renderer focus can feed the normal activation-to-Lua path | Local scripts and Lua object lifecycle/activation are defaults | No local script, Lua object, global lookup, console reference, or activation event |
| Persistence/save | Avatar is outside cell save collections, but creation mutates PRNG and pointer registry state | Dynamic cell reference and actor state are save candidates; exclusion requires durable filters and load rules | Structurally unsaveable because the role is absent from `CellStore`, `WorldModel`, and state writers |
| Interaction | Unintentionally focusable as a normal NPC through renderer `PtrHolder` | Normal activation, tooltip, dialogue, hit, crime, combat, and console semantics unless individually excluded | No gameplay `PtrHolder`; visual intersections pass through it for focus/activation and no local interaction is emitted |
| Errors | One generic provider failure | Wide failure surface spread over world subsystems | Typed creation/update errors at one boundary, mapped to stable sanitized connection status and detailed local diagnostics |
| Future features | Every new feature further stretches a nominal renderer-only abstraction | Features are easy to start but hard to contain | Each new capability requires an explicit role-capability amendment and a narrow subsystem adapter |

## Consequences by option

### Option A — retain and repair the current renderer-only proxy

This would keep the four-path P8-004 concept and replace full NPC initialization
with a smaller presentation initializer. It would also need to remove picking
metadata, inventory listeners, generated inventory registration, PRNG use, and
the implicit animation assumptions.

- **Gameplay:** can preserve non-collision and non-mechanics behavior, but the
  abstraction remains a special case inside normal NPC rendering.
- **Security:** network state remains outside the engine, but a normal `Ptr`
  continues to be a tempting route into activation or later gameplay APIs.
- **Protocol:** no protocol change.
- **Scripting:** can be kept invisible only through continued negative tests.
- **Persistence:** can stay structurally excluded if no nested object registers.
- **Migration:** smallest immediate diff, but most of the work needed for C is
  still required to make the claims true.
- **VR:** render reuse is possible, but role semantics remain implicit and easy
  for a VR-specific provider to diverge from.
- **Operations:** typed errors and a real-content preflight must be added anyway.

Option A is viable only as a deliberately short-lived Phase 8 repair. It is not
recommended because it preserves the ambiguity that caused this review.

### Option B — normal actor with explicit exclusions

This would create a dynamic NPC in `CellStore`, register it with `WorldModel`,
and use normal scene insertion, then introduce a role/capability check at each
unwanted subsystem boundary.

- **Gameplay:** best immediate fidelity for equipment and animation, but local
  AI, collision, combat, spell, activation, crime, and death semantics become
  opt-out hazards.
- **Security:** a malicious or malformed remote stream reaches a much larger
  engine surface; every exclusion becomes part of the trust boundary.
- **Protocol:** no immediate schema change, but normal actor systems encourage
  packet-driven synchronization of local engine state.
- **Scripting:** scripts and mods can discover or mutate the actor unless lookup,
  lifecycle events, commands, and serialization all filter the role.
- **Persistence:** save exclusion is not structural. It requires filters plus
  tests for moved references, generated `RefNum`s, cell unload/reload, and old
  saves.
- **Migration:** highest patch count and regression risk. The legacy 16-file
  pattern demonstrates the likely trajectory even if vNext centralizes checks.
- **VR:** physical presence may appear attractive, but creates platform-specific
  collision and interaction pressure before Phase 9 decisions.
- **Operations:** failures can originate in every participating subsystem and
  teardown ordering becomes harder to bound.

Option B is not recommended.

### Option C — first-class ephemeral replicated actor

This adds one protocol-agnostic OpenMW role whose capabilities are explicit and
default-deny. Its initial Phase 8 capability profile is proposed as rendering
plus renderer-local passive animation only. It owns no canonical state and is
not a normal world reference.

- **Gameplay:** the remote avatar is visible and smoothly positioned but has no
  local authority, collision, AI, combat, activation, inventory, or scripts.
- **Security:** validated appearance and pose values cross one narrow main-thread
  boundary. Network data cannot select scripts or arbitrary resource paths.
- **Protocol:** none. The adapter converts existing snapshot/fixture values to
  an engine-local appearance descriptor and pose.
- **Scripting:** invisible by construction. A future script-visible replica API
  requires a separately approved capability, not discovery as a normal object.
- **Persistence:** excluded by ownership and type, not a save-time special case.
- **Migration:** requires a small rendering refactor now, but removes P8-004's
  false contract and avoids cross-engine exclusions.
- **VR:** desktop and PC VR can use the same role and state contract; only their
  presentation provider differs.
- **Operations:** one bounded collection, typed errors, deterministic cleanup,
  and direct real-content tests.

Option C is recommended.

## Approved package C-R1

### Authority, scope, visibility, and ordering

- The server snapshot remains authoritative. The replica accepts a validated
  presentation pose; it never publishes an OpenMW-derived position back to the
  session.
- State scope is one client-local replica per observed `EntityId`, maximum 255
  peers under the existing 256-player canonical bound.
- A replica is visible only when the peer and local authoritative entries share
  a cell. A complete snapshot reconciliation is idempotent and latest-wins.
- The existing correct-then-command frame order remains unchanged. Replica
  reconciliation follows local authoritative correction; replica animation and
  smoothed pose advance once per main-thread frame.
- Revisions, generations, and resync remain session concerns. The engine role
  receives no protocol counters.

### Identity and lifetime

- `EntityId` stays in `DesktopPresentation::remotes`; it is never converted to
  an `ESM::RefNum`, record ID, or Lua object ID.
- The OpenMW side returns a non-copyable RAII replica object. Object identity is
  its lifetime-bound handle, not a globally searchable world identity.
- Create is valid only for an active target cell and a prevalidated appearance.
  Repeated create for an existing `EntityId` is prevented by adapter reconcile.
- Destroy is idempotent. Removal from the observed set, peer cell departure,
  complete-snapshot replacement, disconnect, failed resume barrier, new game,
  renderer clear, and adapter teardown destroy the object before its cell or
  renderer dependencies.

### Registration and subsystem capability profile

The initial profile is explicit:

| Capability | Phase 8 C-R1 |
|---|---|
| Replica collection | Yes, renderer-owned and bounded |
| Scene graph rendering | Yes |
| Passive renderer-local animation | Yes |
| `CellStore` | No |
| `WorldModel`/`PtrRegistry` | No |
| `MWMechanics::Actors`/AI/combat/stats/spells | No |
| Bullet actor/object collision | No |
| Navigator agent/obstacle | No |
| Local scripts or Lua object lifecycle | No |
| Save/load or reference serialization | No |
| Focus, activation, tooltip, dialogue, hit, crime, console lookup | No |
| Water ripple, looping gameplay particles, sound emitter | No |

Any `Yes` added later requires an ADR/GDR amendment naming authority, input,
output, ordering, reset, and tests for that capability. A generic bitmask
mutable by the adapter is not recommended; the engine should expose named,
reviewed profiles so callers cannot assemble unreviewed behavior.

### Appearance and rendering

- The configured NPC record is a local content key, not network data.
- Preflight resolves record type, race, sex, head, hair, skeleton, body parts,
  and record-declared visible clothing/armor into a read-only appearance
  descriptor. It must not create `NpcStats`, `CreatureStats`, spells, AI,
  inventory objects, listeners, generated `RefNum`s, or PRNG draws.
- Conflicting or unsupported record inventory is rejected as an appearance
  dependency error rather than invoking gameplay auto-equip. The approved
  `player` record's shirt, pants, and shoes become visual equipment inputs.
- Scene insertion reuses the cell render root but uses a distinct
  `Mask_ReplicatedActor`. The main camera renders it; actor-shadow and
  actor-reflection settings include it; ToggleWorld hides it. Rendering
  intersection visitors exclude that mask, so a nearest replica cannot consume
  the `LIMIT_NEAREST` hit before a real object behind it. It does not attach a
  normal renderer `PtrHolder`.
- Pose updates change only the replica transform. Same-cell movement never
  calls `World::moveObject`, physics, navigation, or scripts.

### Animation

For Phase 8, use a neutral idle loop advanced by the replica's renderer-local
controller and the frame delta. If the idle group is absent, hold a valid bind
pose and report a non-terminal diagnostic. Do not instantiate
`MWMechanics::CharacterController`, infer combat/draw state, consume the game
PRNG for blinking, or use root-motion output.

Velocity-derived walk/run/strafe selection is deliberately deferred. It is a
player-visible behavior with direction, speed, transition, and fallback rules
that should be approved with its own demo rather than smuggled into actor
creation.

### Physics, interaction, scripts, and persistence

- A Phase 8 replica is visual-only in the physical sense: players, actors,
  doors, projectiles, and raycasts behave as though it is absent.
- Looking at or activating it produces no focus object and no Lua activation
  event. A nearer replica must not prevent a real object behind it from being
  focused.
- It has no inventory. Record-declared equipment is immutable appearance data,
  not items that can be taken, scripted, enchanted, or saved.
- Save while replicas are visible must produce byte-equivalent TES3MP-related
  cell/reference state to save with no replicas visible, excluding ordinary
  time-dependent save metadata. Load creates no replica until a new complete
  authoritative view is applied.

### Error taxonomy and policy

Introduce an engine-local typed result with these categories:

| Error | Examples | Adapter mapping | Policy |
|---|---|---|---|
| `InvalidAppearanceRecord` | missing ID, non-NPC ID | `ContentMappingFailed` | fail preflight or close; no alternate record |
| `MissingAppearanceDependency` | missing race, head/hair/body part, skeleton, or declared equipment | `ContentMappingFailed` | fail closed with local component diagnostic |
| `ResourceLoadFailed` | mesh/skeleton/animation resource cannot instantiate | `PresentationFailed` | clear all replicas and close |
| `CapacityExceeded` | replica count exceeds existing observed-player bound | `PresentationFailed` | reject before allocation, clear, close |
| `InvalidPose` | non-finite/out-of-range converted pose or inactive cell | `PresentationFailed` | reject update, clear, close |
| `LifecycleViolation` | duplicate create, update after destroy, dependency teardown order | `PresentationFailed` | contract diagnostic, clear, close |
| `AnimationFallback` | neutral idle group unavailable | no connection failure | hold bind pose and record bounded diagnostic |

Detailed diagnostics may include the local record ID, missing component kind,
cell kind, and error enum. They must not include credentials, tokens, packet
bytes, arbitrary remote strings, or unbounded exception text. User-visible
connection status remains the existing sanitized content-mapping or
presentation failure.

## Exact migration and patch surface if C-R1 is approved

The migration is replacement, not an extension of the current proxy.

### Production paths

| Path | Exact change |
|---|---|
| `apps/openmw/mwrender/replicatedactor.hpp` (new) | Public opaque RAII role, appearance descriptor, pose/update API, and typed result/error contract; no TES3MP types |
| `apps/openmw/mwrender/replicatedactor.cpp` (new) | Validate/build replica appearance, attach/detach the scene node, advance passive animation, and guarantee cleanup |
| `apps/openmw/mwrender/objects.hpp` and `.cpp` | Add a distinct replica insertion/removal path using the existing cell render root, `Mask_ReplicatedActor`, and no `PtrHolder` or inventory/container listener |
| `apps/openmw/mwrender/vismask.hpp` | Add `Mask_ReplicatedActor` and include it in ToggleWorld without classifying it as a gameplay actor |
| `apps/openmw/mwrender/renderingmanager.cpp` | Include replicas in actor-shadow rendering and exclude them from gameplay/focus intersection traversal |
| `apps/openmw/mwrender/water.cpp` | Include replicas exactly when the configured reflection detail includes normal actors |
| `apps/openmw/mwrender/npcanimation.hpp` and `.cpp` | Extract the reusable NPC body/skeleton/equipment appearance assembly needed by both normal NPCs and replicas; keep gameplay stats, inventory, sounds, PRNG, and `CharacterController` dependencies on the normal path |
| `apps/openmw/mwrender/animation.hpp` and `.cpp` | Make the extracted appearance/animation core accept explicit actor-render context instead of discovering actor-ness only through a gameplay `Ptr`; retain normal-object behavior |
| `apps/openmw/mwclass/npc.hpp` and `.cpp` | Remove `ensureTransientPresentationData`; no replacement replica initializer belongs in `MWClass::Npc` |
| `apps/openmw/mwrender/transientactorpresentation.hpp` and `.cpp` | Delete after all callers move to the first-class role |
| `apps/openmw/CMakeLists.txt` | Replace transient presentation sources with replicated-actor sources |
| `apps/openmw/tes3mp/desktop_providers.hpp` and `.cpp` | Keep `EntityId` ownership and motion buffers in the adapter, preflight the configured appearance, reconcile RAII replica objects, and map typed errors |
| `apps/openmw/tes3mp/adapter_tests.cpp` | Add provider-contract cases for idempotent reconcile, typed failure mapping, cleanup, and no resurrection from stale views |

No production change is proposed in `CellStore`, `WorldModel`, `Scene`,
`MWMechanics`, `MWPhysics`, `MWLua`, `MWScript`, state/save code, protocol,
canonical state, server core, or transport. If implementation proves any such
path necessary, work stops and returns to the owner because the approved patch
surface would have been exceeded.

### Verification and evidence paths

| Path | Exact change |
|---|---|
| `apps/openmw_tests/CMakeLists.txt` and `apps/openmw_tests/mwrender/testreplicatedactor.cpp` (new) | Register focused engine contracts for no gameplay registration/picking and idempotent teardown where fixture construction can be isolated |
| `scripts/run_phase8_desktop_demo.py` | Add credential-free replica lifecycle/error fields and real-content assertions without changing fixture mapping |
| `scripts/tests/test_phase8_desktop_harness.py` | Cover the new evidence schema and failure classifications |
| `docs/vnext/OPENMW_PATCH_REGISTRY.json` | Supersede P8-004 with one replacement patch entry containing the exact reviewed engine paths and removal condition |
| ADR-0007, ADR-0050, GDR-0013, implementation plan/notes, and `STATE.md` | Amend only after owner approval, recording the chosen profile and evidence; do not rewrite prior history |

### Migration order

1. Add failing engine/provider contracts and a real-content preflight that
   reports typed errors while the old proxy remains the production path.
2. Extract the shared appearance renderer with normal NPC behavior unchanged;
   prove multiplayer-disabled and upstream OpenMW tests.
3. Implement the first-class replica path and its negative-registration tests.
4. Switch `DesktopPresentation` atomically from the old proxy to the new role.
5. Remove the transient initializer/proxy, supersede P8-004, regenerate
   provenance, and run the complete Phase 8 gate.

No compatibility shim should register replicas as normal actors. No archived
TES3MP implementation or dedicated-player checks should be copied.

## Acceptance scenarios

These names are proposed as automated contracts and demo steps.

1. `replicated_actor_fixture_player_preflight_passes_with_morrowind_esm_and_bsa`
   validates the exact approved record and all appearance assets before joining.
2. `replicated_actor_wrong_or_missing_record_is_typed_content_failure` covers a
   missing ID and a non-NPC ID without fallback.
3. `replicated_actor_missing_body_or_mesh_is_typed_dependency_failure` proves
   the detailed local error and sanitized user status.
4. `replicated_actor_create_update_destroy_is_main_thread_and_idempotent`
   covers duplicate snapshots and repeated clear.
5. `replicated_actor_never_registers_world_cell_mechanics_physics_lua_or_script`
   compares registry/subsystem membership before, during, and after visibility.
6. `replicated_actor_creation_does_not_advance_world_prng_or_refnum_allocator`
   catches the current nested-inventory side effect.
7. `replicated_actor_is_not_focus_activation_or_combat_target` verifies that a
   real activatable object behind the visual remains selectable.
8. `replicated_actor_save_contains_no_replica_or_replica_equipment_state`
   saves with a visible peer, reloads, and finds no replica before resync.
9. `two_clients_reconcile_same_cell_spawn_move_idle_and_despawn` shows the
   approved interior and exterior flow with passive animation and smoothing.
10. `late_join_complete_snapshot_creates_exact_observed_replica_set` proves no
    missing, duplicate, or stale remote.
11. `cell_change_and_complete_resync_replace_replica_set_without_flashback`
    covers ordering and stale-snapshot non-resurrection.
12. `disconnect_clears_replicas_and_resume_waits_for_complete_snapshot` retains
    the Slice 8.7 continuity barrier.
13. `replicated_actor_capacity_is_bounded_by_canonical_player_limit` exercises
    the exact maximum and one-over rejection.
14. `replicated_actor_teardown_precedes_renderer_and_cell_dependencies` covers
    normal exit, connection failure, new game, and exception cleanup.
15. `multiplayer_disabled_has_no_replica_service_state_or_behavior_delta`
    protects the pinned single-player baseline.
16. `two_client_real_content_flow_reconnect_and_60_second_soak_pass` runs the
    complete content-backed Phase 8 evidence with bounded RSS/queues and no
    credentials in artifacts.

Contention between two network identities for one `EntityId` remains rejected
by existing session identity and snapshot validation before presentation. Reset
means complete-set replacement, disconnect clear, or new-game clear; it never
means retaining a replica as an OpenMW world object.

## Recommendation and owner approval request

Approve package **C-R1**:

1. choose Option C, a first-class ephemeral replicated actor;
2. approve the Phase 8 capability profile of rendering plus passive
   renderer-local idle animation only;
3. keep mechanics, physics/collision, navigation, scripts/Lua, persistence,
   interaction, sound, and gameplay particles excluded by construction;
4. approve the typed error policy and exact patch surface above; and
5. require all 16 named acceptance scenarios, including the content-backed
   two-client run, before replacing the reopened completion claim.

Reason: C-R1 preserves the already accepted player-visible behavior and server
authority while making subsystem non-participation structural. It costs a
focused renderer refactor, but avoids both the current proxy's hidden gameplay
state and the normal-actor model's cross-engine exclusion burden.

## Owner approval

The project owner explicitly approved package **C-R1** on 2026-09-03. This
authorizes only the exact role, subsystem matrix, migration surface, typed-error
contract, and acceptance scenarios in this packet. It does not mark Slice 8.4
or Phase 8 complete, authorize any additional engine subsystem participation,
or waive the real-content and negative-registration evidence gates.

## Implementation result

C-R1 is implemented on the approved surface. The transient proxy and its NPC
custom-data initializer delta are removed; the renderer owns a separate bounded
replica collection without `CellStore`, `WorldModel`, gameplay scene, mechanics,
physics/navigation, Lua, persistence, interaction, sound, or gameplay-particle
registration. Adapter-owned handles reconcile immutable appearance and motion,
treat re-timestamped field-identical revisions idempotently, retain typed
internal diagnostics, and reject contradictory same-revision state.

The focused engine, adapter, negative-registration, build, registry, and
provenance gates pass. Retained real-content evidence in `c-r1-final-5` passes
two-client movement and cell leave/return, exactly 32 resumes, two 60-second
soak clients under the unchanged RSS-window rule, bounded queue high-water, and
zero final queue depth. This evidence does not itself record owner acceptance or
advance Phase 8.
