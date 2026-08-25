# TES3MP vNext implementation plan

Document type: living implementation plan

Updated: 2026-08-25

Direction source: [`README.md`](README.md)

## Purpose

This document turns the accepted vNext architecture direction into an ordered,
trackable implementation program. The architecture README remains the source of
truth for product direction, compatibility policy, and non-goals. This plan owns
implementation order, slice boundaries, phase gates, and status.

The plan deliberately uses small, reviewable slices. A slice should leave the
branch buildable, add its own verification, and avoid depending on unfinished
work hidden in a long-lived branch.

## Status model

Every phase and slice uses exactly one of these statuses:

- **Not Started**: no implementation artifact for the phase or slice has landed.
- **In Progress**: at least one implementation artifact has landed or is actively
  being integrated, but the exit gate has not been met.
- **Implemented**: every required slice is implemented and the phase exit gate is
  satisfied with recorded evidence.

Phase status is maintained explicitly. It is not inferred from code existing in
a branch. If a completed phase needs material rework, move it back to **In
Progress** and record why in its implementation notes.

Documentation and exploratory spikes do not advance an implementation slice
unless they are named deliverables of that slice. A spike must either be removed
or converted into tested production code before a phase can be **Implemented**.

## Current repository baseline

Observed on 2026-08-25 before this plan was added:

- The active branch is `vnext` at `86cfa5ab3`.
- The accepted architecture direction is committed in `docs/vnext/README.md`.
- The archived source branch point is available as branch `0.8.1` at
  `49be5b640`, but the required permanent annotated archive tag at that commit
  has not been created.
- The existing lightweight tag `tes3mp-0.8.1` points to `68954091c`, not to the
  documented archive branch point. It does not satisfy the archive-tag slice.
- Only the `origin` remote is configured; `openmw-upstream` is not configured.
- The active source is still the TES3MP 0.8.1-era tree plus vNext documentation.
  The OpenMW 0.51 clean baseline cutover has not occurred.
- No vNext protocol, transport, server-core, client-session, OpenMW adapter, or
  test-support target exists yet.

## Program tracker

| Phase | Outcome | Status | Depends on |
|---|---|---|---|
| 0 | Archive and cutover preparation | **In Progress** | — |
| 1 | Clean OpenMW 0.51 baseline | **Not Started** | Phase 0 |
| 2 | Security and architecture decisions | **Not Started** | Phase 1 |
| 3 | Independent targets and test scaffold | **Not Started** | Phase 2 |
| 4 | Bounded protocol and in-memory session | **Not Started** | Phase 3 |
| 5 | Deterministic authoritative server core | **Not Started** | Phase 4 |
| 6 | Maintained transport and secure network session | **Not Started** | Phase 5 |
| 7 | Headless end-to-end multiplayer slice | **Not Started** | Phase 6 |
| 8 | OpenMW desktop vertical slice | **Not Started** | Phase 7 |
| 9 | PC VR interoperability gate | **Not Started** | Phase 8 |
| 10 | Player lifecycle and content identity | **Not Started** | Phase 9 |
| 11 | Canonical cells, interest, and resynchronization | **Not Started** | Phase 10 |
| 12 | Production movement, animation, and pose replication | **Not Started** | Phase 11 |
| 13 | Actor lifecycle, AI state, and authority handoff | **Not Started** | Phase 12 |
| 14 | Interactive objects, locks, traps, and doors | **Not Started** | Phase 13 |
| 15 | Inventory, equipment, and container transactions | **Not Started** | Phase 14 |
| 16 | Combat, stats, magic, death, and resurrection | **Not Started** | Phase 15 |
| 17 | Dialogue, journals, factions, and quests | **Not Started** | Phase 16 |
| 18 | Time, weather, and durable world state | **Not Started** | Phase 17 |
| 19 | Versioned server scripting | **Not Started** | Phase 18 |
| 20 | Transactional persistence and deterministic replay | **Not Started** | Phase 19 |
| 21 | Administration, moderation, and discovery | **Not Started** | Phase 20 |
| 22 | Desktop and PC VR stabilization and release | **Not Started** | Phase 21 |
| 23 | Meta Quest 3 feasibility decision | **Not Started** | Phase 22 |
| 24 | Meta Quest 3 multiplayer port | **Not Started** | Phase 23 go decision |

Phases are ordered by their production dependency. Research for a later phase may
run earlier, but later production code must not bypass an unmet exit gate. In
particular, Phases 23 and 24 remain stretch work and must not alter the durable
server model or block the desktop/PC VR release.

## Mapping from the architecture plan

No delivery area from the architecture README is dropped. Its broad phases are
expanded here as follows:

| Architecture README phase | Implementation phases | Refinement |
|---|---|---|
| 0 — Archive and baseline cutover | 0–2 | Separates irreversible Git work, clean baseline verification, and decisions/security review |
| 1 — Architecture scaffold | 3–6 | Separates dependency enforcement, bounded in-memory protocol, deterministic server core, and real transport |
| 2 — Desktop vertical slice | 7–8 | Proves the complete headless system before adding the OpenMW adapter |
| 3 — PC VR integration | 9 | Preserves a discrete interoperability gate before broader gameplay |
| 4 — Authoritative gameplay slices | 10–18 and 21 | Gives each gameplay domain its own gate; delays external admin/discovery surfaces until persistence exists |
| 5 — Server scripting and persistence | 19–20 | Splits two independent failure/security domains while defining their sink boundaries in Phase 5 |
| 6 — Desktop and PC VR stabilization | 22 | Keeps release hardening after the operational surface is complete |
| 7 — Quest stretch target | 23–24 | Separates an evidence-backed feasibility decision from a conditional production port |

The most important sequencing refinement is that Phase 5 adds domain-level
script, persistence, replay, and metrics sink interfaces before gameplay state
expands. Their runtime/database implementations still wait until Phases 19 and
20. This prevents late persistence or scripting work from forcing a second
canonical-state architecture.

## Program-wide implementation rules

These rules apply to every phase and are part of every exit gate:

1. Archived TES3MP code may be studied for gameplay behavior, but no legacy
   packet, processor, scripting binding, RakNet/CrabNet integration, or engine
   modification is ported into the active implementation.
2. Protocol, server-core, and deterministic simulation targets do not include or
   link OpenMW headers or libraries.
3. Network and script input is bounded before allocation or state mutation.
   Decode and validation failure cannot partially commit canonical state.
4. Durable state changes pass through one validated server command path. Scripts,
   administrators, clients, and replay do not receive bypass mutation APIs.
5. Latest-wins sampled state and reliable apply-once operations remain different
   message classes with different queue and delivery behavior.
6. Every durable entity has a stable identity and revision. Every apply-once
   command has an idempotency identity. Every delegated authority has an epoch.
7. Tests use injected clocks, deterministic random sources, and explicit network
   fault profiles. Wall-clock time and nondeterministic iteration must not affect
   canonical reducer results.
8. Queues, messages, collections, strings, per-peer work, and retry windows have
   explicit limits and observable rejection behavior.
9. Structured metrics and logs land with the behavior they describe. Secrets,
   reusable credentials, and unfiltered user data are never logged.
10. Desktop, PC VR, and a possible Quest client produce the same semantic player
    commands. VR poses are optional presentation data, not durable world state.
11. Each slice adds or updates unit/integration tests and records the exact test
    commands or CI jobs in its implementation notes.
12. A schema or persistence change includes its compatibility/evolution test in
    the same slice. Compatibility is only required within the declared vNext
    policy, never with TES3MP 0.8.x.

## Planned component boundaries

Exact paths and target names are finalized in Phase 3 after the OpenMW cutover.
The intended dependency direction is:

