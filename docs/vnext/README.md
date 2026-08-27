# TES3MP vNext

TES3MP vNext is a clean-break multiplayer architecture for Morrowind built on a
clean OpenMW 0.51 baseline. It replaces the TES3MP 0.8.x networking, server, and
scripting architecture instead of porting it forward.

The first release target is an authoritative desktop and PC VR multiplayer
experience with a new bounded protocol, maintained transport boundary,
engine-independent server core, and thin OpenMW client adapter. Standalone Meta
Quest 3 support is a later, conditional stretch target.

## Project status

Phases 0 through 2 are implemented. The active
source tree has been replaced with the provenance-verified OpenMW 0.51 baseline,
and the machine-checkable baseline manifest now enumerates every intentional
difference. The documented local Windows baseline path and the supported
desktop CI matrix pass. Fail-closed legacy-exclusion proof passes locally and
on the accepted Linux, Windows, and macOS CI matrix. Phase 2 security and
architecture decisions are in progress; the hostile-Internet threat model,
restricted FlatBuffers schema/codec policy, and standalone
GameNetworkingSockets transport/security selection are implemented with their
cross-platform proofs. ADR-0005 accepts automatic basic encryption without
operator-managed certificates or dependency patches, an optional shared join
password, and automatic single-use resume tokens. The exact five-platform
transport proof, Clang ASan/UBSan gate, retained-artifact consistency review,
and owner completion acceptance pass at `d5d7a1d1f4`. Slice 2.4 authority and
state-scope review is implemented: ADR-0006 accepts the server command/reducer,
explicit scope/lifetime, reconciled prediction, non-durable presentation
sample, and deferred epoch-lease recommendations, with intentionally basic
friends-server gameplay validation. Slice 2.5 OpenMW hook/patch review is
implemented: ADR-0007 accepts a native app-local adapter, bounded semantic hook
surface, and machine-checked patch registry. General OpenMW candidates stay in
a separate developer-local preparation repository unless later upstream work is
explicitly approved. Slice 2.6 deterministic simulation and protocol
compatibility review is implemented: ADR-0013 accepts a 30 Hz bounded-catch-up
tick, checked fixed-point canonical numerics, writer-assigned command order,
versioned deterministic inputs/canonical bytes, and current-plus-previous-minor
protocol compatibility. The Phase 2 exit review is approved. Phase 3 is in
progress: accepted ADR-0014 defines five engine-independent TES3MP libraries and
one app-local OpenMW adapter with fail-closed dependency checks. Slice 3.1 is
implemented: its empty C++20 targets build locally and the owner accepted the
implementation demo. Slice 3.2 is implemented: ADR-0015 accepts ten scoped
`uint64_t` semantic types with explicit construction, checked optional-returning
counter advancement, type-qualified formatting, and codec/allocation separation.
The approved API and its independent boundary tests are implemented, and the
owner accepted the implementation demo. Slice 3.3 is implemented: ADR-0016 is
accepted with Option A for all five decisions. Its engine-independent
tagged cell, fixed spatial, command/admission metadata, and per-entity snapshot
values plus test-support-only byte round trip are implemented, and the owner
accepted the Slice 3.3 implementation demo. Slice 3.4 is implemented: accepted
ADR-0017 fixes the project-owned monotonic clock seam, passive bounded 30 Hz
scheduler, versioned numeric-keyed deterministic RNG streams,
test-support-only bounded in-memory link, and exact trace plus non-canonical
diagnostic-digest boundaries. Their independent contracts pass and the owner
accepted the implementation demo. Slice 3.5 is implemented: accepted ADR-0018
fixes a passive test-support wrapper with independently bounded
direction/channel profiles, isolated deterministic fault streams, and explicit
stall/disconnect controls, and the owner accepted the implementation demo.
Slice 3.6 sanitizer, race-checking, and fuzz CI plumbing is the next eligible
work; no multiplayer runtime or production transport behavior exists yet.

See the [implementation plan](IMPLEMENTATION_PLAN.md) for the live phase and
slice tracker, completion gates, architecture decisions, and implementation
notes.

## Goals

- Build the active product directly on a pinned, provenance-verified OpenMW
  stable baseline.
- Keep the protocol, client session, and authoritative server core independent
  of OpenMW, rendering, VR, operating system, and transport-library types.
- Make the server the single writer of durable canonical state.
- Support desktop and PC VR clients through the same protocol and gameplay
  rules.
- Treat VR head and hand poses as optional presentation data around a
  platform-neutral authoritative player root.
- Make malformed input, adverse networks, reconnect, resynchronization, and
  authority transfer testable from the beginning.
- Provide new versioned scripting and persistence models designed around typed
  server events and validated commands.

## Non-goals

vNext has no compatibility requirement with TES3MP 0.8.x. It will not preserve:

- The legacy wire protocol, packet IDs, or message layouts.
- Old-client/new-server or new-client/old-server interoperability.
- RakNet or CrabNet integration.
- The old server process, configuration, or CoreScripts API.
- Legacy persistence/save data or undocumented behavior.
- The old engine patch set as an implementation base.

The archived code remains available as a gameplay-feature reference. It is not a
template for the new protocol or server.

## Architecture

```text
                         desktop input/presentation
                                      |
OpenMW / OpenMW-VR <-> client adapter + client session
                                      |
                         versioned bounded protocol
                                      |
                         maintained transport adapter
                                      |
                         authoritative server core
                                      |
           +--------------------------+--------------------------+
           |                          |                          |
    scripting boundary       persistence/replay          metrics/admin
```

The durable component boundaries are:

- **Protocol** — schemas, bounded codecs, stable IDs, ticks, revisions,
  idempotency IDs, authority epochs, version negotiation, and capabilities.
