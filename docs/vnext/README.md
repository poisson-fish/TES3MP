# TES3MP vNext

TES3MP vNext is a clean-break multiplayer architecture for Morrowind built on
OpenMW 0.51. It replaces the TES3MP 0.8.x protocol, transport, server, and
scripting architecture instead of porting them forward.

The first release target is authoritative desktop and PC VR multiplayer with a
bounded protocol, maintained transport boundary, engine-independent server
core, and thin OpenMW adapter. Standalone Meta Quest 3 support is a later,
conditional stretch target.

## Status

Phases 0–5 are implemented. Phase 6 is in progress, with Slices 6.1 through 6.5
implemented and Slice 6.6 in progress under owner-approved ADR-0039.
The active tree is a provenance-verified OpenMW 0.51 baseline with:

- engine-independent protocol, transport, client-session, server-core, and test
  support targets;
- fail-closed dependency and legacy-exclusion checks;
- strong IDs and deterministic spatial, scheduling, random, link, and network
  fault-test primitives;
- sanitizer, race, fuzz, and supported desktop build evidence; and
- owned typed observability interfaces with bounded deterministic test sinks.

Phase 4—bounded protocol and in-memory session—is implemented, including its
complete manually dispatched hosted phase-exit matrix and owner exit approval.
Phase 5—deterministic authoritative server core—is implemented. Its first
production slice implements the approved writer intake, ordering, limits, and
overload policy with accepted owner demo evidence. Slice 5.2 implements the
approved immutable, bounded canonical player/session state value and invariant
checks with accepted owner demo evidence. Slice 5.3 implements the approved
writer-confined command validation and atomic reducer with accepted owner demo
evidence. Slice 5.4 implements the approved immutable
latest-publication, checked version, typed domain-change, and reader-gap
contracts with accepted owner demo evidence. See the [implementation
plan](IMPLEMENTATION_PLAN.md) for the live tracker, phase gates, and decision
records, and the [implementation notes](IMPLEMENTATION_NOTES.md) for detailed
verification and owner-review history. Slice 5.5 has accepted
[`ADR-0030`](adr/ADR-0030-phase5-idempotency-checksum-and-resync-boundary.md);
its production implementation and focused tests pass applicable local
verification and owner demo acceptance. The reducer core is eligible for later
reviewed composition, but no composed production multiplayer runtime exists
yet. Slice 5.6 has accepted
[`ADR-0031`](adr/ADR-0031-phase5-committed-domain-sink-boundary.md); production
sink-interface implementation and focused tests pass applicable local
verification and owner demo acceptance. Slice 5.7's test-only seeded reducer
property and eight-client deterministic simulation implementation passes
applicable local verification and owner demo acceptance. The complete Phase 5
hosted manual matrix is green on the exact portability-repair candidate, and
the project owner approved the Phase 5 exit gate. Phase 6 is in progress;
Slice 6.1's owned real-transport lifecycle boundary is implemented.

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
4.6 is also **Implemented** with exhaustive property, mutation, golden-corpus,
and fuzz-registration coverage and introduces no new production protocol,
architecture, authority, state-scope, or gameplay behavior.

Phase 5 Slice 5.1 implements accepted
[`ADR-0026`](adr/ADR-0026-phase5-writer-command-intake-and-ordering.md), which
defines the server-core command boundary, writer cutoff/order, hard safety
ceilings, overload behavior, component boundary, and acceptance tests. Local
verification and owner implementation-demo acceptance pass; canonical state,
validation/reduction, acknowledgements, and gameplay behavior remain later
gated work.

Phase 5 Slice 5.2 implements accepted
[`ADR-0027`](adr/ADR-0027-phase5-canonical-player-and-session-state.md): separate
immutable player-entity and active-session-progress partitions, deterministic
identity ordering, 256/256 hard bounds, explicit unique bindings, one checked
atomic spatial revision, and session-generation-scoped optional
contiguous-finalized acknowledgement progress. It adds no online state install,
reducer, lifecycle, persistence, publication, movement, or gameplay behavior;
local verification and owner implementation-demo acceptance pass.

Phase 5 Slice 5.3 now has accepted
[`ADR-0028`](adr/ADR-0028-phase5-command-validation-and-atomic-reducer.md) and
[`GDR-0012`](gdr/GDR-0012-phase5-minimal-motion-reducer-semantics.md), covering
the atomic reducer, validation/disposition, acknowledgement, incomplete
cross-batch idempotency gate, and narrow provisional velocity-replacement
decisions. The writer-confined implementation, closed observability, and focused
contract tests pass applicable local verification and owner implementation-demo
acceptance. Online composition stays gated on Slice 5.5.

Phase 5 Slice 5.4 has accepted
[`ADR-0029`](adr/ADR-0029-phase5-immutable-canonical-publication-and-versioned-change-feed.md)
for immutable publication ownership, state versioning, complete domain record
scope, latest-only retention, gap recovery, and slow-reader isolation. Owner
architecture approval is complete. The implementation and focused contract
tests pass applicable local verification and owner demo acceptance.