```text
multiplayer_protocol       no engine, runtime, transport, or platform dependency
          ^
          |
multiplayer_transport      transport implementation hidden behind owned interfaces
          ^
          |
multiplayer_client_session reusable headless client state machine
          ^
          |
openmw_multiplayer_adapter the only component coupled to OpenMW internals

multiplayer_protocol
          ^
          |
multiplayer_server_core    deterministic canonical state and reducers
          ^
          |
openmw_multiplayer_server  process/configuration/transport composition root

multiplayer_test_support   fake clock, fake transport, fault injection, fake clients
```

Likely source areas are `components/multiplayer/...` for engine-independent
libraries, an OpenMW application-local directory for the adapter, a dedicated
server application, and `docs/vnext/adr/` for decisions. Phase 3 may adjust these
names to match the clean OpenMW 0.51 tree, but it must preserve the dependency
boundaries above. The final product target should not use `vnext` as a permanent
runtime or protocol name.

## Architecture decision register

| ADR | Decision | Needed by | Status |
|---|---|---|---|
| ADR-0001 | Baseline-cutover Git mechanics | Phase 1 | **Not Started** |
| ADR-0002 | Supported desktop platforms, compilers, and dependency policy | Phase 1 | **Not Started** |
| ADR-0003 | Threat model and authoritative-state policy | Phase 3 | **Not Started** |
| ADR-0004 | Protocol schema, codec, and evolution policy | Phase 4 | **Not Started** |
| ADR-0005 | Transport, encryption, authentication, and session resumption | Phase 6 | **Not Started** |
| ADR-0006 | Authority rules by gameplay subsystem | Phase 10 | **Not Started** |
| ADR-0007 | OpenMW hook and patch-queue policy | Phase 8 | **Not Started** |
| ADR-0008 | PC VR fork/worktree maintenance policy | Phase 9 | **Not Started** |
| ADR-0009 | Scripting language, runtime, isolation, and API model | Phase 19 | **Not Started** |
| ADR-0010 | Persistence store, schema evolution, backup, and replay model | Phase 20 | **Not Started** |
| ADR-0011 | Quest rendering, packaging, and hardware-support policy | Phase 24 | **Not Started** |

An ADR is complete only when it records considered alternatives, selection
criteria, consequences, failure modes, and a replacement/review trigger. A
library-selection ADR must also record maintenance health, license, supported
platforms, security properties, dependency pinning, and a minimal proof build.

## Feature-slice contract

Phases 10 through 18 use the following checklist. A phase cannot be marked
**Implemented** until its implementation notes link each item to code and tests:

- Semantic commands, authority owner, validation, and abuse limits.
- Bounded schemas and explicit collection/string/rate limits.
- Stable identity, revision, authority epoch, and idempotency rules as relevant.
- Initial snapshot, delta ordering, reconnect, and full-resync behavior.
- Interest rules and behavior when an entity enters or leaves interest.
- Desktop presentation and relevant VR presentation/fallback behavior.
- Loss, latency, duplication, reordering, disconnect, and contention coverage.
- Metrics, structured logs, and actionable rejection/error categories.
- Persistence and script event/command mapping, even before those backends exist.
- A deterministic state checksum addition for newly durable data.

## Detailed phases

### Phase 0 — Archive and cutover preparation

Status: **In Progress**

Outcome: the legacy boundary is permanent, the cutover is rehearsed, and the
team has enough recorded context to replace the active source safely.

| Slice | Deliverable | Status | Completion evidence |
|---|---|---|---|
| 0.1 | Record the accepted clean-break direction, source branch point, and OpenMW baseline | **Implemented** | `docs/vnext/README.md` at `86cfa5ab3` |
| 0.2 | Create a permanent annotated archive tag at `49be5b640` without moving or reusing the existing `tes3mp-0.8.1` tag | **Not Started** | Tag name, peeled commit, annotation, and push verification recorded |
| 0.3 | Write a high-level legacy gameplay feature inventory for reference only | **Not Started** | Inventory groups user-visible behavior without packet/API porting tasks |
| 0.4 | Record desktop, PC VR, and Quest target policy plus initial supported toolchains in ADR-0002 | **Not Started** | ADR accepted; CI runner choices identified |
| 0.5 | Write ADR-0001 and rehearse the cutover in a disposable branch/worktree | **Not Started** | Commands, resulting ancestry/tree, rollback, and verification output recorded |
| 0.6 | Capture pre-cutover repository provenance | **Not Started** | Branches, tags, submodules, remotes, commit IDs, and clean-tree check archived in documentation |

Exit gate:

- The annotated archive tag resolves to exactly `49be5b640` and exists on the
  shared remote.
- ADR-0001 describes the exact cutover mechanics and has been dry-run without
  modifying shared history.
- The reference inventory and platform scope exist.
- The cutover can be performed without relying on uncommitted files or an
  undocumented local Git state.

Implementation notes:

- The direction document is the only completed slice.
- The existing `tes3mp-0.8.1` tag is a lightweight tag at `68954091c` and must
  not be moved. ADR-0001 must choose a new unambiguous archive-tag name.
- Branch `0.8.1` currently provides the intended `49be5b640` source reference,
  but a branch alone is not the permanent archive artifact required by the plan.
- No cutover command should be run until the dry-run result preserves the vNext
  documentation and produces an OpenMW 0.51 tree without a textual merge.

### Phase 1 — Clean OpenMW 0.51 baseline

Status: **Not Started**

Outcome: the active vNext source is a provenance-verified OpenMW 0.51 baseline
with reproducible desktop CI and no compiled legacy multiplayer code.

Depends on: Phase 0.

| Slice | Deliverable | Status | Completion evidence |
|---|---|---|---|
| 1.1 | Add `openmw-upstream`, fetch the official `openmw-0.51.0` tag, and verify `f4bec41444214a7903bebd178389ca22ca13f646` | **Not Started** | Remote URL, tag resolution, and verification output recorded |
| 1.2 | Perform the clean baseline-cutover commit exactly as ADR-0001 specifies | **Not Started** | Active tree is OpenMW 0.51 plus reviewed vNext-owned files |
| 1.3 | Add a machine-checkable baseline provenance manifest/check | **Not Started** | CI can enumerate every intentional difference from the pinned tag |
| 1.4 | Establish a documented local configure/build/test preset | **Not Started** | Clean checkout build and upstream test commands pass |
| 1.5 | Add Linux baseline CI | **Not Started** | Configure, build, and upstream tests pass on the supported Linux toolchain |
| 1.6 | Add Windows baseline CI | **Not Started** | Configure, build, and upstream tests pass on the supported Windows toolchain |
| 1.7 | Add macOS baseline CI | **Not Started** | Configure, build, and upstream tests pass on the supported macOS toolchain |
| 1.8 | Prove legacy multiplayer exclusion | **Not Started** | No legacy server, packet processor, RakNet/CrabNet, or CoreScripts target is present in build metadata |

Exit gate:

- A clean clone builds and runs upstream tests on every supported desktop CI
  platform with pinned or reproducibly resolved dependencies.
- The baseline verification job accounts for every difference from OpenMW
  `f4bec4144`.
- No TES3MP 0.8 multiplayer target or dependency is compiled.

Implementation notes:

- Preserve `docs/vnext/` and the minimum required repository metadata during the
  cutover as explicit vNext-owned differences.
- Baseline fixes required merely to build on supported runners should be isolated
  and documented; they must not become a place to reintroduce legacy multiplayer
  changes.
- Record CI images, compilers, CMake version, dependency source, and cache keys in
  ADR-0002 so a green baseline can be reproduced outside CI.

### Phase 2 — Security and architecture decisions

Status: **Not Started**

Outcome: implementation begins with explicit trust boundaries, technology
choices, ownership rules, and patch policies rather than accidental coupling.

Depends on: Phase 1.