- **Transport** — owned connection/channel semantics, encryption,
  authentication plumbing, backpressure, disconnect reasons, and telemetry.
  Transport-library types do not escape this adapter.
- **Client session** — a reusable headless connection and replication state
  machine shared by desktop, PC VR, and any future platform client.
- **Server core** — deterministic command validation, canonical state reducers,
  interest management, snapshots, revisions, resynchronization, and authority.
  It has no OpenMW or renderer dependency.
- **OpenMW adapter** — the only layer coupled to OpenMW internals. It converts
  engine events into semantic commands and authoritative state into local
  presentation.
- **Scripting and persistence** — typed consumers/producers at the canonical
  state boundary, never direct access to packet buffers or mutable server state.

## First product milestone

The first end-to-end slice is intentionally small:

1. Start a dedicated server and two OpenMW 0.51 desktop clients.
2. Negotiate the vNext protocol and capabilities.
3. Authenticate, create a session, and join the world.
4. Enter one interior and one exterior cell.
5. Spawn and observe the other player.
6. Send semantic movement commands and receive timestamped snapshots.
7. Interpolate remote movement through a bounded jitter buffer.
8. Disconnect and resume the session cleanly.

Before OpenMW integration, the same flow must pass headlessly with deterministic
fake clients. Both versions must tolerate configured latency, loss, jitter,
duplication, reordering, stalls, and disconnects without unbounded queues or
durable divergence.

Initial canonical state is limited to player identity, connection/session,
canonical cell, root transform, velocity, revision, and acknowledgement state.
Inventory, actors, combat, quests, scripting, and persistence wait until this
slice passes its gate.

## Platform scope

| Target | Scope |
|---|---|
| Windows, Linux, and macOS desktop | Primary supported client/server targets |
| PC VR through OpenMW-VR | Required secondary client target using the shared core |
| Meta Quest 3 standalone | Conditional stretch target after desktop/PC VR stabilization |

OpenMW-VR is currently maintained separately from official OpenMW. VR integration
therefore stays in a separate worktree/patch target and must not leak fork- or
OpenXR-specific types into the multiplayer core.

## Core engineering rules

- Sampled movement and pose snapshots are latest-wins, never reliable ordered
  operations.
- Reliable apply-once operations carry command IDs and expected entity
  revisions.
- Durable entities have stable identities and monotonically increasing
  revisions.
- Delegated authority, where explicitly permitted, uses leases with epochs and
  complete atomic handoff snapshots.
- Every decoder has byte, collection, string, allocation, and work limits.
- State is decoded and validated completely before canonical mutation.
- Unknown optional capabilities are ignored safely; unknown required
  capabilities fail clearly.
- Tests inject clocks, deterministic randomness, network faults, malformed
  inputs, disconnects, and contention.
- Metrics and structured logs ship with each feature and never contain reusable
  credentials.
- Desktop and VR controls produce the same semantic command types. Headset pose
  does not silently move the authoritative root.

## Source provenance and cutover

- Archived TES3MP source branch point:
  `49be5b6405d6ab427e06ed350cf76c715a1f3bdd`
  (`tes3mp-0.8.1-20-g49be5b640`).
- Target engine baseline: OpenMW `openmw-0.51.0` at
  `f4bec41444214a7903bebd178389ca22ca13f646`.
- The `0.8.1` branch and a permanent annotated archive tag retain the old source.
- The vNext line will receive a clean baseline-cutover commit, not a textual
  merge of legacy engine modifications into OpenMW 0.51.
- Every active-tree difference from the pinned OpenMW baseline must be intentional
  and reviewable.

[`BASELINE_PROVENANCE.json`](BASELINE_PROVENANCE.json) records the pinned
baseline and cutover objects, every intentional path difference, and the hashed
OpenMW dependency-declaration inputs. Verify the committed tree with:

```sh
python scripts/verify_vnext_baseline.py
```

The verifier fails closed on unrecorded or missing path differences, baseline or
cutover identity drift, and dependency-declaration hash changes. The manifest
also records inherited floating dependency inputs that must be resolved by the
remaining Phase 1 build and CI slices.

The exact Git mechanics must be accepted in ADR-0001 and rehearsed before the
cutover. Shared vNext history must not be force-pushed after publication.

## Working on vNext

Until the baseline cutover is complete, the repository still builds the archived
TES3MP-era source. Do not add new multiplayer work to its packet/processors,
RakNet/CrabNet integration, server, or CoreScripts directories.

New vNext work should follow the current slice in the
[implementation plan](IMPLEMENTATION_PLAN.md). Every implementation change must:

- Preserve the documented dependency boundaries.
- Include proportionate unit, property, fuzz, simulation, integration, or fault
  tests in the same slice.
- Update the phase/slice status and implementation notes with exact verification
  evidence.
- Record architecture or technology choices in the corresponding ADR instead of
  burying them in code or pull-request discussion.
- Leave the branch buildable and avoid unfinished cross-slice coupling.

Planned ADRs will live under `docs/vnext/adr/` as they are created.

## Upstream references

- [OpenMW 0.51 source](https://gitlab.com/OpenMW/openmw/-/tree/openmw-0.51.0)
- [OpenMW Lua and multiplayer design note](https://gitlab.com/OpenMW/openmw/-/blob/openmw-0.51.0/docs/source/reference/lua-scripting/overview.rst#L85-90)
- [OpenMW-VR source](https://gitlab.com/madsbuvi/openmw/-/tree/openmw-vr)
- [OpenMW-VR versioning policy](https://gitlab.com/madsbuvi/openmw/-/blob/openmw-vr/docs/source/manuals/openmw-vr/versioning.rst)

## License

vNext remains governed by the repository's [license](../../LICENSE).
