# ADR-0007: OpenMW hook and patch-queue policy

Status: **Accepted**

Date opened: 2026-08-26

Date approved: 2026-08-26

Phase 8.1 inventory approved: 2026-09-02

Decision owner: project owner

Needed by: Phase 2

## Decision questions

How should the vNext client session attach to pinned OpenMW 0.51; where may
semantic input, authoritative presentation, lifecycle, and cell/player events
cross the adapter boundary; how are OpenMW-coupled changes tracked; and which
changes should be proposed upstream?

The framework is needed before Phase 3 fixes target directions and before Phase
8 adds a real desktop adapter. The exact feature-by-feature hook inventory is
still reviewed in Phase 8 Slice 8.1 after GDR-0001 and the headless vertical
slice make the required behavior concrete.

## Decision summary

The project owner approved Option A for Decisions 1 through 3 and amended
Option A for Decision 4 on 2026-08-26:

1. use a native app-local C++ adapter and coordinator;
2. begin with one main-thread frame coordinator and add only approved feature-
   specific semantic seams;
3. land buildable commits and maintain a machine-checked patch registry; and
4. do not proactively submit patches to OpenMW for now. Track general-purpose
   upstream candidates locally, and use a separate local OpenMW repository to
   prepare clean changesets only if upstreaming is later desired and explicitly
   authorized.

The separate local repository is a disposable upstream-preparation workspace,
not a submodule, remote build input, CI prerequisite, authoritative source, or
replacement for the vNext commits, patch registry, and baseline manifest. Its
path is developer-local and is not recorded as a portable project path.

## Accepted constraints

Every option must preserve the vNext README, implementation plan, and accepted
ADR-0003 through ADR-0006:

1. `multiplayer_protocol`, `multiplayer_client_session`, and server-core/test
   targets compile without OpenMW, rendering, VR, SDL, OSG, or transport-library
   types.
2. The OpenMW adapter is the only multiplayer component allowed to include
   OpenMW application headers or translate `MWWorld`, `MWMechanics`, `MWInput`,
   SDL, OSG, renderer, or future OpenXR values.
3. OpenMW callbacks and engine state are untrusted observations, not canonical
   state. They become owned semantic commands or presentation samples before
   crossing into the client session.
4. Authoritative session results are converted to OpenMW presentation only on
   the engine's owning/main thread. Network callbacks never mutate OpenMW
   objects, scene graphs, UI, mechanics, or physics directly.
5. The adapter contains no packet parsing, transport-library calls, second
   canonical simulation, direct server state, or legacy packet/processor code.
6. Desktop and PC VR compose the same client session and semantic providers.
   Fork-, renderer-, and OpenXR-specific types stop inside the platform adapter.
7. Multiplayer-disabled OpenMW remains buildable and behaves like the pinned
   baseline except for separately reviewed general fixes.
8. Every active-tree difference from OpenMW remains intentional, documented,
   testable, and covered by the baseline provenance verifier.

## Repository observations

The pinned OpenMW 0.51 tree supplies useful evidence but no pre-approved vNext
hook design:

- [`Engine::frame`](../../../apps/openmw/engine.cpp) runs input first, then Lua
  synchronized changes, state, scripts, mechanics, physics, world, and GUI on
  the main simulation thread. This provides one bounded frame-coordination seam
  after current input is sampled and before world mutation.
- [`Engine::prepareEngine`](../../../apps/openmw/engine.cpp) constructs and
  registers the OpenMW subsystems, while
  [`main.cpp`](../../../apps/openmw/main.cpp) constructs `Engine` and calls
  `go()`. These are natural composition/lifetime boundaries.
- [`MWBase::World`](../../../apps/openmw/mwbase/world.hpp) already exposes
  player, cell, movement, placement, rotation, and scene operations. Some
  operations also trigger scripts, physics, loading, or local gameplay side
  effects, so they cannot automatically be treated as authoritative-apply APIs.
- [`MWBase::InputManager`](../../../apps/openmw/mwbase/inputmanager.hpp) exposes
  action state and the concrete input manager samples devices before mechanics.
  Raw SDL events and binding IDs are OpenMW-side inputs, not protocol commands.
- [`MWBase::LuaManager`](../../../apps/openmw/mwbase/luamanager.hpp) and the
  [Lua implementation note](../../../apps/openmw/mwlua/README.md) demonstrate
  queued events and main-thread synchronized mutations. The interface contains
  OpenMW/SDL/renderer types, runs Lua asynchronously by default, and is designed
  for game scripts rather than a reusable headless C++ client session.
