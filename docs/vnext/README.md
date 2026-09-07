# TES3MP vNext

TES3MP vNext is a clean-break multiplayer architecture for Morrowind built on
OpenMW 0.51. It replaces the TES3MP 0.8.x protocol, transport, server, and
scripting architecture instead of porting them forward.

The first release target is authoritative desktop and PC VR multiplayer with a
bounded protocol, a maintained transport boundary, an engine-independent server
core, and a thin OpenMW adapter. Standalone Meta Quest 3 support is a later,
conditional stretch target.

## Current status

Phases 0–12 are **Complete** except for deferred PC-VR hardware budget capture.
The headless, OpenMW desktop, and PC VR vertical slices share one bounded
protocol, reusable client session, and authoritative server path. Optional
OpenXR head/hand samples are isolated presentation data; they cannot author
canonical movement or gameplay.

Phase 13 actor lifecycle and server-owned AI discovery is complete and awaiting
the owner package decision. The recommended package keeps actors server-owned,
manifest-bound, additive to the player protocol, and frozen in inactive exact
cells without introducing client simulation leases. The [rolling
implementation plan](IMPLEMENTATION_PLAN.md) is the authoritative Now / Next /
Later tracker. The [implementation notes](IMPLEMENTATION_NOTES.md) retain
detailed historical evidence and owner-review history.

## Product scope

The first end-to-end milestone is deliberately small:

1. A dedicated server and two clients negotiate capabilities and authenticate.
2. Both players join, enter fixed interior and exterior fixtures, and observe
   one another only while in the same fixture cell.
3. Clients send semantic movement commands and receive sequenced authoritative
   snapshots.
4. Headless clients replace confirmed state with newer snapshots and reject
   stale sequences; prediction and jitter-buffer presentation belong to later
   client-facing phases.
5. Disconnect, bounded session resume, and expiration complete cleanly under
   adverse network profiles.

This flow passes first with deterministic headless clients, then with OpenMW
desktop clients, and finally with one desktop and one PC VR client. Initial
canonical state is limited to player/session identity, cell, root transform,
velocity, revisions, and acknowledgements. Inventory, actors, combat, quests,
scripting, and persistence arrive in later gated phases.

## Architecture

```text
OpenMW / OpenMW-VR
        |
thin OpenMW adapter
        |
reusable client session
        |
bounded versioned protocol
        |
maintained transport adapter
        |
authoritative server core
        |
scripts | persistence/replay | metrics/admin
```

- **Protocol:** bounded schemas and codecs, semantic identities, negotiation,
  and compatibility rules.
- **Transport:** owned connection and channel semantics, encryption,
  backpressure, disconnect reasons, and telemetry. Library types remain inside
  the adapter.
- **Client session:** reusable headless connection and replication state shared
  by desktop, PC VR, and future clients.
- **Server core:** deterministic validation and canonical reducers without an
  OpenMW, renderer, socket-library, or platform dependency.
- **OpenMW adapter:** the only layer coupled to OpenMW internals; it translates
  engine events into semantic commands and canonical state into presentation.
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
- Durable entities have stable identities and revisions. Explicitly approved
  delegated authority uses epochs and atomic handoff snapshots.
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
from its permanent archive tag. Every active-tree difference is recorded in
[`BASELINE_PROVENANCE.json`](BASELINE_PROVENANCE.json).

Verify the current tree with:

```sh
python scripts/verify_vnext_baseline.py
```

For local configure, build, and test commands, use
[`LOCAL_BASELINE_BUILD.md`](LOCAL_BASELINE_BUILD.md). New work follows one active
rolling pass, preserves dependency boundaries, adds proportionate tests,
refreshes the concise [`STATE.md`](STATE.md) handoff, and leaves the branch
buildable. Full hosted/platform matrices are milestone gates rather than a
requirement for every small pass.

## Documentation map

- [`IMPLEMENTATION_PLAN.md`](IMPLEMENTATION_PLAN.md): rolling Now / Next / Later status and outcome tracker
- [`STATE.md`](STATE.md): concise current-pass handoff; read first in a new session
- [`IMPLEMENTATION_NOTES.md`](IMPLEMENTATION_NOTES.md): historical chronological evidence, commands, approvals, and follow-ups
- [`COLLISION_CONTENT_V1.md`](COLLISION_CONTENT_V1.md): bounded manifest-scoped server collision artifact contract
- [`LOCAL_BASELINE_BUILD.md`](LOCAL_BASELINE_BUILD.md): local build and test workflow
- [`LEGACY_GAMEPLAY_FEATURE_INVENTORY.md`](LEGACY_GAMEPLAY_FEATURE_INVENTORY.md): historical gameplay reference only
- [`PRE_CUTOVER_PROVENANCE.md`](PRE_CUTOVER_PROVENANCE.md): historical cutover provenance
- [`adr/`](adr/): architecture decision records
- [`gdr/`](gdr/): gameplay decision records

## References and license

- [OpenMW 0.51 source](https://gitlab.com/OpenMW/openmw/-/tree/openmw-0.51.0)
- [OpenMW-VR source](https://gitlab.com/madsbuvi/openmw/-/tree/openmw-vr)
- [License](../../LICENSE)