| Slice | Deliverable | Status | Completion evidence |
|---|---|---|---|
| 2.1 | Write ADR-0003 and a data-flow threat model | **Not Started** | Trust boundaries, assets, attacker capabilities, mitigations, and deferred risks are reviewed |
| 2.2 | Evaluate schema/codec candidates and accept ADR-0004 | **Not Started** | Bounded decode, evolution, fuzzability, language/tooling, and license criteria are demonstrated |
| 2.3 | Evaluate transport/security candidates and accept ADR-0005 | **Not Started** | Desktop proof builds and channel/security/backpressure semantics are demonstrated |
| 2.4 | Define subsystem authority in ADR-0006 | **Not Started** | Movement, actors, combat, inventory, scripting, admin, and persistence ownership is explicit |
| 2.5 | Define the OpenMW hook policy in ADR-0007 | **Not Started** | Allowed hook surface, patch organization, and upstreaming criteria are explicit |
| 2.6 | Define deterministic simulation and protocol compatibility policies | **Not Started** | Tick, numeric/ordering rules, supported version window, and capability behavior are documented |

Exit gate:

- ADR-0003 through ADR-0007 are accepted at the level needed by the first
  vertical slice.
- Selected libraries build on Linux, Windows, and macOS in a minimal isolated
  proof without OpenMW coupling.
- The threat model identifies resource exhaustion, malformed input, replay,
  spoofing, stale authority, credential handling, and administrative abuse.

Implementation notes:

- Authentication must be an owned interface; choosing a transport does not imply
  exposing that library's identity or connection types outside the adapter.
- The protocol version is semantic product metadata, not an engine commit hash.
- Deferred gameplay-specific details may be appended to ADR-0006 before their
  phases, but the initial command/state ownership model must be decided now.
- A selection proof is disposable evidence, not the production wrapper created
  in Phase 6.

### Phase 3 — Independent targets and test scaffold

Status: **Not Started**

Outcome: dependency boundaries are enforced by the build, and all later work has
deterministic testing, fuzzing, sanitizers, fault injection, and observability.

Depends on: Phase 2.

| Slice | Deliverable | Status | Completion evidence |
|---|---|---|---|
| 3.1 | Add empty protocol, transport, server-core, client-session, adapter, and test-support targets | **Not Started** | Build graph documents and enforces allowed dependencies |
| 3.2 | Add strong value types for IDs, ticks, sequences, revisions, command IDs, and authority epochs | **Not Started** | Construction, comparison, formatting, overflow, and invalid-value tests pass |
| 3.3 | Add canonical `CellId`, transform, velocity, and platform-neutral command/snapshot primitives | **Not Started** | Types round-trip without OpenMW, renderer, VR, or transport types |
| 3.4 | Add injected clock, deterministic RNG, deterministic scheduler, and in-memory link | **Not Started** | Repeated simulations produce identical event logs and checksums |
| 3.5 | Add latency/loss/jitter/duplication/reordering/stall/disconnect fault controls | **Not Started** | Seeded fault profiles are repeatable and independently configurable per direction/channel |
| 3.6 | Add ASan, UBSan, race-checking where supported, and fuzz-target CI plumbing | **Not Started** | Smoke jobs run even before the full decoder corpus exists |
| 3.7 | Add owned metrics/logging interfaces and test sinks | **Not Started** | Core tests can assert metrics and structured events without a production backend |

Exit gate:

- Protocol and server-core compile and test without OpenMW or the selected
  transport library.
- Forbidden dependency checks fail intentionally when an OpenMW or transport
  header is introduced into an engine-independent target.
- The deterministic harness reproduces the same trace and checksum from the same
  seed, including under injected faults.

Implementation notes:

- Prefer explicit constructors and checked arithmetic for network-visible numeric
  types. Do not use primitive aliases where IDs or epochs could be mixed.
- Canonical transforms need a documented coordinate system, units, numeric
  representation, and normalization rules before they enter a schema.
- Test support should be a first-class reusable library, not private helpers tied
  to one integration test.
- Sanitizer and fuzz options should remain opt-in for local builds and explicit in
  CI presets so normal developer builds stay predictable.

### Phase 4 — Bounded protocol and in-memory session

Status: **Not Started**

Outcome: a simulated peer negotiates vNext, authenticates through an interface,
and exchanges bounded messages entirely in memory.

Depends on: Phase 3.

| Slice | Deliverable | Status | Completion evidence |
|---|---|---|---|
| 4.1 | Implement framing/envelopes, message classification, byte budgets, and structured decode errors | **Not Started** | Truncated, oversized, unknown, and malformed inputs fail without partial objects or unbounded allocation |
| 4.2 | Implement `ClientHello`, `ServerHello`, and clear rejection with version/capability negotiation | **Not Started** | Required/optional/unknown capability and legacy-peer cases have golden tests |
| 4.3 | Implement the client/server session state machines and authentication-provider interface | **Not Started** | Illegal transitions, timeout, cancellation, rejection, and secret-redaction tests pass |
| 4.4 | Define reliable-operation and latest-wins snapshot envelopes | **Not Started** | Command ID/revision and tick/sequence/epoch rules are enforced separately |
| 4.5 | Exchange a minimal player command and world snapshot over the in-memory link | **Not Started** | A fake peer completes handshake and state exchange with no sockets or OpenMW |
| 4.6 | Add round-trip, property, golden-schema, mutation, and fuzz coverage | **Not Started** | Every decoder is registered with a corpus and sanitizer-backed fuzz target |

Exit gate:

- A simulated client completes a handshake and bounded state exchange in memory.
- Unknown optional capabilities are ignored, unknown required capabilities fail
  clearly, and legacy peers receive a clear incompatibility rejection when
  enough input is available to respond safely.
- Malformed input cannot cause partial state commits, unbounded allocation, or
  secret-bearing log output.

Implementation notes:

- Decode into validated temporary values; only deliver a message to a session or
  reducer after the entire envelope and payload pass bounds and semantic checks.
- Capability negotiation should produce an immutable negotiated-capability set
  used by encoders and handlers for the lifetime of a connection.
- Keep authentication claims opaque to protocol code. The session may consume a
  validated principal/session result but must not know backend credential format.
- Golden files cover vNext evolution only and must include an explanation when
  intentionally updated.

### Phase 5 — Deterministic authoritative server core

Status: **Not Started**

Outcome: one deterministic writer validates commands, owns minimal canonical
player state, publishes immutable snapshots, and emits changes to owned sinks.

Depends on: Phase 4.

| Slice | Deliverable | Status | Completion evidence |
|---|---|---|---|
| 5.1 | Implement fixed-tick scheduling, bounded per-tick command intake, and deterministic ordering | **Not Started** | Same initial state and command stream always produce the same result/checksum |
| 5.2 | Implement minimal player/session/cell/root-transform/velocity/ack canonical state | **Not Started** | State invariants and revision monotonicity tests pass |
| 5.3 | Implement command validation and atomic reducer application | **Not Started** | Invalid, stale, duplicate, and over-budget commands cause no partial state mutation |
| 5.4 | Publish immutable snapshots and versioned state-change events | **Not Started** | Readers cannot mutate canonical state and slow readers cannot block the writer indefinitely |
| 5.5 | Add idempotency windows, authority-epoch checks, state checksums, and explicit resync requests | **Not Started** | Duplicate/stale/epoch mismatch and divergence-repair tests pass |
| 5.6 | Add persistence, replay, script, and metrics sink interfaces without implementations | **Not Started** | All sinks receive the same committed change record after, never before, commit |
| 5.7 | Add reducer property tests and deterministic multi-client simulation tests | **Not Started** | Randomized command streams preserve invariants and reproduce by seed |

Exit gate:

- The server core has a single canonical mutation path and no renderer, engine,
  platform, socket, script-runtime, or database dependency.
- State commits are atomic, deterministic, revisioned, and observable.
- A slow or failed auxiliary consumer has an explicit bounded failure policy and
  cannot silently mutate or deadlock canonical state.