- The current [`openmw-lib` target](../../../apps/openmw/CMakeLists.txt) contains
  the application subsystems and is linked by both `openmw` and `openmw-tests`.
  A new adapter target therefore needs an explicit acyclic composition design;
  simply making `openmw-lib` and an adapter library depend on one another is not
  viable.
- The active OpenMW application tree currently has no vNext delta from the
  pinned upstream commit. The first hook can therefore be reviewed from a clean
  baseline rather than inherited legacy patches.

## Representative scenarios

1. **Multiplayer disabled:** OpenMW starts, loads, plays, saves, and exits
   without constructing network/session state or changing single-player update
   ordering.
2. **Client composition:** a multiplayer-enabled executable constructs one
   app-local adapter around the reusable client session without making
   `openmw-lib` or the engine-independent targets cyclic.
3. **Frame input:** keyboard, controller, or VR input is sampled normally. The
   adapter translates current intent to platform-neutral commands without
   placing SDL, OpenMW action IDs, or OpenXR types in protocol/session APIs.
4. **Inbound correction:** a transport callback queues an owned session result.
   The main thread drains and applies it at a declared frame seam before
   mechanics/world presentation advances; the callback thread touches no engine
   object.
5. **No feedback loop:** applying an authoritative correction, cell placement,
   or remote snapshot does not get re-observed as a new local player command.
6. **Stall and disconnect:** network progress stalls or disconnects while input,
   rendering, UI, and bounded local presentation continue. Shutdown cancels the
   session before OpenMW dependencies are destroyed.
7. **Cell transition and remote presence:** later approved behavior can observe
   a player cell attempt and apply server-confirmed local/remote placement using
   feature-specific semantic seams rather than scanning arbitrary engine memory.
8. **Unsupported engine side effect:** an existing OpenMW mutation method also
   fires a local script or gameplay action that an authoritative apply must not
   repeat. A narrow named application seam is added and tested instead of
   copying the implementation into the adapter or suppressing all events
   globally.
9. **Upstream update:** a newer OpenMW baseline moves the frame loop or mutation
   API. The patch registry identifies every affected seam, its test, disposition,
   and rebase result before the baseline changes.
10. **PC VR composition:** the OpenMW-VR worktree supplies input/pose providers
    and presentation conversions around the same adapter/session contracts. No
    fork-specific conditional enters protocol or server core.

## Decision 1: integration vehicle

### Option A: native app-local C++ adapter and coordinator (approved)

Create a dedicated OpenMW-coupled adapter target and one application-level
coordinator instance. The target may include OpenMW application headers and
link the headless client-session library. Concrete composition occurs at the
OpenMW executable/engine boundary so dependency direction remains acyclic:

```text
openmw executable/composition
          |              \
          v               v
    openmw-lib      openmw_multiplayer_adapter
                           |
                           v
              multiplayer_client_session
```

The adapter owns conversion and main-thread presentation. The reusable client
session owns connection/replication state without knowing that OpenMW exists.
This gives compile-time dependency enforcement, direct access to the necessary
native semantics, and one place to support desktop and VR providers.

The cost is a small maintained native seam in the OpenMW application and CMake
composition. Phase 3 must prove the exact target graph without creating a
library cycle.

### Option B: OpenMW Lua scripts as the primary adapter

Implement the client integration through existing Lua events, bindings, and
queued actions. This can minimize early C++ call-site changes and is friendly to
mod experimentation. However, the current Lua boundary is asynchronous,
OpenMW-type-heavy, incomplete for required lifecycle/state interception, and
designed around engine scripting capabilities. A native networking module would
still be needed and would create a second API/security/runtime surface before
Phase 19 decides server scripting.

Lua may later consume presentation events or supply mod-facing integration, but
it is not recommended as the primary transport/session/authority boundary.

### Option C: deep engine-integrated multiplayer subsystems

Add multiplayer branches, callbacks, and state directly across input, world,
mechanics, physics, scripts, rendering, and state management. This provides
maximum access and can make individual features quick to patch, but packet or
session concerns would spread through OpenMW, tests would require the full
engine, and every upstream update would re-open a broad fork. It resembles the
legacy coupling that vNext explicitly replaces.

### Option D: external process or automation adapter