Phase 5 Slice 5.5 has accepted
[`ADR-0030`](adr/ADR-0030-phase5-idempotency-checksum-and-resync-boundary.md)
for the bounded per-generation command-ID retry horizon, duplicate finalization,
canonical byte/checksum contract, explicit server-snapshot resync boundary, and
remaining online-composition gate. Owner architecture approval is complete;
the bounded server-core implementation and focused tests pass applicable local
verification and owner implementation-demo acceptance.

Phase 5 Slice 5.6 is **Implemented** under accepted
[`ADR-0031`](adr/ADR-0031-phase5-committed-domain-sink-boundary.md), which defines the
committed publication payload, four role-specific sink ports, post-commit
fan-out, bounded failure reporting, and retention decisions. Owner architecture
approval is complete. The bounded server-core implementation and focused tests
pass applicable local verification and owner implementation-demo acceptance.

Phase 5 Slice 5.7 is **Implemented** and introduces no new production behavior.
Its test-only seeded eight-client simulation exercises the accepted intake,
reducer, publication, checksum, and rejection contracts while checking
canonical invariants after every batch and exact same-seed replay. Implementation
`917ecc3278`, applicable local verification, and owner demo acceptance pass.
The complete Phase 5 hosted manual matrix passes on exact commit
`8e7ca8ff607f3b95ec8d348919f6c1233a11eb23`, and the project owner approved the
exit gate. Phase 5 is **Implemented**. Phase 6 is **In Progress** and Slice 6.1
is **Implemented** under accepted
[`ADR-0032`](adr/ADR-0032-phase6-transport-adapter-and-lifecycle-boundary.md)
after owner approval of its target, provisioning, lifecycle, pumping,
initial-bound, and hostname/DNS decisions. The authorized exact-pin c-ares proof
passes locally and across the complete five-job supported desktop matrix on
exact candidate `6dcea1b4af`; retained artifacts are consistent, and the owner
accepted the dependency profile. The production lifecycle adapter and focused
deterministic/loopback checks pass locally. Exact implementation candidate
`36c1ac6617d75c8d8ef88d687901c9d73b25d0a0` passes the complete six-job
supported-platform and sanitizer lifecycle matrix in
[`33215346506`](https://github.com/poisson-fish/TES3MP/actions/runs/33215346506),
and all six retained artifact sets are internally verified and consistent.
Slice 6.2 is **Implemented** under accepted
[`ADR-0033`](adr/ADR-0033-phase6-transport-channel-and-delivery-semantics.md):
two fixed equal-scheduled channels, an owned bounded byte-delivery API, and
minimal fail-closed semantics. Implementation `2f022fe4ad` and applicable local
verification pass, and the owner accepted the implementation demo. Application
queue/coalescing policy, product capacity, detailed telemetry, and gameplay
behavior remain separately gated. Slice 6.3 is **Implemented** under accepted
[`ADR-0034`](adr/ADR-0034-phase6-credential-and-resumption-boundary.md); its
production artifacts add the approved bounded credential messages, move-only
secret and admission values, exact generated schemas, corpus/fuzz registration,
bounded digest-only transactional resume store, and fixed-work optional
join-password provider with process-local non-reused routing principals. The
approved opaque admission-scope value, bounded global/source token-bucket
limiter, and private exact-pinned OpenSSL 3.5.8 credential crypto provider also
pass focused contracts. Accepted
[`ADR-0035`](adr/ADR-0035-phase6-transport-admission-scope-handoff-and-derivation.md)
adds atomic accepted-event scope handoff and runtime-keyed HMAC-SHA-256 IPv4/
IPv6-/64 derivation with no exposed address. Accepted
[`ADR-0036`](adr/ADR-0036-phase6-authentication-composition-and-session-finalization.md)
authorizes the typed shared authentication composition and fail-closed session
finalization boundary. Its production implementation, redaction contracts, and
complete authentication loopback closure pass applicable local checks, and the
owner accepted the implementation demo on 2026-08-31.
Slice 6.5 is **Implemented** under accepted
[`ADR-0038`](adr/ADR-0038-phase6-network-telemetry-and-stable-reasons.md);
closed stable reasons, saturated application counters, bounded queue gauges,
sink-failure isolation, and honest public GNS per-lane pressure sampling pass
focused local checks, and the owner accepted the implementation demo on
2026-09-01.
Slice 6.6 is **In Progress** under accepted
[`ADR-0039`](adr/ADR-0039-phase6-deterministic-real-transport-fault-boundary.md);
the approved boundary composes the repository-owned deterministic fault
scheduler above real encrypted localhost sockets while retaining native GNS
fault controls only as supplemental below-adapter coverage.

## References and license

- [OpenMW 0.51 source](https://gitlab.com/OpenMW/openmw/-/tree/openmw-0.51.0)
- [OpenMW multiplayer design note](https://gitlab.com/OpenMW/openmw/-/blob/openmw-0.51.0/docs/source/reference/lua-scripting/overview.rst#L85-90)
- [OpenMW-VR source](https://gitlab.com/madsbuvi/openmw/-/tree/openmw-vr)
- [Implementation plan](IMPLEMENTATION_PLAN.md)
- [Implementation notes](IMPLEMENTATION_NOTES.md)
- [License](../../LICENSE)