Implementation notes:

- Define total command ordering explicitly, including ties among connections and
  commands arriving for the same tick. Never depend on hash-container iteration.
- State-change records should describe domain changes, not serialized network or
  database bytes. Protocol, scripting, persistence, and metrics adapt from the
  same committed domain record.
- The first checksum need cover only canonical state present in this phase, but
  later phases must extend it whenever they add durable data.
- Persistence is intentionally only an interface here so Phase 20 does not force
  a later redesign of the server mutation boundary.

### Phase 6 — Maintained transport and secure network session

Status: **Not Started**

Outcome: the in-memory session runs over a maintained real transport with secure
identity, channel semantics, backpressure, and telemetry hidden behind owned APIs.

Depends on: Phase 5.

| Slice | Deliverable | Status | Completion evidence |
|---|---|---|---|
| 6.1 | Wrap connection/listen/connect/disconnect/cancellation/lifecycle in an owned transport interface | **Not Started** | No selected-library type crosses the adapter boundary |
| 6.2 | Map reliable operations and latest-wins snapshots to explicit transport channels | **Not Started** | Loss/reorder tests prove snapshots do not head-of-line block behind operations |
| 6.3 | Implement encryption, peer authentication plumbing, and credential redaction per ADR-0005 | **Not Started** | Invalid identity, downgrade, replay, timeout, and redaction tests pass |
| 6.4 | Implement bounded queues, priority, rate limits, backpressure, and slow-peer eviction | **Not Started** | Flood/slow-reader tests remain within configured memory/work budgets |
| 6.5 | Implement network telemetry and stable disconnect/rejection reasons | **Not Started** | Per-channel sent/received/dropped/retransmitted/queued metrics are asserted in tests |
| 6.6 | Integrate real sockets into the deterministic fault harness | **Not Started** | Localhost tests exercise faults above/below the adapter as supported |
| 6.7 | Prove Linux, Windows, and macOS build/test support | **Not Started** | Transport integration CI is green on every supported desktop platform |

Exit gate:

- A real client and server complete the Phase 4 exchange securely on all
  supported desktop platforms.
- Snapshot and reliable-operation delivery behave according to their distinct
  semantics under congestion and loss.
- Queues and per-peer work remain bounded under malformed, flooding, stalled, and
  disconnecting peers.

Implementation notes:

- The transport interface should expose product semantics rather than mimic the
  selected library one method at a time.
- Session resumption credentials must be short-lived/revocable according to the
  threat model and must never be persisted or logged in reusable plaintext form.
- Disconnect and rejection reasons need stable internal categories, with a
  separately sanitized user-facing message.
- Android support is a selection constraint, not a Phase 6 deliverable; retain a
  small compile proof if ADR-0005 uses Android portability as a deciding factor.

### Phase 7 — Headless end-to-end multiplayer slice

Status: **Not Started**

Outcome: a dedicated server and two fake clients complete connect, join, cell,
movement, observation, disconnect, and resume without OpenMW.

Depends on: Phase 6.

| Slice | Deliverable | Status | Completion evidence |
|---|---|---|---|
| 7.1 | Add the dedicated-server composition root and new minimal configuration format | **Not Started** | Server starts/stops cleanly; invalid config fails with bounded, actionable errors |
| 7.2 | Add a reusable headless client-session library and scripted fake-client driver | **Not Started** | Fake clients expose commands/snapshots without renderer or engine dependencies |
| 7.3 | Implement authentication, session creation, player creation, and join | **Not Started** | Two clients receive distinct stable identities and negotiated sessions |
| 7.4 | Implement one interior and one exterior cell fixture plus enter/leave observation | **Not Started** | Both clients receive correct initial and visibility state across transitions |
| 7.5 | Implement semantic movement commands and sequenced snapshots | **Not Started** | Remote root transform/velocity converges while stale snapshots are rejected |
| 7.6 | Implement disconnect, bounded grace period, resume, and clean expiration | **Not Started** | Resume preserves identity/revision/acks; expiration creates an explicit new session path |
| 7.7 | Add adverse-network matrix, reconnect loop, queue-bound, and soak tests | **Not Started** | Named profiles pass with deterministic seeds and recorded thresholds |

Exit gate:

- Two automated headless clients complete the full first-product flow against a
  real server process.
- The flow passes declared latency, loss, jitter, duplication, reordering, stall,
  and disconnect profiles without unbounded growth or durable divergence.
- The server can cleanly stop and restart; durable restore is not required yet.

Implementation notes:

- Keep the initial world fixture deliberately tiny: player identity, session,
  canonical cell, root transform, velocity, revision, and acknowledgement state.
- Fake-client scripts should emit a machine-readable timeline so CI failures can
  be replayed from seed and command trace.
- Define quantitative pass thresholds for queue depth, correction/convergence,
  resume time, and soak duration in the test configuration rather than prose-only
  expectations.

### Phase 8 — OpenMW desktop vertical slice

Status: **Not Started**

Outcome: a thin OpenMW 0.51 adapter completes the headless slice with two real
desktop clients while keeping engine-specific behavior isolated.

Depends on: Phase 7.

| Slice | Deliverable | Status | Completion evidence |
|---|---|---|---|
| 8.1 | Inventory the minimum required OpenMW hooks and approve the patch surface | **Not Started** | Hook document maps each patch to an adapter need and upstream strategy |
| 8.2 | Implement the adapter lifecycle and desktop input/presentation provider interfaces | **Not Started** | Adapter owns every direct OpenMW dependency; client-session remains headless |
| 8.3 | Connect/authenticate/join from OpenMW with actionable UI errors | **Not Started** | Version, capability, auth, timeout, and disconnect paths are testable |
| 8.4 | Map interior/exterior cell changes and spawn/despawn remote player presentation | **Not Started** | Two clients observe correct cell and peer lifecycle behavior |
| 8.5 | Convert input to semantic movement commands and apply authoritative local correction | **Not Started** | Prediction/reconciliation tests cover correction and hard-snap thresholds |
| 8.6 | Interpolate/extrapolate remote movement through a bounded jitter buffer | **Not Started** | Snapshot-age, buffer-depth, extrapolation, correction, and hard-snap metrics exist |
| 8.7 | Implement disconnect/resume presentation and automate the two-client slice | **Not Started** | CI or a documented harness executes the desktop flow and captures artifacts |

Exit gate:

- Two automated OpenMW desktop clients pass the same vertical slice and adverse
  network profiles as the headless clients.
- Protocol, server-core, and client-session still compile without OpenMW.
- Every required engine hook is documented, narrowly scoped, and represented in
  the maintained patch policy.

Implementation notes:

- The adapter translates between OpenMW state/events and owned domain types. It
  must not become a second canonical simulation or contain packet handling.
- Rendering and local input continue if the network stalls; network callbacks
  must not mutate OpenMW presentation state from arbitrary threads.
- Remote player representation may be minimal in this phase. Correct lifecycle,
  cell placement, and root motion take priority over animation fidelity.
- Keep client-session integration reusable so PC VR and a potential Quest client
  compose the same state machine rather than fork it.

### Phase 9 — PC VR interoperability gate

Status: **Not Started**

Outcome: desktop OpenMW and the maintained PC OpenMW-VR target interoperate using
one protocol, one client session, and optional sampled pose presentation.

Depends on: Phase 8.