Keep OpenMW unchanged and infer/control gameplay through console commands,
save files, window/input automation, or an external process. This has the
smallest source delta but cannot provide precise frame ordering, semantic input,
remote entities, authoritative correction, bounded lifecycle, or reliable
failure handling. It is suitable only for disposable research tooling.

## Decision 2: hook-surface policy

### Option A: one frame coordinator plus feature-specific semantic seams (approved)

Begin with one optional app-level composition/lifetime attachment and one
main-thread frame-coordination call after OpenMW input sampling and before Lua,
mechanics, physics, and world mutation. At that boundary the adapter may sample
semantic input, advance/drain the headless session, and apply queued
authoritative presentation in a documented order and time budget.

Add deeper hooks only when an approved GDR and implementation slice proves that
the frame boundary or an existing public operation cannot express a required
semantic event or authoritative apply. Each deeper hook must be:

- named for one semantic observation or application need, not a generic packet
  or property callback;
- owned by the adapter-facing application layer;
- suppressible or origin-tagged where authoritative application could otherwise
  feed back as a new local command;
- main-thread and lifetime safe;
- independent of protocol message layouts and transport/session objects; and
- accompanied by a contract test and an entry in the patch registry.

Prefer existing OpenMW APIs when their complete side effects match the approved
behavior. When they do not, add a narrow engine method or event at the subsystem
that owns the invariant; do not copy private engine logic into the adapter.

This option gives the first vertical slice a bounded seam while allowing later
domains to add only demonstrated hooks. It requires Phase 8 Slice 8.1 to review
the final exact call sites before those patches land.

### Option B: generic engine event bus and mutation API now

Introduce a comprehensive observer/event bus plus generic read/write property
surface across OpenMW before implementing features. This can reduce later call-
site edits, but it guesses at events, ordering, lifetimes, cancellation, and
mutation semantics. A generic mutation API also risks becoming a bypass around
ADR-0006 and making packet shapes decide engine architecture.

### Option C: poll engine state and apply through existing methods only

The adapter scans player/world state each frame, diffs it, and calls existing
methods for every inbound update. This avoids explicit feature hooks initially,
but polling cannot reliably distinguish player intent, scripts, physics,
authoritative application, load transitions, or transient intermediate state.
It also makes feedback loops and accidental side effects hard to test.

### Option D: feature code calls the client session directly

Input, world, mechanics, and UI sites call session/network APIs wherever an
event occurs. This minimizes adapter plumbing but violates the only-OpenMW-
adapter dependency rule and spreads threading, lifecycle, queue, and protocol
concerns across the engine.

## Decision 3: patch representation and maintenance

### Option A: normal buildable commits plus a machine-checked patch registry (approved)

Land OpenMW-coupled work as normal small Git commits so every shared revision is
buildable, bisectable, and tested. Maintain a vNext patch registry that records
for each non-adapter OpenMW path/seam:

- stable patch ID and owning slice/GDR;
- purpose and exact adapter need;
- affected upstream paths and public contract;
- tests and multiplayer-disabled behavior;
- local-only or upstream-candidate disposition;
- optional local upstream-preparation repository commit/change-set identity;
- upstream issue/MR and accepted/rejected/superseded status only if later
  submission is explicitly authorized;
- last verified OpenMW baseline and rebase notes; and
- removal condition.

The baseline manifest remains the exact tree-difference authority; the registry
adds semantic ownership. CI fails if a tracked non-adapter OpenMW change lacks a
registry entry or an entry names a missing path/test. Baseline updates rehearse
the registered queue in a disposable branch/worktree and publish only after the
full desktop/adapter matrix passes.

This avoids a second source-of-truth patch-file stack while making the permanent
fork surface reviewable. It requires a small registry verifier when the first
engine hook lands.

### Option B: checked-in format-patch/quilt stack applied during the build

Keep OpenMW source pristine in Git and apply numbered patch files before
configuration. This makes the queue visually explicit and portable to another
checkout, but developers and CI build a generated tree different from the
tracked tree, IDE navigation and incremental builds become fragile, and patch
files can drift from actual test evidence. It also conflicts with the current
active-tree provenance model.

### Option C: normal commits with baseline manifest only

Rely on Git history and `BASELINE_PROVENANCE.json` without a semantic registry.
This is the lowest-process choice, but the manifest tracks paths rather than
hook purpose, GDR ownership, upstream disposition, or removal criteria. As the
adapter grows, a baseline update would require rediscovering why each engine
change exists.

### Option D: long-lived integration branch merged periodically

