# TES3MP vNext recovery and migration plan

Status: proposed  
Created: 2026-08-25  
Legacy branch point: `49be5b6405d6ab427e06ed350cf76c715a1f3bdd` (`tes3mp-0.8.1-20-g49be5b640`)  
Target engine baseline: OpenMW `openmw-0.51.0` (`f4bec41444214a7903bebd178389ca22ca13f646`)

## Executive decision

Do not merge OpenMW 0.51 wholesale into the current OpenMW 0.47-derived tree, and do not complete a large redesign while still coupled to OpenMW 0.47.

Use a staged reverse-port instead:

1. Preserve and characterize the working 0.8.1 behavior.
2. Extract only the engine-independent protocol, transport, server-state, and test seams needed for portability.
3. Prove those seams against OpenMW 0.51 with one end-to-end multiplayer vertical slice.
4. Port the remaining features slice by slice onto the 0.51 baseline.
5. Introduce the new replication and authority model behind versioned protocol capabilities as each slice moves.
6. Retire the 0.47 engine tree after parity and soak gates pass.

This avoids two bad failure modes: resolving years of upstream conflicts before the multiplayer boundaries are understood, or investing heavily in abstractions shaped by an engine version that will be removed.

## Upstream status

OpenMW has not upstreamed or taken over maintenance of TES3MP's multiplayer implementation as of OpenMW 0.51.0.

- The stable source tree contains no TES3MP/OpenMW-MP networking subsystem, RakNet/CrabNet integration, multiplayer server, or replicated-world implementation.
- OpenMW's Lua documentation says its scripting API was designed to be conceptually compatible with multiplayer and TES3MP's client-delegated model.
- The same documentation explicitly says that the scripting system does not actually work with multiplayer yet.
- Current Lua APIs contain placeholders for multiple players, but they currently expose a single player.

Primary upstream references:

- [OpenMW 0.51 Lua scripting overview](https://gitlab.com/OpenMW/openmw/-/blob/openmw-0.51.0/docs/source/reference/lua-scripting/overview.rst#L85-90)
- [OpenMW 0.51 nearby-player API placeholder](https://gitlab.com/OpenMW/openmw/-/blob/openmw-0.51.0/files/lua_api/openmw/nearby.lua#L28-31)
- [OpenMW 0.51 world-player API placeholder](https://gitlab.com/OpenMW/openmw/-/blob/openmw-0.51.0/files/lua_api/openmw/world.lua#L12-15)
- [OpenMW 0.51 source tree](https://gitlab.com/OpenMW/openmw/-/tree/openmw-0.51.0)

Implication: upstream provides a better modern engine and useful multiplayer-aware scripting boundaries, but not a replacement for TES3MP's transport, server, authority, replication, persistence, or integration code.

## What “clean slate” means

The clean slate is reached when all of the following are true:

- The active client is built on the pinned OpenMW stable baseline.
- Protocol and server-core targets compile without OpenMW headers.
- OpenMW-specific behavior is isolated behind a client adapter and a documented patch/hook surface.
- Dependencies and toolchains are pinned, and a clean clone builds in CI.
- A server and two automated clients can connect, enter a cell, move, disconnect, and reconnect in a repeatable integration test.
- Packet parsers pass malformed-input fuzzing under sanitizers.
- Loss, latency, jitter, duplication, and reordering can be injected in tests.
- Every replicated entity has a stable identity, revision, authority epoch, and resynchronization path.
- CoreScripts or their successor are versioned and tested with the engine and protocol they target.
- The old OpenMW 0.47 engine tree is no longer part of the active vNext build.

The clean slate does not require discarding all existing behavior. Existing behavior is evidence to characterize, then deliberately reimplement or preserve behind explicit interfaces.

## Process flow

```text
Freeze 0.8.1 baseline
        |
        v
Reproducible legacy build + behavior/packet characterization
        |
        v
Extract portable seams and harden unsafe packet decoding
        |
        +----------------------------+
        |                            |
        v                            v
Legacy adapter remains runnable   OpenMW 0.51 port worktree
                                     |
                                     v
                         Vertical slice: connect -> spawn
                           -> enter cell -> move -> quit
                                     |
                           +---------+---------+
                           |                   |
                         passes              blocked
                           |                   |
                           v                   v
                  Port feature slices   Document/request the
                  with vNext protocol   smallest upstream hooks;
                  and authority rules   keep a narrow patch queue
                           |
                           v
                  Parity, fault, and soak gates
                           |
                           v
                    Make vNext the default
                           |
                           v
                   Retire the 0.47 build path
```

## Target architecture

```text
OpenMW client
    |
    v
OpenMW adapter
    |  player intent / presentation state
    v
Versioned protocol library <----> transport adapter
    |                               (legacy CrabNet only where needed)
    v
Single-writer server command loop
    |
    +-- canonical world state and revisions
    +-- interest management
    +-- replication: snapshots + reliable events
    +-- authority leases and handoff
    +-- scripting command/event boundary
    +-- persistence
    +-- observability and replay
```

### Required boundaries

`multiplayer-protocol`

- Contains message schemas, bounded encoding/decoding, capability negotiation, stable IDs, ticks, revisions, and authority epochs.
- Has no OpenMW or Lua dependency.
- Supports golden-message, round-trip, compatibility, and fuzz tests.
- Keeps legacy protocol decoding isolated from vNext messages.

`multiplayer-transport`

- Exposes connect, disconnect, datagram/snapshot, reliable-event, backpressure, and telemetry semantics.
- Does not expose RakNet types outside its legacy adapter.
- Selects a future transport through a separate architecture decision; changing transport must not change world-state code.

`server-core`

- Owns canonical durable state and processes commands through one deterministic writer.
- Validates commands before applying them.
- Emits versioned state changes for replication, persistence, scripts, and replay.
- Publishes immutable snapshots to auxiliary threads such as the master-server announcer.

`openmw-adapter`

- Converts OpenMW events into protocol commands and applies server snapshots to presentation state.
- Contains all direct dependencies on OpenMW internals.
- Is kept small enough that its patch/hook requirements can be reviewed when OpenMW changes.

`server-scripting`

- Receives immutable typed events and returns explicit commands.
- Does not expose global mutable read/write packet buffers.
- Converts bad indices and script failures into recoverable errors, not process termination.
- Versions scripts alongside the protocol and server API.

## Branch and repository strategy

- `0.8.1` remains the legacy maintenance branch.
- `vnext` is the integration and planning branch created from the current 0.8.1 HEAD.
- Tag the current branch point as a permanent migration baseline before code-changing work begins.
- Add the official OpenMW GitLab repository as a read-only `openmw-upstream` remote.
- Create the future OpenMW port worktree/branch from the pinned stable tag, not by merging the entire stable tree into the legacy working tree.
- Move portable commits through small, reviewable cherry-picks or patches. Do not copy the old engine tree into the port branch.
- Maintain any unavoidable OpenMW changes as a narrow patch queue with one purpose per commit. Prefer generally useful hooks suitable for upstream contribution.
- Merge completed port slices into `vnext` only after their parity and fault tests pass.
- Do not force-push shared migration history after implementation work starts.

The exact mechanics of making the port branch the eventual default branch should be recorded in an architecture decision before the first port merge. Preserve the legacy tag and branch regardless of the chosen cutover mechanics.

## Delivery phases and gates

### Phase 0 — Freeze and inventory

Deliverables:

- Record the legacy commit, OpenMW target tag, compiler matrix, dependencies, CoreScripts version, and supported operating systems.
- Tag the migration baseline.
- Produce an inventory of multiplayer-owned files and modifications to upstream OpenMW files.
- Classify every engine modification as required hook, workaround, obsolete change, or unknown.
- Record supported gameplay features and known desync/reconnect failures.

Exit gate:

- Every downstream OpenMW modification has an owner/category and a proposed 0.51 disposition.
- No unpinned external dependency remains in the documented build recipe.

### Phase 1 — Reproduce and characterize legacy behavior

Deliverables:

- Make a clean 0.8.1 build reproducible on at least Linux first, then Windows and macOS.
- Add server-start tests with master-server registration enabled and disabled.
- Add a two-client smoke harness.
- Create a packet catalogue: ID, direction, channel, reliability, maximum encoded size, authority, and handler.
- Capture golden legacy packets and representative play-session traces.
- Define invariants for connect, login, cell change, movement, inventory, combat, death, reconnect, and authority transfer.

Exit gate:

- CI can reproduce a minimal multiplayer session and retain logs/artifacts.
- Legacy behavior is testable without relying solely on manual play.

### Phase 2 — Safety hardening and portable seams

Deliverables:

- Introduce a bounded packet reader with central validity propagation and limits for all strings, collections, indices, and packet sizes.
- Correct payload-length handling and reject malformed messages before processors run.
- Fuzz packet decoding under ASan and UBSan.
- Replace packet-facing RakNet types with protocol-owned value types.
- Add transport and client-world interfaces around existing behavior.
- Fix master-client null handling, ownership, stop signaling, and access to the player collection.
- Pin CrabNet only as the legacy transport dependency.

Exit gate:

- Protocol tests compile independently of OpenMW.
- No malformed packet can cause an unbounded allocation or partially committed state.
- The legacy server/client still complete the smoke test through the new seams.

### Phase 3 — OpenMW 0.51 vertical-slice spike

Build one deliberately narrow path on the pinned OpenMW baseline:

1. Start client and server.
2. Negotiate protocol capabilities.
3. Authenticate and create a player.
4. Enter one interior and one exterior cell.
5. Observe another player and exchange movement snapshots.
6. Disconnect and reconnect cleanly.

Use current OpenMW Lua APIs where they provide an appropriate boundary. Add direct C++ hooks only when latency, authority, or missing API makes Lua unsuitable.

Exit gate:

- The slice runs with two automated clients under normal conditions and injected latency/loss.
- Required OpenMW patches are documented, small, and covered by adapter tests.
- The team confirms that reverse-porting is viable before porting the long feature tail.

### Phase 4 — Port feature slices and introduce protocol vNext

Port in dependency order:

1. Handshake, capabilities, player lifecycle, and reconnect.
2. Canonical cell IDs and interest management.
3. Player movement and animation.
4. Actor lifecycle, movement, AI state, and authority handoff.
5. Object activation, placement, locks, traps, and doors.
6. Inventory, equipment, containers, and item transactions.
7. Combat, dynamic stats, spells, projectiles, death, and resurrection.
8. Dialogue, journals, factions, quests, weather, time, and world state.
9. Server scripting, persistence, administration, and server browser.

Each slice must include:

- A command/authority definition.
- Schema and bounds.
- Revision and idempotency behavior.
- Initial snapshot and resync behavior.
- Interest rules.
- Legacy-parity tests where parity is intended.
- Loss/reorder/reconnect tests.
- Metrics and structured logging.

### Phase 5 — Synchronization and authority completion

Required model:

- Movement uses latest-wins sequenced snapshots, not reliable ordered delivery.
- Snapshots contain simulation tick, sequence, position, rotation, velocity, and authority epoch.
- Clients render from a short jitter buffer with bounded interpolation/extrapolation.
- Reliable events carry unique operation IDs and expected entity revisions.
- The server rejects stale revisions and previous authority epochs.
- Authority transfer uses a lease plus a complete atomic snapshot before deltas resume.
- Periodic checksums/snapshots detect and repair latent divergence.
- Player movement remains predicted locally but server-validated.
- Durable gameplay state is server-owned; any deliberately client-authoritative system is documented as such.

Exit gate:

- Automated tests pass under configured loss, jitter, duplication, and reordering profiles.
- A long-running soak test shows bounded queues and no monotonically increasing correction or state divergence.
- Reconnect and authority transfer recover without restarting the server.

### Phase 6 — Cutover and retirement

Deliverables:

- Make the OpenMW 0.51-based client and new server the default vNext build.
- Publish protocol and scripting compatibility policy.
- Provide migration tooling or an explicit incompatibility notice for server data and CoreScripts.
- Keep 0.8.1 available only for legacy compatibility and critical fixes.
- Remove old engine code, processors, packet types, and global read/write lists once their replacement slices have passed the gates.

Exit gate:

- No vNext target compiles the OpenMW 0.47 engine path.
- CI, documentation, packaging, and release artifacts all use the pinned modern baseline.

## Protocol rules

- Never use exact engine commit hashes as protocol compatibility.
- Negotiate semantic protocol versions and explicit capabilities.
- Every decoder has a byte budget and collection limits.
- Every durable entity has a stable ID and monotonically increasing revision.
- Every operation that must apply once has an idempotency ID.
- Every client-authoritative lease has an epoch and expiry.
- Snapshots and transactions are separate message classes with appropriate delivery semantics.
- Unknown optional fields/capabilities are safely ignored; unknown required capabilities fail the handshake clearly.
- Logs and traces never contain supplied passwords or reusable authentication secrets.

## Testing strategy

Testing is part of the migration mechanism, not a final phase.

- Unit tests: codecs, revisions, interpolation, cell IDs, interest calculations, authority transitions, reducers.
- Property tests: round-trip encoding, bounded decoding, idempotency, revision monotonicity.
- Fuzz tests: every network decoder and script boundary.
- Golden tests: legacy packet corpus and vNext schema compatibility.
- Simulation tests: deterministic server plus fake clients without rendering.
- Integration tests: real server plus two headless/minimal OpenMW clients.
- Fault tests: loss, latency, jitter, duplication, reordering, disconnects, stalled clients, malformed input.
- Soak tests: cell changes, actor authority churn, reconnect loops, inventory/container contention.
- Sanitizers: ASan, UBSan, and a race detector on supported CI runners.

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

Performance and smoothing constants should be tuned from these measurements rather than added as unexplained magic values.

## Early architecture decisions to record

Before their implementation, create short architecture decision records for:

1. Repository/branch cutover mechanics.
2. vNext transport selection and encryption/authentication requirements.
3. Protocol schema/codec format.
4. Server authority policy for movement, actors, combat, and inventory.
5. OpenMW hook policy and upstream-contribution strategy.
6. CoreScripts compatibility versus replacement.
7. Persistence model and migration policy.
8. Supported platforms and toolchain baseline.

## First implementation backlog

1. Tag and document both baselines.
2. Pin CrabNet and all build dependencies.
3. Restore a clean CI build and server smoke test.
4. Add the packet catalogue and golden packet corpus.
5. Implement the bounded packet reader and fuzz harness.
6. Fix master-client lifetime and concurrency defects.
7. Introduce protocol-owned types and a transport interface.
8. Build the two-client simulation harness.
9. Inventory and classify all OpenMW engine modifications.
10. Start the OpenMW 0.51 vertical-slice port in a separate worktree.

Do not begin broad gameplay feature work until items 1–8 are complete. Do not begin a full feature port until the vertical slice passes its exit gate.

## Definition of done for vNext

- Built on a supported OpenMW stable release with an explicit update policy.
- Reproducible, sanitizer-clean CI on supported platforms.
- Standalone bounded protocol and deterministic server core.
- Thin, documented OpenMW adapter/patch surface.
- Versioned scripting API and bundled or pinned server scripts.
- Measurable, tested behavior under adverse networks.
- Recoverable reconnect, resync, and authority transfer.
- No reliance on unbounded input, implicit global packet buffers, or exact-build protocol passwords.
- Documented threat model for both trusted cooperative and public-server deployments.