| Slice | Deliverable | Status | Completion evidence |
|---|---|---|---|
| 9.1 | Accept ADR-0008 and create the separate OpenMW-VR worktree/patch target | **Not Started** | Pinned fork revision, rebase/update procedure, and patch ownership are recorded |
| 9.2 | Build the same adapter/client-session targets against desktop and PC VR engines | **Not Started** | Shared code is not copied; both targets build in their supported environments |
| 9.3 | Add optional `vr_pose` capability and bounded head/hand pose snapshot schema | **Not Started** | Negotiation, absent/unknown capability, bounds, round-trip, and fuzz tests pass |
| 9.4 | Implement VR input and presentation providers using semantic commands | **Not Started** | Locomotion command behavior matches desktop authority rules |
| 9.5 | Sample pose data independently of tracking/render rate and network snapshot rate | **Not Started** | Rate-limit, latest-wins, stale-pose, and buffer tests pass |
| 9.6 | Implement desktop fallback pose and non-VR ignore behavior | **Not Started** | Desktop peers render safely; clients without capability ignore pose data |
| 9.7 | Run desktop/desktop regression and desktop/VR interoperability suites | **Not Started** | Both suites pass without server-side platform branches |

Exit gate:

- Two desktop clients still pass Phase 8, and one desktop plus one PC VR client
  completes the same connect/join/cell/movement/reconnect flow.
- The authoritative root/capsule remains platform neutral; room-scale head motion
  cannot silently move it.
- VR rendering/tracking rate is independent of network pose sampling.

Implementation notes:

- Head and hand transforms are presentation snapshots associated with a player
  root and authority epoch. They are not persisted as durable world state.
- Physical reach validation uses the authoritative root plus declared limits;
  raw tracked coordinates are never accepted as authoritative interaction proof.
- OpenMW-VR stays outside the multiplayer core. Fork-specific code belongs in the
  provider/hook layer and maintained patch target.

### Phase 10 — Player lifecycle and content identity

Status: **Not Started**

Outcome: players join only with compatible content identity, have a complete
canonical lifecycle, and can be acted on by later moderation/admin interfaces.

Depends on: Phase 9.

| Slice | Deliverable | Status | Completion evidence |
|---|---|---|---|
| 10.1 | Define bounded content manifest, canonical record identity, and mismatch policy | **Not Started** | Order/hash/missing/extra/collision cases and user-facing rejection tests pass |
| 10.2 | Implement player create/join/spawn/despawn lifecycle and validated display metadata | **Not Started** | Duplicate, invalid, reconnect, and concurrent join cases preserve invariants |
| 10.3 | Implement session replacement, kick, ban/mute primitives, and reason categories | **Not Started** | Principal/player/session distinctions and audit events are tested |
| 10.4 | Implement reconnect/resume across lifecycle edges | **Not Started** | Cell change, death-in-progress placeholder, eviction, and expired-resume cases are explicit |
| 10.5 | Add lifecycle snapshots, script/persistence mappings, metrics, and fault tests | **Not Started** | Feature-slice contract is fully evidenced |

Exit gate:

- A player cannot enter the world with an incompatible required content manifest.
- Player, authenticated principal, connection, and resumable session are distinct
  identities with documented lifetimes.
- Lifecycle behavior remains deterministic under duplicate joins, disconnects,
  replacement connections, moderation actions, and reconnect.

Implementation notes:

- Content identity must not use local file paths. Canonical record references need
  stable manifest context and explicit collision handling.
- Moderation primitives belong in canonical server commands now; the secure
  administrative interface that invokes them arrives in Phase 21.
- Avoid persisting transport connection identifiers or secrets as player identity.

### Phase 11 — Canonical cells, interest, and resynchronization

Status: **Not Started**

Outcome: the server owns cell membership and interest, produces consistent
initial snapshots, and repairs divergence without reconnecting the whole server.

Depends on: Phase 10.

| Slice | Deliverable | Status | Completion evidence |
|---|---|---|---|
| 11.1 | Finalize canonical interior/exterior/worldspace `CellId` rules | **Not Started** | Normalization, content context, coordinate limits, and collision tests pass |
| 11.2 | Implement authoritative cell transition command/validation/state machine | **Not Started** | Simultaneous, rejected, interrupted, and reconnecting transitions are tested |
| 11.3 | Implement deterministic interest sets and enter/leave deltas | **Not Started** | Boundary churn and rapid travel do not leak or strand entities |
| 11.4 | Implement atomic initial snapshot barrier plus queued post-snapshot deltas | **Not Started** | No delta is lost or applied before its snapshot under reordering |
| 11.5 | Implement scoped checksum comparison and full/partial resync | **Not Started** | Injected divergence repairs identities/revisions without server restart |
| 11.6 | Add load, churn, fault, metrics, scripting, and persistence mapping tests | **Not Started** | Feature-slice contract is fully evidenced |

Exit gate:

- Initial state plus ordered deltas converges correctly during cell changes,
  interest churn, disconnect/resume, and deliberately injected divergence.
- Interest evaluation has explicit per-tick/per-peer budgets and observable
  overflow/defer behavior.

Implementation notes:

- Snapshot completion needs an explicit baseline revision/tick. Do not infer
  completion from a quiet connection.
- Interest changes are server decisions. Clients may suggest view context only
  through validated, bounded commands.
- Resync should be scoped to the smallest sound state domain, with a full session
  snapshot as the bounded fallback.

### Phase 12 — Production movement, animation, and pose replication

Status: **Not Started**

Outcome: the Phase 8/9 movement prototype becomes a production, observable,
server-validated system with animation and VR presentation state.

Depends on: Phase 11.

| Slice | Deliverable | Status | Completion evidence |
|---|---|---|---|
| 12.1 | Finalize semantic locomotion commands, input sequence/ack rules, and movement budgets | **Not Started** | Invalid speed/acceleration/time/cell/epoch commands are rejected deterministically |
| 12.2 | Implement server movement validation and authoritative root/capsule simulation policy | **Not Started** | Desktop and VR use identical durable movement rules |
| 12.3 | Harden client prediction, reconciliation, interpolation, and bounded extrapolation | **Not Started** | Quantitative convergence and hard-snap thresholds pass fault profiles |
| 12.4 | Add animation intent/state replication tied to authoritative motion | **Not Started** | Stale/impossible animation transitions cannot alter durable movement |
| 12.5 | Harden optional head/hand pose compression, validation, rate limiting, and fallback | **Not Started** | Pose loss/stall/abuse has bounded visual-only impact |
| 12.6 | Add movement/pose soak, metrics, resync, scripting, and persistence mappings | **Not Started** | Feature-slice contract is fully evidenced |

Exit gate:

- Movement converges within declared correction thresholds under every supported
  fault profile, with bounded queues and no accumulating positional divergence.
- VR pose traffic cannot move the authoritative root or exhaust per-peer work.
- Animation and pose degradation never blocks reliable gameplay operations.

Implementation notes:

- Snapshot payloads include simulation tick, sequence, transform, velocity, and
  authority epoch. They are latest-wins and must not use reliable ordered queues.
- Measure snapshot age, correction distance, hard snaps, jitter-buffer depth, and
  extrapolation time before tuning rates or compression.
- Teleport/cell-transition correction is a distinct explicit state change, not an
  extreme ordinary movement snapshot.

### Phase 13 — Actor lifecycle, AI state, and authority handoff

Status: **Not Started**

Outcome: non-player actors have stable lifecycle/state, deterministic ownership,
and safe lease-based authority transfer where delegation is justified.

Depends on: Phase 12.

| Slice | Deliverable | Status | Completion evidence |
|---|---|---|---|
| 13.1 | Implement actor stable identity, spawn, despawn, enable/disable, and base state | **Not Started** | Interest/reconnect/resync preserve identity and monotonic revisions |
| 13.2 | Implement actor transform, velocity, stance, and animation snapshots | **Not Started** | Latest-wins and stale/epoch behavior matches movement rules |
| 13.3 | Implement canonical AI state/intents and validated state transitions | **Not Started** | Impossible transitions and unowned commands are rejected |
| 13.4 | Implement authority lease grant/renew/revoke/expiry state machine if ADR-0006 permits delegation | **Not Started** | Old epochs fail immediately and lease expiry has a deterministic fallback |
| 13.5 | Implement atomic handoff snapshot before new-epoch deltas | **Not Started** | Loss/reorder/disconnect during every handoff edge converges safely |
| 13.6 | Add actor/authority churn soak, metrics, script, persistence, and resync tests | **Not Started** | Feature-slice contract is fully evidenced |