Keep engine patches on a separate branch and merge them into vNext in batches.
This isolates work temporarily but hides unfinished cross-slice coupling,
weakens per-commit buildability, and makes provenance and ownership harder to
review.

## Decision 4: upstream-candidate handling policy

### Option A: track generic candidates locally; upstream only after later approval (approved amendment)

Classify a change as a possible OpenMW upstream candidate when it is useful
without TES3MP, has a general engine contract, preserves normal single-player
behavior, includes upstream-appropriate tests/docs, and does not expose
protocol, transport, canonical-server, or vNext policy types. Examples may
include a clean lifecycle callback, origin-safe application primitive, test
seam, or decoupling refactor that benefits other embedders or engine features.

Keep the multiplayer adapter, client-session composition, semantic command
mapping, server-authoritative policy, and vNext UI/configuration local. Do not
open an upstream issue or merge request during the current policy period merely
because a change is classified as a candidate.

If the owner later authorizes upstream work, create or refresh a separate local
clone of the official OpenMW repository and prepare clean general-purpose
changesets there. The local clone may reference the corresponding vNext patch
registry IDs, but the vNext tree remains the build/test/source-of-truth. The
clone is never nested in this repository, committed as a submodule, required by
CI, or used as an undeclared dependency.

This avoids bothering OpenMW during early product development while preserving
enough classification and clean local history to upstream a genuinely general
improvement later.

### Option B: upstream the complete adapter and multiplayer composition

Seek upstream ownership for the whole client integration. This could minimize a
future fork if accepted, but OpenMW 0.51 does not ship the vNext server/session,
the product timelines and compatibility policies differ, and upstream review
would control TES3MP delivery. It also risks coupling engine and protocol
release decisions.

### Option C: never upstream vNext-motivated changes

Keep every change local regardless of general usefulness. This avoids external
coordination but guarantees a permanent patch burden and duplicates fixes that
could be maintained by the engine project.

### Option D: upstream first, local landing only after acceptance

Require upstream acceptance before using any generic seam. This minimizes
carried patches but makes the vertical slice dependent on external schedules
and can force worse workarounds when a narrow local seam is needed immediately.

## Recommended initial hook budget

This is the approved result of Decisions 1 through 4. It is a budget and review
framework, not approval of exact Phase 8 call sites or gameplay behavior.

| Hook category | Initial allowance | Explicit exclusions |
|---|---|---|
| Composition/lifetime | Optional adapter factory/attachment at executable or `Engine` composition; deterministic startup/shutdown | Global raw session pointer; adapter construction in unrelated subsystems |
| Frame coordination | One main-thread call after input sampling and before world mutation, with bounded work and explicit order | Network-thread OpenMW mutation; an adapter tick scattered through subsystems |
| Semantic input | Adapter reads owned provider values and emits platform-neutral commands | Raw SDL/OpenMW action IDs in protocol/session; state-diff-as-intent |
| Authoritative apply | Adapter uses matching existing APIs or narrow named origin-safe application seams | Generic mutable property API; packet handlers in world/mechanics |
| Lifecycle/cell/object observation | Add only feature-specific events proven necessary by an approved GDR/slice | Comprehensive speculative event bus; polling as the sole semantic source |
| UI/errors | Adapter-facing view model or narrow UI provider | Transport-library errors or credentials passed directly to UI |
| VR | Separate provider/conversion implementation around the same adapter/session | OpenXR or fork types in shared client-session/protocol/core |

The Phase 8.1 owner review must turn this budget into an exact path/call-site
inventory. A hook omitted from that inventory remains unauthorized until the
inventory is amended and reviewed.

## Phase 8.1 approved exact hook inventory

The project owner approved Option A on 2026-09-02. The OpenMW executable creates
the concrete adapter and attaches one narrow coordinator interface to `Engine`.
`Engine` owns the coordinator, invokes it once on the main thread immediately
after `mInputManager->update(frametime, false)` and before the window-visibility
branch, Lua synchronization, state, scripts, mechanics, physics, or world
mutation, and destroys it before any OpenMW subsystem dependency.

