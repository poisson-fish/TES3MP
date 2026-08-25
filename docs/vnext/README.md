# TES3MP vNext clean-break architecture plan

Status: accepted direction

Updated: 2026-08-25

Archived source branch point: `49be5b6405d6ab427e06ed350cf76c715a1f3bdd` (`tes3mp-0.8.1-20-g49be5b640`)

Target engine baseline: OpenMW `openmw-0.51.0` (`f4bec41444214a7903bebd178389ca22ca13f646`)

## Executive decision

vNext is a full rearchitecture with deliberate, complete compatibility breakage.

The active vNext source tree will move directly to a clean OpenMW 0.51 baseline. The TES3MP 0.8.1 tree will be retained only as archived Git history and a gameplay-feature reference. We will not merge its multiplayer implementation into OpenMW 0.51, make its protocol portable, or preserve its server and scripting architecture.

The first product milestone is a newly designed end-to-end multiplayer vertical slice on OpenMW 0.51. It will use a new protocol, transport boundary, authoritative server core, and thin OpenMW adapter from its first commit.

PC VR is a required secondary client target. Standalone Meta Quest 3 is a later stretch target. Both must use the same engine-independent multiplayer core and must not introduce VR assumptions into durable world state.

## Compatibility policy

There is no compatibility requirement with TES3MP 0.8.x.

- No wire-protocol compatibility.
- No packet ID or message-layout compatibility.
- No old client/new server or new client/old server interoperability.
- No CrabNet/RakNet compatibility layer.
- No server configuration compatibility.
- No CoreScripts API compatibility.
- No persistence or save-data migration guarantee.
- No requirement to preserve undocumented legacy behavior or bugs.

The vNext handshake must reject legacy peers clearly. The first vNext release must state that it requires new clients, servers, scripts, configuration, and persistence data.

## Upstream status

OpenMW has not upstreamed or taken over maintenance of TES3MP's multiplayer implementation as of OpenMW 0.51.0.

- The stable source tree contains no TES3MP/OpenMW-MP networking subsystem, RakNet/CrabNet integration, multiplayer server, or replicated-world implementation.
- OpenMW's Lua API was designed to be conceptually compatible with multiplayer and TES3MP's client-delegated model.
- The same documentation explicitly says that multiplayer is not implemented.
- OpenMW 0.51 contains useful modern Lua, testing, and Android foundations, but not a replacement for the multiplayer protocol or server.

Primary upstream references:

- [OpenMW 0.51 Lua scripting overview](https://gitlab.com/OpenMW/openmw/-/blob/openmw-0.51.0/docs/source/reference/lua-scripting/overview.rst#L85-90)
- [OpenMW 0.51 world-player placeholder](https://gitlab.com/OpenMW/openmw/-/blob/openmw-0.51.0/files/lua_api/openmw/world.lua#L12-15)
- [OpenMW 0.51 source tree](https://gitlab.com/OpenMW/openmw/-/tree/openmw-0.51.0)

## What “clean slate” means

The clean slate is reached when all of the following are true:

- The active `vnext` tree is OpenMW 0.51 plus vNext-owned documentation and deliberately added code.
- No TES3MP 0.8 multiplayer source is part of the active build.
- Protocol and server-core targets compile without OpenMW headers.
- OpenMW-specific behavior is isolated behind a documented client adapter/hook surface.
- A clean clone builds with pinned dependencies in CI.
- The server and two automated clients can connect, enter a cell, move, disconnect, and reconnect.
- Packet decoders pass malformed-input fuzzing under sanitizers.
- Tests can inject latency, loss, jitter, duplication, reordering, and disconnects.
- Every replicated entity has stable identity, revision, authority epoch, and resynchronization behavior.
- The replacement server scripting API is versioned and tested with its engine and protocol.

The archived code may answer questions about Morrowind feature coverage. It is not an implementation template.

## Process flow

```text
Tag and archive TES3MP 0.8.1
              |
              v
Replace active vnext tree with exact OpenMW 0.51 baseline
              |
              v
Add new protocol, transport boundary, server core, and test harness
              |
              v
Desktop vertical slice
connect -> join -> cell -> movement -> reconnect
              |
              v
PC VR capability and pose integration
              |
              v
Implement gameplay as authoritative vertical slices
              |
              v
New scripting and persistence model
              |
              v
Stabilize desktop + PC VR release
              |
              v
Quest 3 Android/OpenXR feasibility and port (stretch)
```

## Target architecture

```text
                       +-- desktop input/presentation
OpenMW client adapter -+-- PC OpenXR input/presentation
                       +-- Quest Android/OpenXR (stretch)
             |
             | PlayerCommand / WorldSnapshot / PresentationEvent
             v
Versioned multiplayer protocol <----> maintained transport adapter
             |
             v
Single-writer authoritative server
             |
             +-- canonical world state and revisions
             +-- interest management
             +-- snapshot and event replication
             +-- authority leases and handoff
             +-- scripting event/command boundary
             +-- persistence and replay
             +-- observability
```

### `multiplayer-protocol`

- Contains schemas, bounded encoding/decoding, capability negotiation, stable IDs, ticks, revisions, and authority epochs.
- Has no OpenMW, Lua, desktop, VR, Android, or transport-library dependency.
- Separates latest-wins snapshots from reliable operations.
- Supports round-trip, golden-schema, compatibility-within-vNext, property, and fuzz tests.

### `multiplayer-transport`

- Exposes connect, disconnect, datagram/snapshot, reliable event, encryption/authentication, backpressure, and telemetry semantics.
- Uses a maintained dependency selected through an architecture decision.
- Never exposes transport-library types to protocol or game-state code.
- Must support Windows, Linux, macOS, and eventually Android ARM64.

### `server-core`

- Owns canonical durable state and processes commands through one deterministic writer.
- Validates commands before applying them.
- Emits versioned state changes for replication, persistence, scripts, metrics, and replay.
- Publishes immutable snapshots to auxiliary threads.
- Has no renderer, OpenMW runtime, VR, or platform dependency.

### `openmw-adapter`

- Converts OpenMW events into protocol commands.
- Applies authoritative snapshots to local and remote presentation state.
- Contains every direct dependency on OpenMW internals.
- Uses provider interfaces for desktop, VR, and future platform-specific input/presentation.
- Keeps its engine patch surface small, documented, and suitable for upstream contribution where possible.

### `server-scripting`

- Receives immutable typed events and returns explicit validated commands.
- Does not expose packet layouts or global mutable read/write buffers.
- Converts bad inputs and script failures into recoverable errors.
- Is versioned with the server API and treated as part of the tested product.

## Source-control cutover

- Preserve the existing `0.8.1` branch.
- Create a permanent annotated tag at `49be5b640` before replacing the active tree.
- Add the official OpenMW GitLab repository as `openmw-upstream` and fetch the pinned stable tag.
- Record a baseline-cutover commit on the vNext line whose source tree is OpenMW 0.51 plus the vNext plan. Do not perform a textual merge of the old TES3MP engine modifications.
- Retain the archived TES3MP history through the branch/tag and document the exact upstream tree used for the cutover.
- Verify the baseline tree against `openmw-0.51.0`; every difference must be intentional and reviewed.
- Add new multiplayer code only after the clean OpenMW baseline builds and its tests pass.
- Maintain unavoidable OpenMW integration changes as a narrow, purpose-specific patch queue.
- Do not force-push shared vNext history after the cutover is published.

The exact Git mechanics of the baseline-cutover commit must be recorded in ADR-0001 before executing it. The desired result is unambiguous: vNext history remains traceable to the archived project, while its active source is clean OpenMW 0.51 rather than a conflict-resolved hybrid.

## Delivery phases and gates

### Phase 0 — Archive and baseline cutover

Deliverables:

- Tag the archived TES3MP commit.
- Inventory gameplay features at a high level; do not inventory every packet or recreate the legacy build matrix.
- Record supported desktop and PC VR goals and Quest 3 as a stretch target.
- Replace the active source tree with the pinned OpenMW 0.51 baseline.
- Establish Linux, Windows, and macOS CI for the unmodified engine baseline.
- Record the initial architecture decisions listed below.

Exit gate:

- The source tree matches OpenMW 0.51 except for reviewed vNext documentation/build scaffolding.
- OpenMW builds and its upstream tests run in vNext CI.
- No 0.8 multiplayer target is compiled.

### Phase 1 — Architecture scaffold

Deliverables:

- Create independent protocol, transport, server-core, client-session, adapter, and test targets.
- Select a maintained transport and schema/codec format.
- Add bounded decoding, explicit size limits, and capability negotiation from the start.
- Add ASan, UBSan, fuzzing, and a deterministic fake-client simulator.
- Define stable `PlayerId`, `EntityId`, canonical `CellId`, tick, revision, command ID, and authority epoch types.
- Define platform-neutral player commands and presentation snapshots.

Exit gate:

- Protocol and server targets compile and test without linking OpenMW.
- A simulated client can complete a handshake and exchange state entirely in memory.
- Malformed messages cannot cause partial state commits or unbounded allocation.

### Phase 2 — New desktop vertical slice

Implement only:

1. Start server and OpenMW 0.51 client.
2. Negotiate vNext protocol and capabilities.
3. Authenticate and create a player session.
4. Enter one interior and one exterior cell.
5. Spawn and observe another player.
6. Exchange input commands and timestamped movement snapshots.
7. Interpolate remote movement from a jitter buffer.
8. Disconnect and resume the session cleanly.

The initial server state contains only player identity, connection/session, canonical cell, root transform, velocity, revision, and acknowledgement state.

Exit gate:

- Two automated desktop clients complete the slice.
- The slice passes configured latency, loss, jitter, duplication, and reordering profiles.
- Queues remain bounded and stale snapshots/operations are rejected.
- The OpenMW adapter is isolated and its required engine hooks are documented.

### Phase 3 — PC VR integration gate

Full VR is currently maintained in the separate OpenMW-VR fork, not official OpenMW 0.51:

- [OpenMW-VR source](https://gitlab.com/madsbuvi/openmw/-/tree/openmw-vr)
- [OpenMW-VR installation documentation](https://gitlab.com/madsbuvi/openmw/-/blob/openmw-vr/docs/source/manuals/installation/install-openmw-vr.rst)
- [OpenMW-VR versioning and upstream intent](https://gitlab.com/madsbuvi/openmw/-/blob/openmw-vr/docs/source/manuals/openmw-vr/versioning.rst)

Do not merge the VR fork into the multiplayer core. Maintain a separate engine worktree/patch target until VR is upstreamed.

Deliverables:

- Build the OpenMW adapter against desktop OpenMW and OpenMW-VR.
- Negotiate an optional `vr_pose` protocol capability.
- Keep authoritative locomotion in a platform-neutral player root/capsule.
- Replicate optional head and hand poses as sampled latest-wins presentation snapshots.
- Render sensible fallback poses for desktop peers.
- Convert desktop and VR controls into the same semantic player commands.

Exit gate:

- Two desktop clients still pass the vertical slice.
- One desktop and one PC VR client interoperate.
- Clients without `vr_pose` safely ignore VR presentation data.
- VR rendering/tracking rate is independent of network snapshot rate.

### Phase 4 — Authoritative gameplay slices

Implement in dependency order:

1. Player lifecycle, content manifests, moderation, and reconnect.
2. Canonical cells, interest management, and initial snapshots.
3. Player movement, animation, and VR presentation poses.
4. Actor lifecycle, movement, AI state, and authority handoff.
5. Object activation, placement, locks, traps, and doors.
6. Inventory, equipment, containers, and idempotent item transactions.
7. Combat, stats, spells, projectiles, death, and resurrection.
8. Dialogue, journals, factions, quests, weather, time, and world state.
9. Server scripting, persistence, administration, and discovery.

Every slice must define:

- Commands, authority, and validation.
- Schema and bounds.
- Revision and idempotency behavior.
- Initial snapshot and resynchronization behavior.
- Interest rules.
- Desktop and relevant VR presentation behavior.
- Loss, reorder, reconnect, and contention tests.
- Metrics and structured logging.

### Phase 5 — Server scripting and persistence

Deliverables:

- Introduce a typed event/command scripting API designed for the new canonical server.
- Decide whether to reuse Lua as a language without reusing the old binding API.
- Bundle or pin server scripts with their exact API/protocol version.
- Add transactional persistence, schema versioning, backup, and restore tests.
- Add deterministic replay for diagnosing state divergence.

Exit gate:

- Script failure cannot corrupt canonical state or terminate the server unexpectedly.
- Persistence restores the same canonical revisions and entity identities.
- A recorded command stream reproduces the same server-state checksum.

### Phase 6 — Desktop and PC VR stabilization

Deliverables:

- Complete fault, soak, security, and performance testing.
- Publish supported operating systems, OpenMW baseline, protocol policy, and scripting API.
- Package desktop client, PC VR client, and dedicated server artifacts.
- Explicitly document incompatibility with every 0.8.x component.

Exit gate:

- Desktop and PC VR release criteria pass without using archived code.
- Reconnect, resync, and authority transfer recover without restarting the server.
- Long-running tests show bounded queues and no accumulating divergence.

### Phase 7 — Meta Quest 3 standalone stretch target

OpenMW 0.51 already contains Android ARM64 build support, including Android CI and an Android entry point:

- [OpenMW 0.51 Android CI configuration](https://gitlab.com/OpenMW/openmw/-/blob/openmw-0.51.0/CI/before_script.android.sh)
- [OpenMW 0.51 Android entry point](https://gitlab.com/OpenMW/openmw/-/blob/openmw-0.51.0/apps/openmw/androidmain.cpp)

OpenMW-VR does not currently provide a supported standalone Quest build. Community experiments demonstrate feasibility but are not vNext dependencies:

- [Native Quest OpenMW-VR proof of concept](https://github.com/tomups/openmw-vr-quest) — OpenXR plus GLES/gl4es; explicitly not under active development.
- [Experimental OpenMW-VR Quest port](https://github.com/mmry2940/openmw-vr-quest) — Android ARM64/OpenXR packaging and controller work.

Quest work begins only after the desktop and PC VR architecture is stable.

Feasibility gate:

- Cross-compile the client and dependencies for Android ARM64 with a current supported NDK.
- Package and launch a minimal native OpenXR APK on Quest 3.
- Select and validate the rendering route: native GLES/Vulkan or a measured compatibility layer.
- Initialize the Android OpenXR loader and Quest controller profiles.
- Implement Android lifecycle, suspend/resume, audio, networking, scoped storage, and legal game-data import.
- Establish CPU, GPU, memory, thermal, loading-time, and battery budgets on real hardware.
- Identify OpenMW/OpenMW-VR changes that can be upstreamed instead of permanently forked.

Quest multiplayer gate:

- A Quest 3 client completes the same vNext connect, join, cell, movement, and reconnect slice.
- Quest, desktop, and PC VR clients interoperate without platform-specific server logic.
- Head/hand presentation uses the existing optional VR capability.
- Suspend/resume either resumes safely or performs an explicit clean reconnect.
- A hardware soak test stays inside the defined thermal and memory budgets.

## Synchronization and authority rules

- Movement uses latest-wins sequenced snapshots, never reliable ordered delivery.
- Snapshots contain simulation tick, sequence, transform, velocity, and authority epoch.
- Clients render remote entities from a short jitter buffer with bounded interpolation/extrapolation.
- Reliable operations carry unique command IDs and expected entity revisions.
- The server rejects stale revisions and previous authority epochs.
- Authority transfer uses a lease plus a complete atomic snapshot before deltas resume.
- Periodic checksums/snapshots detect and repair latent divergence.
- Local player movement is predicted but server-validated.
- Durable gameplay state is server-owned.
- Any client-authoritative subsystem requires an explicit documented exception and threat analysis.

## VR and platform rules

- Authoritative player movement is the platform-neutral root/capsule, not the headset pose.
- Desktop, PC VR, and Quest controls produce the same semantic command types.
- HMD and controller tracking render locally at platform rate and replicate at a lower sampled rate.
- Room-scale head motion cannot silently move the authoritative root.
- Physical interactions validate reach relative to the authoritative root.
- VR presentation fields are optional protocol capabilities, not separate protocols.
- The server contains no OpenXR, Android, renderer, or headset-specific code.

## Protocol rules

- Never use engine commit hashes as protocol compatibility.
- Negotiate semantic protocol versions and explicit capabilities.
- Every decoder has a byte budget and collection limits.
- Every durable entity has a stable ID and monotonically increasing revision.
- Every apply-once operation has an idempotency ID.
- Every delegated authority lease has an epoch and expiry.
- Snapshots and transactions use distinct message classes and delivery semantics.
- Unknown optional capabilities are ignored safely; unknown required capabilities fail clearly.
- Logs and traces never contain supplied passwords or reusable authentication secrets.

## Testing strategy

Testing is part of each feature slice.

- Unit tests: codecs, revisions, interpolation, cell IDs, interest, authority transitions, reducers.
- Property tests: round-trip encoding, bounded decoding, idempotency, revision monotonicity.
- Fuzz tests: every network decoder and script boundary.
- Golden tests: vNext schema evolution only, never legacy packet compatibility.
- Simulation tests: deterministic server plus fake clients without rendering.
- Integration tests: real server plus at least two minimal OpenMW clients.
- Platform tests: desktop in CI, PC VR where automation permits, Quest on hardware runners later.
- Fault tests: loss, latency, jitter, duplication, reordering, disconnects, stalls, malformed input.
- Soak tests: cell changes, authority churn, reconnect loops, and transaction contention.
- Sanitizers: ASan, UBSan, and a race detector on supported runners.

## Observability required before tuning

Capture at minimum:

- Server tick and client snapshot age.
- Per-channel sent, received, dropped, retransmitted, and queued messages.
- Interpolation-buffer depth and extrapolation time.
- Correction distance and hard-snap count.
- Entity revision conflicts and rejected stale operations.
- Authority owner, epoch, transfer reason, and transfer duration.
- Full-resync requests and causes.
- Script callback duration and failures.
- Client platform/capabilities without collecting sensitive hardware identifiers.

## Required architecture decisions

1. Baseline-cutover Git mechanics.
2. Transport, encryption, and authentication.
3. Protocol schema/codec format and evolution policy.
4. Server authority for movement, actors, combat, inventory, and scripts.
5. OpenMW hook policy and upstream-contribution strategy.
6. PC VR fork/worktree maintenance strategy.
7. Scripting language/runtime and API model.
8. Persistence model.
9. Supported desktop toolchains and platforms.
10. Quest 3 rendering, packaging, and hardware-support policy before Phase 7.

## Immediate implementation backlog

1. Create the permanent annotated archive tag at `49be5b640`.
2. Add and pin the official OpenMW upstream remote/tag.
3. Record ADR-0001 and perform the clean OpenMW 0.51 baseline cutover.
4. Verify the new tree and run upstream OpenMW tests in CI.
5. Record the threat model and authoritative-state policy.
6. Decide the maintained transport and protocol schema.
7. Scaffold protocol, transport, server-core, client-session, and adapter targets.
8. Add sanitizer, fuzz, and deterministic simulation infrastructure.
9. Implement the new desktop vertical slice.
10. Establish the PC VR adapter target before starting the long gameplay feature list.

Do not repair or port the legacy packet/processors architecture. Do not begin inventory, combat, actor, or scripting implementation until the new vertical slice passes its exit gate.

## Definition of done for vNext

- Built on a supported OpenMW stable release with an explicit update policy.
- Reproducible, sanitizer-clean CI on supported desktop platforms.
- Standalone bounded protocol and deterministic authoritative server core.
- Thin, documented OpenMW adapter/patch surface.
- Desktop and PC VR clients use the same protocol and server behavior.
- Versioned scripting API and bundled or pinned server scripts.
- Measurable, tested behavior under adverse networks.
- Recoverable reconnect, resync, and authority transfer.
- No legacy protocol, transport, packet processors, scripting ABI, or engine code in the active build.
- Quest 3 feasibility remains isolated until the desktop/PC VR foundation is stable.