Exit gate:

- Actors converge through lifecycle, interest changes, reconnect, and authority
  churn without duplicate identity or accepted stale authority.
- Authority handoff records owner, epoch, transfer reason, and duration and can
  recover without restarting the server.

Implementation notes:

- Prefer server-owned actor simulation. Client delegation requires the explicit
  threat analysis and validation limits required by ADR-0006.
- A lease transfer commits a complete authoritative snapshot atomically before
  accepting deltas from the new epoch.
- Presentation animation is not permission to drive canonical AI or transform
  state outside the authority policy.

### Phase 14 — Interactive objects, locks, traps, and doors

Status: **Not Started**

Outcome: world-object interactions are authoritative, revisioned, contention-safe,
and visible consistently to interested clients.

Depends on: Phase 13.

| Slice | Deliverable | Status | Completion evidence |
|---|---|---|---|
| 14.1 | Implement stable placed-object identity and enable/disable/transform state | **Not Started** | Cell snapshot/resync/reconnect preserve identity and revision |
| 14.2 | Implement activation command/result/event flow with reach and state validation | **Not Started** | Duplicate, stale, simultaneous, remote, and VR reach cases are tested |
| 14.3 | Implement door open/close/teleport transition semantics | **Not Started** | Contention and interrupted cross-cell transitions converge |
| 14.4 | Implement lock, unlock, trap arm/trigger/disarm state machines | **Not Started** | Apply-once effects and expected-revision conflicts are deterministic |
| 14.5 | Implement authorized placement/move operations and bounds | **Not Started** | Invalid cell/transform/content references cannot commit |
| 14.6 | Add fault, contention, metrics, script, persistence, and resync tests | **Not Started** | Feature-slice contract is fully evidenced |

Exit gate:

- Simultaneous interaction produces one deterministic canonical outcome and clear
  results for winners/rejected commands.
- Reconnect and resync do not repeat apply-once trap, lock, activation, or door
  transition effects.

Implementation notes:

- Separate durable object state from presentation events such as sound or
  animation triggers. Replaying a snapshot must not replay one-shot effects.
- Use expected entity revision plus command ID for reliable interactions.
- VR reach is validated relative to the authoritative player root and declared
  limits, never solely from controller pose.

### Phase 15 — Inventory, equipment, and container transactions

Status: **Not Started**

Outcome: item ownership and equipment changes are atomic, idempotent, revisioned,
and safe under simultaneous access and reconnect.

Depends on: Phase 14.

| Slice | Deliverable | Status | Completion evidence |
|---|---|---|---|
| 15.1 | Define item/stack/instance identity, quantities, metadata, and canonical ownership | **Not Started** | Split/merge/collision/overflow/content-reference tests pass |
| 15.2 | Implement atomic add/remove/move/split/merge transaction reducer | **Not Started** | Conservation and no-duplication properties hold under randomized streams |
| 15.3 | Implement container open/view/update access and contention rules | **Not Started** | Concurrent viewers, stale revisions, disconnect, and interest loss are tested |
| 15.4 | Implement equipment/unequipment and slot validation | **Not Started** | Invalid slot, incompatible item, duplicate, and interrupted changes are rejected safely |
| 15.5 | Implement inventory/container initial snapshots, deltas, and resync | **Not Started** | Private data interest and reconnect convergence tests pass |
| 15.6 | Add idempotency, fault, contention, metrics, script, persistence, and checksum tests | **Not Started** | Feature-slice contract is fully evidenced |

Exit gate:

- Property tests demonstrate item conservation and absence of duplication/loss
  across retries, contention, disconnect, and resync.
- Private inventory/container data is disclosed only to authorized interested
  sessions.

Implementation notes:

- Treat an inventory change as a domain transaction with one command ID and one
  commit result, not a sequence of independently reliable packet mutations.
- Explicitly define stack equivalence and metadata that prevents unsafe merging.
- Persistence later stores canonical ownership/revisions, not client UI layout or
  transport acknowledgement state unless needed for bounded idempotency recovery.

### Phase 16 — Combat, stats, magic, death, and resurrection

Status: **Not Started**

Outcome: combat consequences are validated and server-owned, including stats,
effects, projectiles, death, and resurrection.

Depends on: Phase 15.

| Slice | Deliverable | Status | Completion evidence |
|---|---|---|---|
| 16.1 | Implement canonical stats/resources and bounded stat-change operations | **Not Started** | Clamp/overflow/ordering/reconnect/resync tests pass |
| 16.2 | Implement attack/cast commands, timing/range/resource validation, and result events | **Not Started** | Forged timing, reach, target, equipment, and duplicate commands are rejected |
| 16.3 | Implement damage/healing and active-effect lifecycle | **Not Started** | Stacking, expiry, dispel, repeated delivery, and ordering are deterministic |
| 16.4 | Implement projectile identity, spawn, movement policy, collision result, and despawn | **Not Started** | Loss/interest/reconnect cannot duplicate projectile consequences |
| 16.5 | Implement death, corpse state, respawn/resurrection, and lifecycle interactions | **Not Started** | Simultaneous lethal effects and reconnect at each edge converge |
| 16.6 | Add adversarial, fault, soak, metrics, script, persistence, and replay tests | **Not Started** | Feature-slice contract is fully evidenced |

Exit gate:

- No client or presentation pose can directly author damage, stats, inventory
  consumption, death, or resurrection.
- A deterministic replay of the same combat command stream produces the same
  state checksum and apply-once presentation events.

Implementation notes:

- Keep command intent, canonical outcome, and presentation event separate. A hit
  visual is not evidence that canonical damage occurred.
- Combat validation must use server-known state and explicit lag-handling policy
  from ADR-0006; do not silently trust client timestamps.
- Add threat-model cases for rate abuse, forged targets, impossible reach, stale
  authority, resource bypass, and replayed commands.

### Phase 17 — Dialogue, journals, factions, and quests

Status: **Not Started**

Outcome: narrative progression has explicit per-player/shared ownership,
revisioned transitions, and deterministic multiplayer contention behavior.

Depends on: Phase 16.

| Slice | Deliverable | Status | Completion evidence |
|---|---|---|---|
| 17.1 | Implement dialogue session start/select/end commands and validation | **Not Started** | Speaker/actor/range/topic/state/reconnect cases are tested |
| 17.2 | Implement per-player journal and topic state | **Not Started** | Monotonic progression, duplicate entries, resync, and privacy tests pass |
| 17.3 | Implement faction membership/rank/reputation and disposition inputs | **Not Started** | Authorization, bounds, ordering, and conflict tests pass |
| 17.4 | Implement explicit per-player and shared quest-state models | **Not Started** | Ownership cannot change implicitly; contention policy is deterministic |
| 17.5 | Implement quest/journal/faction snapshots, deltas, checksums, and reconnect | **Not Started** | Initial snapshot plus deltas converges under injected faults |
| 17.6 | Add metrics, scripting contract, persistence mapping, and narrative replay tests | **Not Started** | Feature-slice contract is fully evidenced |

Exit gate:

- Every narrative value declares whether it is per-player, party/group if later
  introduced, or shared world state; no scope is inferred from the sender.
- Duplicate/reordered commands and reconnect cannot regress or repeat apply-once
  progression unless the game rule explicitly permits it.

Implementation notes:

- This phase implements canonical state and commands, not the general-purpose
  server scripting runtime. It must still emit typed domain events for Phase 19.
- Dialogue presentation stays client-side while dialogue eligibility and durable
  consequences use validated canonical state.
- Record intentional differences from single-player behavior as product rules,
  not undocumented race outcomes.