| Patch | Exact path and seam | Adapter need | Required evidence | Disposition | Removal condition |
|---|---|---|---|---|---|
| P8-001 | `apps/openmw/engine.hpp` and `apps/openmw/engine.cpp`: optional owned coordinator attachment, one post-input frame call, and ordered reset at the start of `Engine::~Engine` | Stable main-thread lifetime and frame ordering without concrete adapter/session dependencies | disabled-mode equivalence, post-input/pre-world order, main-thread-only call, and shutdown-before-dependencies tests | local; possible general-purpose upstream candidate, but no contact authorized | OpenMW supplies an equivalent reviewed lifecycle/frame seam |
| P8-002 | `apps/openmw/main.cpp`: create and attach the concrete adapter only when multiplayer configuration enables it | Executable-level acyclic composition | disabled mode creates no adapter; enabled construction/attachment failure is bounded and actionable | local product composition | a future supported OpenMW plugin/composition facility fully owns this boundary |
| P8-003 | `apps/openmw/CMakeLists.txt`: link `openmw` to `TES3MP::OpenMWAdapter` after the adapter target is declared, without adding an `openmw-lib` back-edge | Concrete factory availability at the executable boundary | target graph remains acyclic and engine-independent targets remain OpenMW-free | local product build wiring | adapter becomes an independently loaded supported module without a build edge |

The coordinator interface and concrete factory live under `apps/openmw/tes3mp/`.
Those adapter-owned files are not OpenMW baseline patches, but remain subject to
the dependency, main-thread, bounded-work, and no-packet-handling constraints.
No deeper input, cell, world, mechanics, physics, UI, or presentation hook is
approved by this inventory. Such a hook requires an amended inventory and owner
review before implementation.

## Approved acceptance tests and evidence

The owning phases must add these checks:

1. `engine_independent_targets_reject_openmw_sdl_osg_and_transport_headers`
2. `adapter_target_is_the_only_multiplayer_target_with_openmw_dependencies`
3. `openmw_target_graph_is_acyclic`
4. `multiplayer_disabled_preserves_baseline_frame_and_lifecycle_behavior`
5. `network_callback_cannot_mutate_openmw_off_main_thread`
6. `frame_hook_runs_after_input_and_before_world_mutation`
7. `authoritative_apply_does_not_emit_a_local_command`
8. `network_stall_does_not_block_input_render_or_ui`
9. `adapter_shutdown_precedes_openmw_dependency_destruction`
10. `desktop_input_converts_without_openmw_types_crossing_session_boundary`
11. `authoritative_cell_and_root_apply_use_reviewed_origin_safe_paths`
12. `patch_registry_covers_every_non_adapter_openmw_delta`
13. `patch_registry_rejects_missing_path_test_owner_or_disposition`
14. `baseline_update_rehearsal_reports_each_patch_disposition`
15. `vr_provider_compiles_without_fork_types_in_shared_targets` in Phase 9
16. `upstream_prep_clone_is_not_a_submodule_build_input_or_ci_requirement`

Phase 8.1 must retain an owner-reviewed map from each exact OpenMW change to its
adapter need, test, upstream disposition, and removal condition. Phase 8's demo
must include multiplayer-disabled startup, two desktop clients, authoritative
correction without feedback, a network stall, disconnect/shutdown, and a trace
showing all OpenMW mutation on the main thread.

## Consequences of the approved decision

- The first OpenMW change is a small composition/frame seam, not a gameplay-
  wide event system.
- The adapter may depend on OpenMW and the client session; the client session
  never depends on the adapter or engine.
- Some existing OpenMW APIs will be reused directly, while methods with wrong
  side effects require narrow origin-safe seams rather than copied internals.
- The patch registry and baseline manifest serve different purposes and both
  remain required.
- General engine improvements are classified and tracked, but no proactive
  OpenMW submission occurs until a later explicit owner decision. A separate
  local clone may prepare clean changesets without becoming project state.
- Lua remains available for engine scripting but does not become the native
  multiplayer session boundary or the Phase 19 server scripting API.
- PC VR maintenance remains separately governed by ADR-0008 and Phase 9.

## Failure modes and mitigations

- **CMake cycle between engine and adapter:** compose concrete objects at the
  executable boundary and add a build-graph regression test before sources grow.
- **Main-thread stall:** bound session draining/application work; never wait for
  network progress inside the frame hook.
- **Feedback loop:** tag authoritative-origin application or use a dedicated
  apply path; test that it cannot emit a local semantic command.
- **Engine types leak inward:** forbidden-include/link checks cover protocol,
  transport abstraction, session, server core, and test support.
- **Generic hook surface becomes a mutation bypass:** require one approved
  semantic need, typed arguments, contract test, registry entry, and owner review
  for each deeper hook.
- **Lua and adapter both apply the same event:** define one owning path per
  feature; presentation events may be exposed to Lua only after canonical apply
  and with duplicate/reentrancy tests.
