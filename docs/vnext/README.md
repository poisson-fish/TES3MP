# TES3MP vNext

TES3MP vNext is a clean-break multiplayer architecture for Morrowind built on
OpenMW 0.51. It replaces the TES3MP 0.8.x protocol, transport, server, and
scripting architecture instead of porting them forward.

The first release target is authoritative desktop and PC VR multiplayer with a
bounded protocol, a maintained transport boundary, an engine-independent server
core, and a thin OpenMW adapter. Standalone Meta Quest 3 support is a later,
conditional stretch target.

## Current status

Phases 0–6 are **Implemented**. Phase 7, the headless end-to-end multiplayer
slice, is **In Progress**.

| Slice | Outcome | Status |
|---|---|---|
| 7.0 | Approve the vertical-slice behavior | **Implemented** |
| 7.1 | Compose the dedicated server and configuration | **Implemented** |
| 7.2 | Add the reusable headless client and script driver | **Implemented** |
| 7.3 | Authenticate, allocate identities, and join | **Implemented** |
| 7.4 | Transition fixtures and project observations | **Implemented** |
| 7.5 | Integrate movement and deliver snapshots | **Implemented** |
| 7.6 | Disconnect, resume, and expire sessions | **In Progress** |
| 7.7 | Run adverse-network, reconnect, bound, and soak tests | **Not Started** |

Slice 7.6 is governed by accepted
[`ADR-0045`](adr/ADR-0045-phase7-disconnect-resume-and-expiration-composition.md).
Failure-atomic resume-token rotation and transactional canonical hide, resume,
and exact-deadline expiration are implemented. Live join registration and
failure-atomic disconnect hide/output composition are also wired. Resume
authentication/output, expiration pumping, and the complete process proof remain.

The [implementation plan](IMPLEMENTATION_PLAN.md) is the authoritative status
tracker. The [implementation notes](IMPLEMENTATION_NOTES.md) retain detailed
verification and owner-review history.

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
[`LOCAL_BASELINE_BUILD.md`](LOCAL_BASELINE_BUILD.md). New work follows the active
implementation-plan slice, preserves dependency boundaries, adds proportionate
tests, records exact evidence in the implementation notes, and leaves the branch
buildable. Beginning with Phase 4, the complete hosted vNext matrix is a manual
phase-exit gate and includes macOS x86-64.

## Documentation map

- [`IMPLEMENTATION_PLAN.md`](IMPLEMENTATION_PLAN.md): current phase/slice status, ordered work, decision registers, and exit gates
- [`IMPLEMENTATION_NOTES.md`](IMPLEMENTATION_NOTES.md): chronological evidence, verification commands, approvals, and follow-ups
- [`LOCAL_BASELINE_BUILD.md`](LOCAL_BASELINE_BUILD.md): local build and test workflow
- [`LEGACY_GAMEPLAY_FEATURE_INVENTORY.md`](LEGACY_GAMEPLAY_FEATURE_INVENTORY.md): historical gameplay reference only
- [`PRE_CUTOVER_PROVENANCE.md`](PRE_CUTOVER_PROVENANCE.md): historical cutover provenance
- [`adr/`](adr/): architecture decision records
- [`gdr/`](gdr/): gameplay decision records

## References and license

- [OpenMW 0.51 source](https://gitlab.com/OpenMW/openmw/-/tree/openmw-0.51.0)
- [OpenMW-VR source](https://gitlab.com/madsbuvi/openmw/-/tree/openmw-vr)
- [License](../../LICENSE)