### Phase 18 — Time, weather, and durable world state

Status: **Not Started**

Outcome: shared world clocks, weather, globals, and other durable world values
have explicit ownership, evolution, interest, and presentation behavior.

Depends on: Phase 17.

| Slice | Deliverable | Status | Completion evidence |
|---|---|---|---|
| 18.1 | Implement canonical game time/calendar, pause/rate policy, and synchronization | **Not Started** | Drift, rate change, reconnect, and server-stall behavior is bounded and tested |
| 18.2 | Implement regional weather state, transition commands, and presentation snapshots | **Not Started** | Cell/region interest, reconnect, and transition interpolation converge |
| 18.3 | Implement typed durable globals/world flags with ownership and bounds | **Not Started** | Unknown/type mismatch/stale revision/content mismatch cases fail safely |
| 18.4 | Implement scheduled world events and apply-once presentation events | **Not Started** | Tick jumps, restart mapping, duplicate delivery, and resync are tested |
| 18.5 | Add world-state checksums, metrics, script/persistence mappings, fault and soak tests | **Not Started** | Feature-slice contract is fully evidenced |

Exit gate:

- Time, weather, and globals restore/converge from canonical snapshot plus ordered
  changes without using a client clock as durable authority.
- All newly durable values participate in state checksums and have explicit
  scripting and persistence types.

Implementation notes:

- Separate canonical transition parameters from client-local visual interpolation.
- Use typed keys/values or schema-defined records for globals; do not expose an
  unbounded arbitrary network dictionary.
- Server maintenance pause and no-player-online policy must be explicit because
  they affect later persistence/replay behavior.

### Phase 19 — Versioned server scripting

Status: **Not Started**

Outcome: pinned server scripts receive immutable typed events and submit explicit
commands through the same validation path as every other authority.

Depends on: Phase 18.

| Slice | Deliverable | Status | Completion evidence |
|---|---|---|---|
| 19.1 | Accept ADR-0009 and pin the language/runtime/dependency | **Not Started** | Isolation, determinism, resource limits, debugging, license, and support policy are recorded |
| 19.2 | Define versioned typed event and command APIs for all implemented domains | **Not Started** | API schema/docs and compatibility tests exist independently of packet layouts |
| 19.3 | Implement immutable event delivery and validated command submission | **Not Started** | Scripts cannot directly mutate canonical state or retain unsafe mutable views |
| 19.4 | Implement callback time/instruction/memory limits and failure isolation | **Not Started** | Timeout, exception, allocation pressure, bad command, and reload failures are recoverable |
| 19.5 | Implement script bundle manifest/pinning and API/protocol compatibility checks | **Not Started** | Mismatched or partially loaded bundles fail before world mutation |
| 19.6 | Add reference scripts, API docs, fuzz tests, metrics, and deterministic integration tests | **Not Started** | Script failures cannot corrupt state or terminate the server unexpectedly |

Exit gate:

- Scripts cannot access packet layouts, transport objects, mutable global server
  buffers, credentials, or direct persistence mutation.
- Every script command follows normal authority, validation, revision,
  idempotency, logging, and replay rules.
- Script API and bundle versions are explicit and tested with the server release.

Implementation notes:

- Reusing Lua as a language is allowed only if selected by ADR-0009; the legacy
  CoreScripts binding/API is never reused for compatibility.
- Events should be immutable snapshots or values with bounded lifetime. Commands
  are queued for a deterministic server tick rather than applied during callbacks.
- Define callback ordering and script-generated command ordering so replay does
  not depend on hash order, worker timing, or filesystem enumeration.

### Phase 20 — Transactional persistence and deterministic replay

Status: **Not Started**

Outcome: canonical state survives restart transactionally, evolves through
versioned schemas, can be backed up/restored, and is reproducible from command
and event records.

Depends on: Phase 19.

| Slice | Deliverable | Status | Completion evidence |
|---|---|---|---|
| 20.1 | Accept ADR-0010 and implement the persistence adapter boundary | **Not Started** | Store/runtime types do not leak into server-core domain APIs |
| 20.2 | Implement transactional durable snapshots/change records and commit coordination | **Not Started** | Crash/failure injection cannot expose a partially committed canonical revision |
| 20.3 | Implement persistence schema versioning and forward migration tooling | **Not Started** | Supported migrations and unsupported downgrade/error paths are tested |
| 20.4 | Implement online-safe backup and verified restore workflow | **Not Started** | Restore reproduces stable identities, revisions, script bundle, and checksum |
| 20.5 | Implement deterministic command/change replay and diagnostic trace export | **Not Started** | Recorded stream reproduces the same state checksum at declared checkpoints |
| 20.6 | Add corruption, disk-full, interrupted-write, restart, migration, and soak tests | **Not Started** | Failures are recoverable or fail closed with actionable diagnostics |

Exit gate:

- Persistence restores identical canonical identities and revisions, and a
  recorded command stream reproduces identical declared checksums.
- Backup/restore and schema migration are automated tests, not documentation-only
  procedures.
- Persistence failure has an explicit server policy and cannot silently continue
  while claiming durable commits.

Implementation notes:

- Persist domain state/change records, not protocol payloads, OpenMW object
  pointers, renderer state, or transport-library objects.
- Define the exact durability acknowledgement point: clients/scripts/admin tools
  must not be told an operation is durable before the selected commit policy is
  satisfied.
- Replay inputs include negotiated rules/configuration, content manifest, script
  bundle/API versions, deterministic seed, and command ordering metadata.
- TES3MP 0.8.x save or persistence migration is explicitly out of scope.

### Phase 21 — Administration, moderation, and discovery

Status: **Not Started**

Outcome: operators can configure, observe, moderate, administer, and advertise a
server through authenticated, auditable, versioned interfaces.

Depends on: Phase 20.

| Slice | Deliverable | Status | Completion evidence |
|---|---|---|---|
| 21.1 | Finalize the new validated server configuration format and secret-source policy | **Not Started** | Invalid/deprecated/unknown settings fail clearly; secrets are not dumped or logged |
| 21.2 | Implement authenticated administrative principals, roles, and authorization | **Not Started** | Least-privilege, revocation, expiry, and confused-deputy tests pass |
| 21.3 | Implement versioned admin commands through the canonical command path | **Not Started** | State-changing admin actions validate, persist, replay, and emit audit events |
| 21.4 | Expose moderation workflows using Phase 10 primitives | **Not Started** | Kick/ban/mute/reason/reconnect behavior and authorization are end-to-end tested |
| 21.5 | Implement bounded status/health/metrics and structured audit export | **Not Started** | Sensitive fields are redacted and slow consumers cannot affect the server writer |
| 21.6 | Implement opt-in discovery/server-list registration and query policy | **Not Started** | Spoofing, stale registration, privacy, rate-limit, and outage behavior are tested |

Exit gate:

- All state-changing operator actions are authenticated, authorized, validated,
  auditable, and use the same persistence/replay path as other commands.
- A server can operate privately without discovery, and discovery failure cannot
  stop an otherwise healthy server.

Implementation notes:

- Do not expose scripting/runtime internals as the administrative API.
- Health and metrics endpoints should separate public-safe status from privileged
  operational detail.
- The new config/admin/discovery formats have no TES3MP 0.8 compatibility goal.

### Phase 22 — Desktop and PC VR stabilization and release

Status: **Not Started**

Outcome: desktop clients, PC VR clients, and the dedicated server are packaged,
documented, secure, observable, and stable under release workloads.

Depends on: Phase 21.