- **Upstream method side effects repeat gameplay:** inventory complete effects
  and add a narrow origin-safe seam instead of global suppression.
- **Patch registry drifts:** fail CI when registered paths/tests disappear or
  unregistered non-adapter OpenMW deltas appear.
- **Local upstream-preparation clone is lost or stale:** treat it as disposable;
  reconstruct candidates from authoritative vNext commits/registry entries and
  record its upstream base before preparing a changeset.
- **Local clone becomes a hidden dependency:** prohibit nesting/submodules and
  verify that build, tests, CI, and release inputs never reference its path.
- **Baseline update loses a hook:** disposable rebase rehearsal, registry audit,
  single-player baseline tests, and full adapter matrix gate publication.
- **VR fork adds a second adapter:** ADR-0008 must compose provider interfaces
  around this adapter/session and keep fork types local.

## Review and replacement triggers

Reopen this ADR if:

- the target graph cannot remain acyclic with a dedicated app-local adapter;
- required behavior needs protocol/session types in OpenMW world, input,
  mechanics, physics, Lua, renderer, or UI subsystems;
- existing OpenMW APIs and narrow semantic seams cannot apply authoritative
  presentation without duplicating substantial engine logic;
- network processing must block or mutate the engine from a non-owning thread;
- the patch registry grows across multiple engine subsystems without matching
  approved feature needs and removal/upstream plans;
- the owner authorizes contacting OpenMW or submitting an upstream issue/merge
  request;
- upstream independently accepts a replacement seam that makes a local patch
  obsolete;
- a new OpenMW baseline materially changes composition, frame ordering, Lua
  synchronization, player/cell identity, or application APIs; or
- PC VR cannot reuse the same adapter/session through platform providers.

## Owner approval

Approved by the project owner in the 2026-08-26 working session: Option A for
Decisions 1 through 3 and amended Option A for Decision 4.

For Decision 4, the owner directed the project not to bother OpenMW with patches
for now. General-purpose candidates remain classified in the patch registry,
and a separate developer-local OpenMW repository may hold clean changesets if
upstreaming is later desired. No upstream contact or submission is authorized
by this ADR; that requires a later explicit owner decision.

This approval establishes the integration and patch-maintenance framework.
The project owner approved the Phase 8 Slice 8.1 exact path/call-site inventory
and Option A narrow coordinator boundary on 2026-09-02. The relevant GDRs still
own player-visible behavior.

## 2026-09-03 remote-replica review authorization

The owner approved focused Option B research into a first-class, protocol-
agnostic replicated-remote-actor role. This reopens the adequacy of the P8-004
renderer-only seam without replacing this ADR's adapter, main-thread ownership,
dependency-direction, or patch-registry rules. No new engine hook or subsystem
participation is approved yet.

The research must compare a renderer-only proxy, a normal actor with exclusions,
and a first-class ephemeral replica role. It must present authority, lifetime,
identity, registration, rendering, animation, mechanics, physics, scripts,
persistence, interaction, and testing choices for owner approval before ADR,
GDR, registry, plan, or production implementation selects a replacement.

## 2026-09-03 C-R1 remote-replica approval

The project owner approved package C-R1 in the
[remote actor owner decision packet](../REMOTE_ACTOR_OWNER_DECISION_PACKET.md).
P8-004 is superseded in place by a first-class, ephemeral replicated-actor
render role. The adapter owns one opaque RAII handle per authoritative
`EntityId`; the role is client-local, main-thread-only, capacity-bounded, and
default-deny outside rendering and passive renderer-local neutral idle
animation.

The approved patch surface is limited to the exact OpenMW rendering, visibility,
build, and focused test paths named by that packet. No `CellStore`, `WorldModel`,
scene insertion, mechanics, physics, navigation, Lua/script, persistence,
protocol, server-core, or transport change is approved. Discovering that one of
those paths is necessary reopens owner review before implementation expands.
P8-001 through P8-003 and all dependency, main-thread, bounded-work, registry,
and upstream-contact rules remain unchanged.

## 2026-09-03 C-R1 implementation acceptance

The project owner accepted implementation `93690354d1` after the exact P8-004
registry, provenance, negative-registration, build, and real-content gates
passed. The accepted implementation did not add an unlisted OpenMW subsystem
path. Phase 8 is complete; ADR-0008 and Phase 9 now own any PC VR fork,
worktree, or provider expansion.
