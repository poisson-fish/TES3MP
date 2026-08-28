# TES3MP vNext

TES3MP vNext is a clean-break multiplayer architecture for Morrowind built on
OpenMW 0.51. It replaces the TES3MP 0.8.x protocol, transport, server, and
scripting architecture instead of porting them forward.

The first release target is authoritative desktop and PC VR multiplayer with a
bounded protocol, maintained transport boundary, engine-independent server
core, and thin OpenMW adapter. Standalone Meta Quest 3 support is a later,
conditional stretch target.

## Status

Phases 0–3 are implemented. The active tree is a provenance-verified OpenMW
0.51 baseline with:

- engine-independent protocol, transport, client-session, server-core, and test
  support targets;
- fail-closed dependency and legacy-exclusion checks;
- strong IDs and deterministic spatial, scheduling, random, link, and network
  fault-test primitives;
- sanitizer, race, fuzz, and supported desktop build evidence; and
- owned typed observability interfaces with bounded deterministic test sinks.

Phase 4—bounded protocol and in-memory session—is in progress. Slices 4.1–4.5
are implemented; Slice 4.6 is adding the phase's exhaustive property, mutation,
golden-corpus, and fuzz-registration coverage. No production multiplayer runtime
or real transport integration exists yet. See the [implementation
plan](IMPLEMENTATION_PLAN.md) for the live tracker, phase gates, decision
records, and verification evidence.

Beginning with Phase 4, each slice runs applicable local verification. The full
vNext hosted matrix runs once by manual dispatch against the phase-completion
candidate, including macOS x86-64. A phase cannot close while that gate is red.

## Product scope

The first end-to-end milestone is deliberately small:

1. A dedicated server and two clients negotiate capabilities and authenticate.
2. Both players join, enter interior and exterior cells, and observe each other.
3. Clients send semantic movement commands and receive timestamped snapshots.
4. Remote movement uses a bounded jitter buffer.
5. Disconnect and session resume complete cleanly under adverse network
   profiles.

The flow must pass first with deterministic headless clients, then with OpenMW
desktop clients, and finally with one desktop plus one PC VR client. Initial
canonical state is limited to player/session identity, cell, root transform,
velocity, revisions, and acknowledgements. Inventory, actors, combat, quests,
scripting, and persistence arrive in later gated phases.

## Architecture

```text
OpenMW / OpenMW-VR
        |
thin client adapter
        |
headless client session
        |
bounded versioned protocol
        |
maintained transport adapter
        |
authoritative server core
        |
scripts | persistence/replay | metrics/admin
```

- **Protocol:** schemas, bounded codecs, semantic identities, negotiation, and
  compatibility rules.
- **Transport:** owned connection/channel semantics, encryption, backpressure,
  disconnect reasons, and telemetry. Library types stay inside the adapter.
- **Client session:** reusable headless connection and replication state shared
  by desktop, PC VR, and any future client.
- **Server core:** deterministic validation and canonical reducers with no
  OpenMW, renderer, socket-library, or platform dependency.
- **OpenMW adapter:** the only layer coupled to OpenMW internals; it translates
  engine events to semantic commands and canonical state to presentation.
- **Scripting and persistence:** typed canonical-state boundaries, never packet
  buffers or direct mutable-state access.

## Supported platforms

| Target | Scope |
|---|---|
| Windows, Linux, and macOS desktop | Primary client and server targets |
| PC VR through OpenMW-VR | Required secondary client using the shared core |
| Meta Quest 3 standalone | Conditional post-release feasibility target |

OpenMW-VR is maintained separately from official OpenMW. Fork- and OpenXR-
specific types must not leak into the multiplayer core.

## Engineering invariants

- The server is the single writer of durable canonical state.
- Protocol, client-session, and server-core targets remain independent of
  OpenMW, rendering, VR, operating-system, and transport-library types.
- Network and script input is bounded before allocation or mutation; failed
  decoding or validation cannot partially commit state.
- Latest-wins sampled state remains distinct from reliable apply-once
  operations, which carry command IDs and expected revisions.
- Durable entities have stable identities and revisions; delegated authority,
  when explicitly approved, uses epochs and atomic handoff snapshots.
- Deterministic tests inject clocks, random sources, malformed input, network
  faults, disconnects, and contention.
- Metrics and structured events land with the behavior they describe and never
  contain reusable credentials or unfiltered user data.
- Desktop and VR controls produce the same semantic commands. Head and hand
  poses are presentation data and cannot move the authoritative player root.
- Architecture, authority, state-scope, compatibility, and gameplay decisions
  require owner-approved ADRs or GDRs before dependent production code lands.

## Compatibility and non-goals

vNext does not preserve TES3MP 0.8.x wire compatibility, mixed old/new peers,
RakNet or CrabNet integration, the old server/CoreScripts API, legacy saves, or
the old engine patch set. Archived code may inform gameplay requirements but is
not an implementation template.

## Repository workflow

The active baseline is OpenMW `openmw-0.51.0` at
`f4bec41444214a7903bebd178389ca22ca13f646`. Archived TES3MP remains available
from its permanent branch and tag. Every active-tree difference is recorded in
[`BASELINE_PROVENANCE.json`](BASELINE_PROVENANCE.json).

Verify the current tree with:

```sh
python scripts/verify_vnext_baseline.py
```

New work follows the current implementation-plan slice, preserves dependency
boundaries, adds proportionate tests, records exact evidence, updates statuses,
and leaves the branch buildable. Hosted vNext workflows are manual phase-exit
gates; ordinary slice commits must not trigger the full matrix.

Slice 4.5 is **Implemented** under accepted
[`ADR-0025`](adr/ADR-0025-minimal-player-command-world-snapshot-exchange.md)
bounded typed-root, role-specific exchange-guard, and deterministic in-memory
fake-peer design and accepted
[`GDR-0011`](gdr/GDR-0011-phase4-minimal-player-exchange-semantics.md) separately
defined its narrow player-intent, authority, and session-targeted snapshot
scope. Local verification and the required owner demo acceptance pass. Slice
4.6 is **In Progress** and introduces no new production protocol, architecture,
authority, state-scope, or gameplay behavior.

## References and license

- [OpenMW 0.51 source](https://gitlab.com/OpenMW/openmw/-/tree/openmw-0.51.0)
- [OpenMW multiplayer design note](https://gitlab.com/OpenMW/openmw/-/blob/openmw-0.51.0/docs/source/reference/lua-scripting/overview.rst#L85-90)
- [OpenMW-VR source](https://gitlab.com/madsbuvi/openmw/-/tree/openmw-vr)
- [Implementation plan](IMPLEMENTATION_PLAN.md)
- [License](../../LICENSE)