| Slice | Deliverable | Status | Completion evidence |
|---|---|---|---|
| 22.1 | Define measurable release SLOs/budgets and supported platform matrix | **Not Started** | Tick, latency, queue, memory, player/entity, reconnect, and soak targets are versioned |
| 22.2 | Complete sanitizer, fuzz, dependency, threat-model, and security review closure | **Not Started** | No open release-blocking finding; residual risks are documented |
| 22.3 | Complete performance profiling, load, fault, authority-churn, and long soak suites | **Not Started** | Budgets pass with retained traces/metrics and no accumulating divergence |
| 22.4 | Harden reconnect, resync, persistence recovery, and authority transfer | **Not Started** | Recovery succeeds without server restart in supported failure cases |
| 22.5 | Package signed/versioned desktop client, PC VR client, and server artifacts | **Not Started** | Clean-machine install/start/upgrade/uninstall smoke tests pass |
| 22.6 | Publish protocol, scripting, configuration, baseline/update, operations, and incompatibility docs | **Not Started** | Release docs clearly require new clients, servers, scripts, config, and persistence data |
| 22.7 | Execute the release-candidate matrix and archive evidence | **Not Started** | All declared release gates pass from clean tagged source |

Exit gate:

- Desktop and PC VR release criteria pass without archived multiplayer code.
- Long-running tests show bounded queues, stable resource use, and no accumulating
  canonical divergence.
- Release artifacts are reproducible from the tagged source and explicitly reject
  incompatible TES3MP 0.8.x peers/components.

Implementation notes:

- Quantitative budgets should be established from measured representative scenes
  before optimization work is accepted as complete.
- A baseline OpenMW update policy must state how upstream security/stability
  updates are evaluated without coupling protocol version to engine version.
- Retain minimized fuzz inputs, failed simulation seeds, replay traces, and soak
  metrics as regression assets when they expose a defect.

### Phase 23 — Meta Quest 3 feasibility decision

Status: **Not Started**

Outcome: only after the desktop/PC VR release is stable, hardware evidence drives
an explicit go/no-go decision for a supported Quest 3 multiplayer port.

Depends on: Phase 22.

| Slice | Deliverable | Status | Completion evidence |
|---|---|---|---|
| 23.1 | Cross-compile OpenMW client plus multiplayer dependencies for a current supported Android ARM64 NDK | **Not Started** | Reproducible proof build and dependency/license manifest exist |
| 23.2 | Package and launch a minimal native OpenXR APK on Quest 3 | **Not Started** | On-device loader, lifecycle, logging, rendering, and controller evidence is recorded |
| 23.3 | Prototype and measure viable rendering routes | **Not Started** | GLES/Vulkan/compatibility candidates have comparable CPU/GPU/memory/thermal traces |
| 23.4 | Prove minimum networking, audio, storage, suspend/resume, and legal game-data import paths | **Not Started** | Each platform risk has an on-device result and bounded remaining work |
| 23.5 | Define preliminary loading, frame-time, memory, thermal, and battery budgets | **Not Started** | Repeatable hardware scenarios and measurement tooling are checked in/documented |
| 23.6 | Accept ADR-0011 with an explicit go/no-go result and support scope | **Not Started** | Evidence, selected route, risks, upstream candidates, and Phase 24 activation decision are recorded |

Exit gate:

- An on-device prototype resolves the major toolchain, OpenXR, rendering,
  lifecycle, networking, storage, and performance unknowns.
- ADR-0011 makes an evidence-backed go/no-go decision. A go decision activates
  Phase 24; a no-go decision leaves Phase 24 **Not Started** and records the
  conditions that would justify reconsideration.
- Either result completes the feasibility phase without weakening Phase 22
  release criteria.

Implementation notes:

- Community Quest experiments are research references, not dependencies or code
  templates. Re-evaluate their status/licenses only when this phase begins.
- Quest constraints may motivate general client optimizations, but must not add
  Android/OpenXR/headset concepts to protocol or canonical server state.
- Upstream generally useful OpenMW/OpenMW-VR changes when practical rather than
  accumulating an unbounded permanent platform fork.

### Phase 24 — Meta Quest 3 multiplayer port

Status: **Not Started**

Outcome: if ADR-0011 records a go decision, a supportable Quest 3 client completes
the existing multiplayer slice and meets measured on-device budgets.

Depends on: Phase 23 with an ADR-0011 go decision.

| Slice | Deliverable | Status | Completion evidence |
|---|---|---|---|
| 24.1 | Productionize the Android ARM64/OpenXR build, packaging, signing, and dependency pipeline | **Not Started** | Reproducible APK artifacts and clean-machine build instructions pass |
| 24.2 | Implement Android lifecycle, suspend/resume, audio, networking, scoped storage, and legal data import | **Not Started** | On-device lifecycle/platform compliance suite passes |
| 24.3 | Compose the existing client session, adapter boundary, semantic commands, and `vr_pose` capability | **Not Started** | No Quest-specific protocol, durable state, or server branch is introduced |
| 24.4 | Complete Quest controller, locomotion, UI, pose presentation, and desktop fallback behavior | **Not Started** | Quest/desktop interaction behavior satisfies the existing VR authority rules |
| 24.5 | Pass Quest/desktop/PC VR interoperability and suspend/reconnect tests | **Not Started** | Quest completes connect/join/cell/movement/reconnect with both peer types |
| 24.6 | Pass loading, frame-time, memory, thermal, battery, and hardware soak budgets | **Not Started** | Hardware-runner evidence meets the ADR-0011 support policy |
| 24.7 | Package, document, and execute the Quest release-candidate matrix | **Not Started** | Installation, data import, permissions, update, recovery, and support docs are verified |

Exit gate:

- Quest 3 completes the same vNext vertical slice and interoperates with desktop
  and PC VR without platform-specific protocol or server logic.
- Suspend/resume safely resumes or performs an explicit clean reconnect.
- Real-hardware soak stays within declared performance, thermal, memory, and
  battery budgets.

Implementation notes:

- This phase is conditional and remains **Not Started** unless ADR-0011 records a
  go decision. A no-go decision in Phase 23 does not block program completion.
- Reuse the established `vr_pose` capability and platform-neutral authoritative
  root/capsule. Quest is another provider/composition target, not another core.
- Device-specific work belongs in the platform/adapter layer and a bounded patch
  queue with upstream candidates tracked explicitly.

## Phase update procedure

Every pull request that advances a slice must update this document in the same
change or immediately linked follow-up:

1. Change the slice to **In Progress** when the first non-disposable artifact
   lands, and change the phase to **In Progress** if needed.
2. Append an implementation note with the date, commit/PR, important design
   details, deviations from this plan, and exact verification evidence.
3. Change a slice to **Implemented** only when its completion evidence exists.
4. Run the entire phase exit gate after all slices are implemented. Record the
   gate evidence, then change the phase and program tracker row to **Implemented**.
5. Add newly discovered work as a bounded slice in the correct phase. Do not hide
   required work in an implementation note or mark a phase complete with a known
   unmet gate.

Use this note format:

```markdown
- YYYY-MM-DD — Slice N.N — Status
  - Change: commit/PR and concise implementation description.
  - Decisions: important choices or approved deviation from the plan/ADR.
  - Verification: exact local commands and CI job/artifact links.
  - Follow-ups: bounded work and owning phase, or `none`.
```

## Program completion gate

The vNext program, excluding optional Quest Phases 23 and 24, is complete when
Phase 22 is **Implemented** and all of the following remain true:

- The active product is based on a supported OpenMW stable release with an
  explicit update policy.
- Desktop and PC VR use the same bounded protocol, deterministic server core,
  client session, and gameplay rules.
- The OpenMW adapter and engine patch surface are narrow and documented.
- Server scripting and persistence are versioned, transactional, tested product
  boundaries rather than packet- or engine-coupled extension points.
- Adverse-network, reconnect, resync, authority-transfer, security, and soak gates
  pass with measurable evidence.
- No legacy protocol, RakNet/CrabNet integration, packet processor, CoreScripts
  ABI, configuration, persistence, or modified engine code is part of the active
  implementation.
