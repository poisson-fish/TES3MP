# TES3MP vNext implementation plan

Document type: living implementation plan

Updated: 2026-09-01

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
- **Implemented**: every required slice is implemented, its approved behavior has
  been demonstrated, and the phase exit gate is satisfied with recorded evidence.

Phase status is maintained explicitly. It is not inferred from code existing in
a branch. If a completed phase needs material rework, move it back to **In
Progress** and record why in its implementation notes.

Documentation and exploratory spikes do not advance an implementation slice
unless they are named deliverables of that slice. A spike must either be removed
or converted into tested production code before a phase can be **Implemented**.

## Project-owner collaboration and decision gates

Implementation is a collaborative design process, not permission to turn an
underspecified idea into whichever behavior is easiest to code. The project
owner must remain in the loop on architecture, authority, state scope, security,
user-visible behavior, scripting, persistence, and compatibility decisions.

Production implementation must not begin for an unresolved decision. Before an
affected slice moves beyond research or a disposable spike:

1. Identify the concrete question and the systems/players it affects.
2. Present a small decision packet containing realistic scenarios, viable
   options, tradeoffs, a recommendation, and proposed acceptance tests.
3. Discuss the packet with the project owner and answer follow-up questions.
4. Record the explicit owner-approved decision in an ADR for architecture or a
   Gameplay Decision Record (GDR) for world behavior.
5. Implement only the approved behavior and make the tests read like the
   approved scenarios.
6. Demonstrate the completed behavior and any important failure/contention case
   to the project owner before marking the slice **Implemented**.

Silence, an unanswered question, an implementation convenience, existing legacy
behavior, or code already written is not approval. Engineers and agents may
recommend a default, prototype options, or continue unrelated work, but they may
not silently settle an open product or architecture question.

If a decision appears during implementation, pause the affected part of the
slice, record the question, and bring it back to the project owner. Keep the
phase **In Progress** while safe unrelated work continues. If an approved
decision later changes, reopen its record, document migration/compatibility
consequences, and return affected implemented slices/phases to **In Progress**.

Required review points are:

- **Phase kickoff:** review the phase outcome, open decisions, proposed slice
  boundaries, and risks before production code starts.
- **Decision review:** approve each required ADR/GDR before dependent code lands.
- **Behavior review:** walk through concrete single-player, two-player,
  contention, reconnect, and failure scenarios before schemas/reducers harden.
- **Implementation demo:** compare observed behavior and test evidence with the
  approved scenarios.
- **Exit-gate review:** confirm the phase is complete, note deviations/follow-ups,
  and explicitly approve moving to the next phase.

Uncontroversial mechanical details that do not affect behavior or durable
architecture may be implemented without a separate decision record. If there is
reasonable doubt about whether a choice affects player experience, authority,
scope, security, interoperability, persistence, or future extensibility, it is a
decision and must be surfaced.

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
| 0 | Archive and cutover preparation | **Implemented** | — |
| 1 | Clean OpenMW 0.51 baseline | **Implemented** | Phase 0 |
| 2 | Security and architecture decisions | **Implemented** | Phase 1 |
| 3 | Independent targets and test scaffold | **Implemented** | Phase 2 |
| 4 | Bounded protocol and in-memory session | **Implemented** | Phase 3 |
| 5 | Deterministic authoritative server core | **Implemented** | Phase 4 |
| 6 | Maintained transport and secure network session | **Implemented** | Phase 5 |
| 7 | Headless end-to-end multiplayer slice | **In Progress** | Phase 6 |
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
13. Architecture and gameplay semantics are approved before their production
    schema, state model, or API lands. A convenient data layout must not decide
    product behavior by accident.
14. Tests must include multi-client scenarios that prove state scope and
    visibility, not merely that one client sees a successful interaction.
15. An implementation demo and owner review are completion evidence for
    user-visible or architecturally significant slices; passing tests alone is
    necessary but not sufficient.
16. Beginning with Phase 4, each slice runs its applicable repository-owned
    local verification, while the complete vNext baseline, dependency-proof,
    and runtime-safety workflows run once by manual dispatch against the
    phase-completion candidate. Every declared job, including macOS x86-64,
    must pass before exit approval. Push, pull-request, schedule, and release
    events do not launch these workflows; a failed gate is corrected and rerun
    before the phase may be marked **Implemented**.

## Authority, state scope, and presentation

Every interaction design must answer three separate questions:

1. **Authority:** who may propose the action, who validates it, and who commits
   the result?
2. **State scope:** whose canonical reality changes—global world, one player, an
   explicitly modeled group, or an explicitly modeled world instance?
3. **Presentation:** which clients observe the durable result and which
   transient/local effects do they render?

Server authority does not automatically mean global state. A journal entry can
be server-authoritative and per-player. Conversely, a door can be
server-authoritative and global while each client renders its animation locally.
Client prediction is speculative presentation and must reconcile to the approved
canonical result; it does not change authority or scope.

The initial recommendations brought to the project owner are:

- Durable physical world state is server-authoritative and global by default.
- Player knowledge and progression is server-authoritative and per-player by
  default.
- Group-scoped or instanced state exists only after the group/instance identity,
  membership, persistence, and visibility rules are intentionally designed.
- Cosmetic effects with no gameplay consequence may remain client-local.

These are recommendation defaults, not blanket approval. Each gameplay domain
still requires concrete scenario review. Per-player or group-scoped world state
must be represented as canonical keyed state; it must never be approximated by
silently hiding a global update on selected clients.

For example, trap disarming always begins as a semantic attempt sent to the
server. The open product question is the scope of the successful result:

- Global: `TrapState(EntityId) = Disarmed`; every interested player sees the
  revisioned change and simultaneous attempts resolve against that revision.
- Per-player: `TrapState(PlayerId, EntityId) = Disarmed`; the server retains a
  distinct canonical relation and the trap remains armed for other players.
- Group/instance: the state is keyed by an explicitly approved group or instance
  identity with defined membership and lifecycle.

The implementation cannot choose among these by selecting an easy packet shape.
GDR-0006 must record the project owner's decision, contention examples, reset
rules, persistence behavior, and two-client acceptance tests before trap state is
implemented.

Every ADR/GDR decision packet must contain:

- The question in player-visible terms and why it must be decided now.
- Representative single-player, two-player, late-join, reconnect, contention,
  reset, and failure scenarios as applicable.
- Viable options and their gameplay, security, protocol, scripting,
  persistence, migration, VR, and operational consequences.
- A clear recommendation with reasoning, while preserving the alternatives for
  discussion.
- The approved authority, state scope, visibility, lifetime/reset, ordering,
  revision/idempotency, persistence, resync, and script behavior.
- Acceptance scenarios that can become named automated tests and a demo script.
- Explicit project-owner approval with date and reference to the discussion.

GDRs will live under `docs/vnext/gdr/`. Small decisions may share a domain GDR,
but each question and approval must remain individually identifiable. A GDR is a
product behavior contract; an ADR records durable technical structure. Some
features require both.

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
| ADR-0001 | Baseline-cutover Git mechanics | Phase 1 | **Implemented** |
| ADR-0002 | Supported desktop platforms, compilers, and dependency policy | Phase 1 | **Implemented** |
| ADR-0003 | Threat model and trust boundaries | Phase 3 | **Implemented** |
| ADR-0004 | Protocol schema, codec, and evolution policy | Phase 4 | **Implemented** |
| ADR-0005 | Transport, encryption, authentication, and session resumption | Phase 6 | **Implemented** |
| ADR-0006 | Authority, state-scope, prediction, and presentation policy | Phase 7 | **Implemented** |
| ADR-0007 | OpenMW hook and patch-queue policy | Phase 8 | **Implemented** |
| ADR-0008 | PC VR fork/worktree maintenance policy | Phase 9 | **Not Started** |
| ADR-0009 | Scripting language, runtime, isolation, and API model | Phase 19 | **Not Started** |
| ADR-0010 | Persistence store, schema evolution, backup, and replay model | Phase 20 | **Not Started** |
| ADR-0011 | Administration authorization, audit, and discovery exposure | Phase 21 | **Not Started** |
| ADR-0012 | Quest rendering, packaging, and hardware-support policy | Phase 24 | **Not Started** |
| ADR-0013 | Deterministic simulation, numeric, ordering, and protocol compatibility policy | Phases 3-5 | **Implemented** |
| ADR-0014 | Phase 3 target topology and boundary enforcement | Phase 3 | **Implemented** |
| ADR-0015 | Strong value types and identity/counter policy | Phase 3 | **Implemented** |
| ADR-0016 | Canonical spatial, command, and snapshot primitives | Phase 3 | **Implemented** |
| ADR-0017 | Deterministic facilities and harness boundaries | Phase 3 | **Implemented** |
| ADR-0018 | Deterministic network fault controls | Phase 3 | **Implemented** |
| ADR-0019 | Runtime-safety and fuzz CI policy | Phase 3 | **Implemented** |
| ADR-0020 | Owned observability interfaces and test sinks | Phase 3 | **Implemented** |
| ADR-0021 | Bounded protocol framing, classification, byte budgets, and decode results | Phase 4 | **Implemented** |
| ADR-0022 | Version/capability negotiation, hello/rejection schemas, and legacy-input response boundary | Phase 4 | **Implemented** |
| ADR-0023 | Session state machines and authentication-provider boundary | Phase 4 | **Implemented** |
| ADR-0024 | Reliable-operation and latest-wins envelope contract | Phase 4 | **Implemented** |
| ADR-0025 | Minimal player command, world-snapshot roots, session guards, and in-memory exchange | Phase 4 | **Implemented** |
| ADR-0026 | Phase 5 writer command intake, ordering, limits, and overload policy | Phase 5 | **Implemented** |
| ADR-0027 | Phase 5 canonical player/session state, scope, invariants, and bounds | Phase 5 | **Implemented** |
| ADR-0028 | Phase 5 command validation, disposition, acknowledgement, and atomic reducer boundary | Phase 5 | **Implemented** |
| ADR-0029 | Phase 5 immutable canonical publication, versioned change feed, retention, and slow-reader policy | Phase 5 | **Implemented** |
| ADR-0030 | Phase 5 bounded idempotency, canonical checksum, and resync boundary | Phase 5 | **Implemented** |
| ADR-0031 | Phase 5 committed domain sink ownership, delivery, failure, and resource-bound policy | Phase 5 | **Implemented** |
| ADR-0032 | Phase 6 selected transport adapter topology, lifecycle, endpoint, pumping, and initial bounds | Phase 6 | **Implemented** |
| ADR-0033 | Phase 6 fixed transport channels, owned byte delivery, lane scheduling, and minimal failure semantics | Phase 6 | **Implemented** |
| ADR-0034 | Phase 6 credential and resumption boundary | Phase 6 | **Implemented** |
| ADR-0035 | Phase 6 transport admission-scope handoff and derivation | Phase 6 | **Implemented** |
| ADR-0036 | Phase 6 authentication composition and session finalization | Phase 6 | **Implemented** |
| ADR-0037 | Phase 6 bounded outbound queues and slow-peer policy | Phase 6 | **Implemented** |
| ADR-0038 | Phase 6 network telemetry and stable reasons | Phase 6 | **Implemented** |
| ADR-0039 | Phase 6 deterministic real-transport fault boundary | Phase 6 | **Implemented** |

An ADR is complete only when it records considered alternatives, selection
criteria, consequences, failure modes, a replacement/review trigger, and
explicit project-owner approval. A library-selection ADR must also record
maintenance health, license, supported platforms, security properties,
dependency pinning, and a minimal proof build. Research can precede approval;
production code that depends on the choice cannot.

## Gameplay decision register

| GDR | Behavior requiring owner approval | First needed by | Status |
|---|---|---|---|
| GDR-0001 | First vertical-slice session, cell entry, player visibility, movement, and reconnect semantics | Phase 7 | **Not Started** |
| GDR-0002 | Player identity/lifecycle, content mismatch, replacement connection, and moderation semantics | Phase 10 | **Not Started** |
| GDR-0003 | Cell transitions, interest visibility, initial state, and resynchronization semantics | Phase 11 | **Not Started** |
| GDR-0004 | Movement validation, prediction/correction, animation, teleport, and VR pose semantics | Phase 12 | **Not Started** |
| GDR-0005 | Actor/AI ownership, lifecycle, simulation, and authority-delegation semantics | Phase 13 | **Not Started** |
| GDR-0006 | Physical-object interaction scope, including activation, placement, locks, traps, and doors | Phase 14 | **Not Started** |
| GDR-0007 | Item ownership, loot scope, container visibility/contention, and equipment semantics | Phase 15 | **Not Started** |
| GDR-0008 | Combat resolution, lag handling, effects, projectiles, death, and resurrection semantics | Phase 16 | **Not Started** |
| GDR-0009 | Dialogue, journal, faction, quest, and progression state scope | Phase 17 | **Not Started** |
| GDR-0010 | Time, weather, globals, reset, and shared-world evolution semantics | Phase 18 | **Not Started** |
| GDR-0011 | Phase 4 minimal player-intent authority and session-targeted snapshot semantics | Phase 4 | **Implemented** |
| GDR-0012 | Phase 5 minimal motion reducer effect, scope, and contention semantics | Phase 5 | **Implemented** |

A GDR is **Implemented** only after its questions are explicitly approved and
the approval is recorded. Later code slices remain separate: an approved design
is not evidence that the behavior has been implemented. If implementation
reveals a missing or materially different scenario, the GDR returns to **In
Progress** for owner review before that behavior continues.

## Feature-slice contract

Phases 10 through 18 use the following checklist. A phase cannot be marked
**Implemented** until its implementation notes link each item to code and tests:

- Approved GDR questions, decision rationale, owner approval, scenario tests,
  and implementation demo evidence.
- Semantic commands, authority owner, validation, and abuse limits.
- Explicit global/per-player/group/instance state scope, visibility, lifetime,
  reset policy, and the canonical key that represents it.
- Bounded schemas and explicit collection/string/rate limits.
- Stable identity, revision, authority epoch, and idempotency rules as relevant.
- Initial snapshot, delta ordering, reconnect, and full-resync behavior.
- Interest rules and behavior when an entity enters or leaves interest.
- Desktop presentation and relevant VR presentation/fallback behavior.
- Loss, latency, duplication, reordering, disconnect, and contention coverage.
- Metrics, structured logs, and actionable rejection/error categories.
- Persistence and script event/command mapping, even before those backends exist.
- A deterministic state checksum addition for newly durable data.

The GDR review happens before the production schema and reducer for the feature.
The implementation demo happens after the automated scenarios pass and before
the feature slice or phase is marked **Implemented**.

## Detailed phases

### Phase 0 — Archive and cutover preparation

Status: **Implemented**

Outcome: the legacy boundary is permanent, the cutover is rehearsed, and the
team has enough recorded context to replace the active source safely.

| Slice | Deliverable | Status | Completion evidence |
|---|---|---|---|
| 0.1 | Record the accepted clean-break direction, source branch point, and OpenMW baseline | **Implemented** | `docs/vnext/README.md` at `86cfa5ab3` |
| 0.2 | Create a permanent annotated archive tag at `49be5b640` without moving or reusing the existing `tes3mp-0.8.1` tag | **Implemented** | Annotated tag `tes3mp-0.8.1-archive` (tag object `1f3bc4c651573a60b4326b5d4703b6fad4b7fccf`) is published on `origin` and peels to `49be5b6405d6ab427e06ed350cf76c715a1f3bdd` |
| 0.3 | Write a high-level legacy gameplay feature inventory for reference only | **Implemented** | [`LEGACY_GAMEPLAY_FEATURE_INVENTORY.md`](LEGACY_GAMEPLAY_FEATURE_INVENTORY.md) groups repository-backed user-visible behavior without packet/API porting tasks |
| 0.4 | Review desktop, PC VR, Quest, and toolchain options with the owner and approve ADR-0002 | **Implemented** | Owner approved Option A for all four decisions in [`ADR-0002`](adr/ADR-0002-platform-toolchain-policy.md) on 2026-08-25, with macOS desktop supported and macOS PC VR outside the initial release scope |
| 0.5 | Prepare ADR-0001, review the irreversible Git mechanics with the owner, and rehearse the cutover in a disposable branch/worktree | **Implemented** | Owner-approved [`ADR-0001`](adr/ADR-0001-baseline-cutover-git-mechanics.md) records the exact mechanics; disposable production-form rehearsal `e042db240` verified ancestry, tree identity, abort, and rollback |
| 0.6 | Capture pre-cutover repository provenance | **Implemented** | Published commit `1dc1d5bd00519efa5b92a917c46f9407e9e28257` adds [`PRE_CUTOVER_PROVENANCE.md`](PRE_CUTOVER_PROVENANCE.md); post-push preflight verified matching local/tracking/remote refs, clean state, archive tag, submodule pin, and repository integrity |

Exit gate:

- The annotated archive tag resolves to exactly `49be5b640` and exists on the
  shared remote.
- ADR-0001 describes the exact cutover mechanics and has been dry-run without
  modifying shared history.
- The reference inventory and platform scope exist.
- The cutover can be performed without relying on uncommitted files or an
  undocumented local Git state.

Implementation history: [Phase 0 notes](IMPLEMENTATION_NOTES.md#phase-0--archive-and-cutover-preparation)

### Phase 1 — Clean OpenMW 0.51 baseline

Status: **Implemented**

Outcome: the active vNext source is a provenance-verified OpenMW 0.51 baseline
with reproducible desktop CI and no compiled legacy multiplayer code.

Depends on: Phase 0.

| Slice | Deliverable | Status | Completion evidence |
|---|---|---|---|
| 1.1 | Add `openmw-upstream`, fetch the official `openmw-0.51.0` tag, and verify `f4bec41444214a7903bebd178389ca22ca13f646` | **Implemented** | `openmw-upstream` uses the official URL; fetched `openmw-0.51.0` resolves to the approved commit |
| 1.2 | Perform the clean baseline-cutover commit exactly as ADR-0001 specifies | **Implemented** | Published cutover `6cdaddda60` has the exact OpenMW tree plus six reviewed `docs/vnext/**` files |
| 1.3 | Add a machine-checkable baseline provenance manifest/check | **Implemented** | [`BASELINE_PROVENANCE.json`](BASELINE_PROVENANCE.json) and `scripts/verify_vnext_baseline.py` enumerate all 26 intentional differences and 18 dependency inputs and fail on tree/dependency-input drift |
| 1.4 | Establish a documented local configure/build/test preset | **Implemented** | Clean checkout build and upstream test commands pass |
| 1.5 | Add Linux baseline CI | **Implemented** | Ubuntu 24.04 GCC 13 and Clang 18 full-build/install/test jobs passed on `8e378c2c39`; retained package/license artifacts were reviewed |
| 1.6 | Add Windows baseline CI | **Implemented** | Windows Server 2022/MSVC v143 full-build/install/test passed on `8e378c2c39`; its retained dependency/license artifact was reviewed |
| 1.7 | Add macOS baseline CI | **Implemented** | macOS 15 arm64 per-change and manually dispatched Intel Xcode 16 full-build/install/test jobs passed on `8e378c2c39`; both retained artifacts were reviewed |
| 1.8 | Prove legacy multiplayer exclusion | **Implemented** | Fail-closed proof passed on committed Windows HEAD and the owner-accepted Linux GCC/Clang, Windows MSVC, and macOS arm64 CI matrix at `2cad2dbb28`; retained artifacts include the exclusion evidence |

Exit gate:

- A clean clone builds and runs upstream tests on every supported desktop CI
  platform with pinned or reproducibly resolved dependencies.
- The baseline verification job accounts for every difference from OpenMW
  `f4bec4144`.
- No TES3MP 0.8 multiplayer target or dependency is compiled.

Implementation history: [Phase 1 notes](IMPLEMENTATION_NOTES.md#phase-1--clean-openmw-051-baseline)

### Phase 2 — Security and architecture decisions

Status: **Implemented**

Outcome: implementation begins with explicit trust boundaries, technology
choices, ownership rules, and patch policies rather than accidental coupling.

Depends on: Phase 1.

| Slice | Deliverable | Status | Completion evidence |
|---|---|---|---|
| 2.1 | Prepare the threat model and obtain owner approval for ADR-0003 | **Implemented** | Owner-approved [`ADR-0003`](adr/ADR-0003-hostile-internet-threat-model.md) records the hostile-Internet boundary, protected assets, attacker capabilities, mitigations, tests, and explicit deferred risks |
| 2.2 | Evaluate schema/codec candidates with the owner and approve ADR-0004 | **Implemented** | Owner-approved [`ADR-0004`](adr/ADR-0004-protocol-schema-codec-evolution-policy.md) selects restricted verifier-first FlatBuffers; the exact Windows/Linux/macOS/fuzzer proof and retained-artifact consistency gate pass at `da24423a1a`, and the owner accepted the completion evidence on 2026-08-26 |
| 2.3 | Evaluate transport/security candidates with the owner and approve ADR-0005 | **Implemented** | Owner-approved [`ADR-0005`](adr/ADR-0005-transport-security-authentication-resumption.md) selects standalone GameNetworkingSockets with automatic encryption, no endpoint-certificate operations, an optional shared join password, and automatic single-use resume tokens; the exact five-platform proof and retained-artifact consistency gate pass at `d5d7a1d1f4`, and the owner accepted the completion evidence on 2026-08-26 |
| 2.4 | Review authority/state-scope options by subsystem and approve ADR-0006 | **Implemented** | Owner-approved [`ADR-0006`](adr/ADR-0006-authority-state-scope-prediction-presentation.md) selects Option A for all five decisions, records the friends-server validation-depth condition, maps initial subsystems, lists domain GDR questions, and defines named acceptance scenarios |
| 2.5 | Review the OpenMW hook/patch options with the owner and approve ADR-0007 | **Implemented** | Owner-approved [`ADR-0007`](adr/ADR-0007-openmw-hook-patch-queue-policy.md) selects the native adapter, bounded hook, and machine-checked registry options and keeps possible upstream changes in a separate non-authoritative local OpenMW preparation repository unless later submission is approved |
| 2.6 | Review and approve deterministic simulation and protocol compatibility policies | **Implemented** | Owner-approved [`ADR-0013`](adr/ADR-0013-deterministic-simulation-protocol-compatibility.md) selects the 30 Hz bounded-catch-up tick, checked integer/fixed-point canonical numerics, writer-owned command ordering, versioned deterministic inputs/canonical bytes, and current-plus-previous-minor capability negotiation |

Exit gate:

- ADR-0003 through ADR-0007 and ADR-0013 are accepted at the level needed by the
  first vertical slice.
- Selected libraries build on Linux, Windows, and macOS in a minimal isolated
  proof without OpenMW coupling.
- The threat model identifies resource exhaustion, malformed input, replay,
  spoofing, stale authority, credential handling, and administrative abuse.

Implementation history: [Phase 2 notes](IMPLEMENTATION_NOTES.md#phase-2--security-and-architecture-decisions)

### Phase 3 — Independent targets and test scaffold

Status: **Implemented**

Outcome: dependency boundaries are enforced by the build, and all later work has
deterministic testing, fuzzing, sanitizers, fault injection, and observability.

Depends on: Phase 2.

| Slice | Deliverable | Status | Completion evidence |
|---|---|---|---|
| 3.1 | Add empty protocol, transport, server-core, client-session, adapter, and test-support targets | **Implemented** | Accepted [`ADR-0014`](adr/ADR-0014-phase3-target-topology-boundary-enforcement.md) fixes the six-target topology; the independent and adapter graphs build, focused tests prove forbidden direct links/includes fail closed, and the owner accepted the implementation demo on 2026-08-26 |
| 3.2 | Add strong value types for IDs, ticks, sequences, revisions, command IDs, and authority epochs | **Implemented** | Accepted [`ADR-0015`](adr/ADR-0015-strong-value-types-identity-counter-policy.md) is implemented by ten explicit semantic types and independent compile-time/runtime boundary tests; the owner accepted the implementation demo on 2026-08-26 |
| 3.3 | Add canonical `CellId`, transform, velocity, and platform-neutral command/snapshot primitives | **Implemented** | Accepted [`ADR-0016`](adr/ADR-0016-canonical-spatial-command-snapshot-primitives.md) is implemented by engine-independent spatial/metadata values and a test-support-only byte round trip; the owner accepted the implementation demo on 2026-08-26 |
| 3.4 | Add injected clock, deterministic RNG, deterministic scheduler, and in-memory link | **Implemented** | Accepted [`ADR-0017`](adr/ADR-0017-deterministic-facilities-harness-boundaries.md) is implemented by passive server-core clock/scheduler/RNG facilities and a bounded test-support link/exact-trace harness; independent long-run, vector, isolation, backpressure, and repeatability contracts pass and the owner accepted the implementation demo on 2026-08-26 |
| 3.5 | Add latency/loss/jitter/duplication/reordering/stall/disconnect fault controls | **Implemented** | Accepted [`ADR-0018`](adr/ADR-0018-deterministic-network-fault-controls.md) is implemented by a passive bounded test-support wrapper with independently configurable direction/channel profiles, isolated seeded fault streams, explicit stall/disconnect controls, and passing exact-trace contracts; the owner accepted the implementation demo on 2026-08-26 |
| 3.6 | Add ASan, UBSan, race-checking where supported, and fuzz-target CI plumbing | **Implemented** | Accepted [`ADR-0019`](adr/ADR-0019-runtime-safety-and-fuzz-ci-policy.md), the owner-accepted demo, and reviewed hosted Clang 18 ASan+UBSan/libFuzzer and ThreadSanitizer evidence all pass at `fc8f178081` |
| 3.7 | Add owned metrics/logging interfaces and test sinks | **Implemented** | Accepted [`ADR-0020`](adr/ADR-0020-owned-observability-interfaces-and-test-sinks.md), local implementation, owner demo acceptance, and reviewed hosted six-contract safety plus Linux GCC/Clang, Windows MSVC, and macOS arm64 baseline evidence pass at `e757e063c4` |

Exit gate:

- Protocol and server-core compile and test without OpenMW or the selected
  transport library.
- Forbidden dependency checks fail intentionally when an OpenMW or transport
  header is introduced into an engine-independent target.
- The deterministic harness reproduces the same trace and checksum from the same
  seed, including under injected faults.

Implementation history: [Phase 3 notes](IMPLEMENTATION_NOTES.md#phase-3--independent-targets-and-test-scaffold)

### Phase 4 — Bounded protocol and in-memory session

Status: **Implemented**

Outcome: a simulated peer negotiates vNext, authenticates through an interface,
and exchanges bounded messages entirely in memory.

Depends on: Phase 3.

| Slice | Deliverable | Status | Completion evidence |
|---|---|---|---|
| 4.1 | Implement framing/envelopes, message classification, byte budgets, and structured decode errors | **Implemented** | Accepted [`ADR-0021`](adr/ADR-0021-bounded-protocol-framing-and-decode-boundary.md), implementation `98e62f5f19`, all applicable local verification, and owner demo acceptance pass |
| 4.2 | Implement `ClientHello`, `ServerHello`, and clear rejection with version/capability negotiation | **Implemented** | Accepted [`ADR-0022`](adr/ADR-0022-version-and-capability-negotiation.md), implementation `e89621e970`, all applicable local verification, and owner demo acceptance pass |
| 4.3 | Implement the client/server session state machines and authentication-provider interface | **Implemented** | Accepted [`ADR-0023`](adr/ADR-0023-session-state-machines-and-authentication-provider-boundary.md), implementation `edbedbd632`, all applicable local verification, and owner demo acceptance pass |
| 4.4 | Define reliable-operation and latest-wins snapshot envelopes | **Implemented** | Accepted [`ADR-0024`](adr/ADR-0024-reliable-operation-latest-wins-envelope-contract.md), implementation `ee17ebdd0a`, all applicable local verification, and owner demo acceptance pass |
| 4.5 | Exchange a minimal player command and world snapshot over the in-memory link | **Implemented** | Accepted [`ADR-0025`](adr/ADR-0025-minimal-player-command-world-snapshot-exchange.md) and [`GDR-0011`](gdr/GDR-0011-phase4-minimal-player-exchange-semantics.md), implementation `077a08da48`, all applicable local verification, and owner demo acceptance pass |
| 4.6 | Add round-trip, property, golden-schema, mutation, and fuzz coverage | **Implemented** | Implementation `7361a43af7`, all applicable local verification, complete decoder/corpus registration, and owner demo acceptance pass |

Exit gate:

- A simulated client completes a handshake and bounded state exchange in memory.
- Unknown optional capabilities are ignored and unknown required capabilities
  fail clearly. Older vNext peers receive a clear incompatibility rejection
  when a valid shared frame can be decoded; TES3MP 0.8 or other non-vNext input
  is rejected locally without an incompatible wire reply.
- Malformed input cannot cause partial state commits, unbounded allocation, or
  secret-bearing log output.

Implementation history: [Phase 4 notes](IMPLEMENTATION_NOTES.md#phase-4--bounded-protocol-and-in-memory-session)

### Phase 5 — Deterministic authoritative server core

Status: **Implemented**

Outcome: one deterministic writer validates commands, owns minimal canonical
player state, publishes immutable snapshots, and emits changes to owned sinks.

Depends on: Phase 4.

| Slice | Deliverable | Status | Completion evidence |
|---|---|---|---|
| 5.1 | Implement fixed-tick scheduling, bounded per-tick command intake, and deterministic ordering | **Implemented** | Accepted [`ADR-0026`](adr/ADR-0026-phase5-writer-command-intake-and-ordering.md), implementation `e0dde571f5`, applicable local verification, and owner demo acceptance pass |
| 5.2 | Implement minimal player/session/cell/root-transform/velocity/ack canonical state | **Implemented** | Accepted [`ADR-0027`](adr/ADR-0027-phase5-canonical-player-and-session-state.md), implementation `f1a1f0632e`, applicable local verification, and owner demo acceptance pass |
| 5.3 | Implement command validation and atomic reducer application | **Implemented** | Accepted [`ADR-0028`](adr/ADR-0028-phase5-command-validation-and-atomic-reducer.md) and [`GDR-0012`](gdr/GDR-0012-phase5-minimal-motion-reducer-semantics.md), implementation `f57a4074db`, applicable local verification, and owner demo acceptance pass |
| 5.4 | Publish immutable snapshots and versioned state-change events | **Implemented** | Accepted [`ADR-0029`](adr/ADR-0029-phase5-immutable-canonical-publication-and-versioned-change-feed.md), implementation `d7e7b25950`, applicable local verification, and owner demo acceptance pass |
| 5.5 | Add idempotency windows, authority-epoch checks, state checksums, and explicit resync requests | **Implemented** | Accepted [`ADR-0030`](adr/ADR-0030-phase5-idempotency-checksum-and-resync-boundary.md), implementation `ac627deafc`, applicable local verification, and owner demo acceptance pass |
| 5.6 | Add persistence, replay, script, and metrics sink interfaces without implementations | **Implemented** | Accepted [`ADR-0031`](adr/ADR-0031-phase5-committed-domain-sink-boundary.md), implementation `2905c7a791`, applicable local verification, and owner demo acceptance pass |
| 5.7 | Add reducer property tests and deterministic multi-client simulation tests | **Implemented** | Implementation `917ecc3278`, applicable local verification, and owner demo acceptance pass |

Exit gate:

- The server core has a single canonical mutation path and no renderer, engine,
  platform, socket, script-runtime, or database dependency.
- State commits are atomic, deterministic, revisioned, and observable.
- A slow or failed auxiliary consumer has an explicit bounded failure policy and
  cannot silently mutate or deadlock canonical state.

Implementation history: [Phase 5 notes](IMPLEMENTATION_NOTES.md#phase-5--deterministic-authoritative-server-core)

### Phase 6 — Maintained transport and secure network session

Status: **Implemented**

Outcome: the in-memory session runs over a maintained encrypted real transport
with application session identity, channel semantics, backpressure, and
telemetry hidden behind owned APIs.

Depends on: Phase 5.

| Slice | Deliverable | Status | Completion evidence |
|---|---|---|---|
| 6.1 | Wrap connection/listen/connect/disconnect/cancellation/lifecycle in an owned transport interface | **Implemented** | Accepted and implemented [`ADR-0032`](adr/ADR-0032-phase6-transport-adapter-and-lifecycle-boundary.md); owned APIs, verified provisioning, deterministic resolver/race/generation coverage, and numeric/DNS encrypted loopback pass locally; exact candidate `36c1ac6617d75c8d8ef88d687901c9d73b25d0a0` passes all six jobs in [`33215346506`](https://github.com/poisson-fish/TES3MP/actions/runs/33215346506), with all retained artifacts verified and consistent; owner closure approval recorded |
| 6.2 | Map reliable operations and latest-wins snapshots to explicit transport channels | **Implemented** | Owner-approved [`ADR-0033`](adr/ADR-0033-phase6-transport-channel-and-delivery-semantics.md), implementation `2f022fe4ad`, applicable local verification, and owner implementation-demo acceptance pass |
| 6.3 | Implement required encryption, optional join-password authentication, resume-token handling, and credential redaction per ADR-0005 | **Implemented** | Owner-approved and implemented [`ADR-0034`](adr/ADR-0034-phase6-credential-and-resumption-boundary.md), [`ADR-0035`](adr/ADR-0035-phase6-transport-admission-scope-handoff-and-derivation.md), and [`ADR-0036`](adr/ADR-0036-phase6-authentication-composition-and-session-finalization.md); bounded credentials, shared one-gate join/resume routing, state-machine resume installation, fail-closed bind-then-issue initial finalization, public credential/source redaction contracts, and a real encrypted protected-join/single-use-resume loopback pass applicable local checks; owner implementation-demo acceptance recorded on 2026-08-31 |
| 6.4 | Implement bounded queues, priority, rate limits, backpressure, and slow-peer eviction | **Implemented** | Owner-approved [`ADR-0037`](adr/ADR-0037-phase6-bounded-outbound-queue-and-slow-peer-policy.md), implementation `da8d139ce3`, focused queue/transport/authentication and repository verification, and owner implementation-demo acceptance pass |
| 6.5 | Implement network telemetry and stable disconnect/rejection reasons | **Implemented** | Owner-approved [`ADR-0038`](adr/ADR-0038-phase6-network-telemetry-and-stable-reasons.md), implementation `b4c4201394`, focused local verification, and owner implementation-demo acceptance pass |
| 6.6 | Integrate real sockets into the deterministic fault harness | **Implemented** | Owner-approved [`ADR-0039`](adr/ADR-0039-phase6-deterministic-real-transport-fault-boundary.md), implementation `9ab4b6e9ed`, applicable local verification, and owner implementation-demo acceptance pass |
| 6.7 | Prove Linux, Windows, and macOS build/test support | **Implemented** | Exact candidate `731bdec782` passes all six transport jobs and both runtime-safety jobs in hosted runs `33553049641` and `33553053380`; owner exit approval recorded on 2026-09-01 |

Exit gate:

- A real client and server complete the Phase 4 exchange over the approved
  encrypted transport on all supported desktop platforms.
- Snapshot and reliable-operation delivery behave according to their distinct
  semantics under congestion and loss.
- Queues and per-peer work remain bounded under malformed, flooding, stalled, and
  disconnecting peers.

Implementation history: [Phase 6 notes](IMPLEMENTATION_NOTES.md#phase-6--maintained-transport-and-secure-network-session)

### Phase 7 — Headless end-to-end multiplayer slice

Status: **In Progress**

Outcome: a dedicated server and two fake clients complete connect, join, cell,
movement, observation, disconnect, and resume without OpenMW.

Depends on: Phase 6.

| Slice | Deliverable | Status | Completion evidence |
|---|---|---|---|
| 7.0 | Review and obtain owner approval for GDR-0001 vertical-slice behavior scenarios | **Implemented** | Accepted [`GDR-0001`](gdr/GDR-0001-phase7-headless-vertical-slice-behavior.md) records approved A/A/A/A/A/A authority, scope, contention, reconnect, and named demo/test scenarios |
| 7.1 | Add the dedicated-server composition root and new minimal configuration format | **Implemented** | Accepted [`ADR-0040`](adr/ADR-0040-phase7-server-composition-and-configuration.md), implementation `d85d3d07f4`, applicable local verification, real loopback lifecycle evidence, and owner demo acceptance pass |
| 7.2 | Add a reusable headless client-session library and scripted fake-client driver | **Implemented** | Accepted [`ADR-0041`](adr/ADR-0041-phase7-headless-client-and-script-driver.md), implementation `a4e983f5f0`, applicable local verification, and owner demo acceptance pass |
| 7.3 | Implement authentication, session creation, player creation, and join | **In Progress** | Accepted [`ADR-0042`](adr/ADR-0042-phase7-authenticated-join-and-identity-allocation.md); bounded prepare/token/frame/atomic-enqueue/commit composition and focused failure atomicity tests pass; real transport wiring and two-client process demo remain |
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

Implementation history: [Phase 7 notes](IMPLEMENTATION_NOTES.md#phase-7--headless-end-to-end-multiplayer-slice)

### Phase 8 — OpenMW desktop vertical slice

Status: **Not Started**

Outcome: a thin OpenMW 0.51 adapter completes the headless slice with two real
desktop clients while keeping engine-specific behavior isolated.

Depends on: Phase 7.

| Slice | Deliverable | Status | Completion evidence |
|---|---|---|---|
| 8.1 | Inventory the minimum required OpenMW hooks and review the final patch surface with the owner | **Not Started** | Owner-approved hook document maps each patch to an adapter need and upstream strategy |
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

Implementation history: [Phase 8 notes](IMPLEMENTATION_NOTES.md#phase-8--openmw-desktop-vertical-slice)

### Phase 9 — PC VR interoperability gate

Status: **Not Started**

Outcome: desktop OpenMW and the maintained PC OpenMW-VR target interoperate using
one protocol, one client session, and optional sampled pose presentation.

Depends on: Phase 8.

| Slice | Deliverable | Status | Completion evidence |
|---|---|---|---|
| 9.1 | Review PC VR maintenance options with the owner, approve ADR-0008, and create the worktree/patch target | **Not Started** | Owner-approved fork revision, rebase/update procedure, and patch ownership are recorded |
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

Implementation history: [Phase 9 notes](IMPLEMENTATION_NOTES.md#phase-9--pc-vr-interoperability-gate)

### Phase 10 — Player lifecycle and content identity

Status: **Not Started**

Outcome: players join only with compatible content identity, have a complete
canonical lifecycle, and can be acted on by later moderation/admin interfaces.

Depends on: Phase 9.

| Slice | Deliverable | Status | Completion evidence |
|---|---|---|---|
| 10.0 | Review and obtain owner approval for GDR-0002 player/content/lifecycle semantics | **Not Started** | Approved scenarios and named demo/tests cover identity, mismatch, replacement, moderation, and reconnect |
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

Implementation history: [Phase 10 notes](IMPLEMENTATION_NOTES.md#phase-10--player-lifecycle-and-content-identity)

### Phase 11 — Canonical cells, interest, and resynchronization

Status: **Not Started**

Outcome: the server owns cell membership and interest, produces consistent
initial snapshots, and repairs divergence without reconnecting the whole server.

Depends on: Phase 10.

| Slice | Deliverable | Status | Completion evidence |
|---|---|---|---|
| 11.0 | Review and obtain owner approval for GDR-0003 cell/interest/resync semantics | **Not Started** | Approved scenarios and named demo/tests cover transitions, visibility, late join, churn, and resync |
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

Implementation history: [Phase 11 notes](IMPLEMENTATION_NOTES.md#phase-11--canonical-cells-interest-and-resynchronization)

### Phase 12 — Production movement, animation, and pose replication

Status: **Not Started**

Outcome: the Phase 8/9 movement prototype becomes a production, observable,
server-validated system with animation and VR presentation state.

Depends on: Phase 11.

| Slice | Deliverable | Status | Completion evidence |
|---|---|---|---|
| 12.0 | Review and obtain owner approval for GDR-0004 movement/animation/VR behavior | **Not Started** | Approved scenarios and named demo/tests cover validation, correction, teleport, animation, and poses |
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

Implementation history: [Phase 12 notes](IMPLEMENTATION_NOTES.md#phase-12--production-movement-animation-and-pose-replication)

### Phase 13 — Actor lifecycle, AI state, and authority handoff

Status: **Not Started**

Outcome: non-player actors have stable lifecycle/state, deterministic ownership,
and safe lease-based authority transfer where delegation is justified.

Depends on: Phase 12.

| Slice | Deliverable | Status | Completion evidence |
|---|---|---|---|
| 13.0 | Review and obtain owner approval for GDR-0005 actor/AI/authority behavior | **Not Started** | Approved scenarios and named demo/tests cover lifecycle, simulation ownership, delegation, handoff, and failure |
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

Implementation history: [Phase 13 notes](IMPLEMENTATION_NOTES.md#phase-13--actor-lifecycle-ai-state-and-authority-handoff)

### Phase 14 — Interactive objects, locks, traps, and doors

Status: **Not Started**

Outcome: world-object interactions are authoritative, revisioned, contention-safe,
and visible consistently to interested clients.

Depends on: Phase 13.

| Slice | Deliverable | Status | Completion evidence |
|---|---|---|---|
| 14.0 | Review and obtain owner approval for GDR-0006 object/lock/trap/door state scope | **Not Started** | Approved scenarios explicitly decide global/per-player/group/instance behavior, contention, reset, persistence, and visibility |
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

Implementation history: [Phase 14 notes](IMPLEMENTATION_NOTES.md#phase-14--interactive-objects-locks-traps-and-doors)

### Phase 15 — Inventory, equipment, and container transactions

Status: **Not Started**

Outcome: item ownership and equipment changes are atomic, idempotent, revisioned,
and safe under simultaneous access and reconnect.

Depends on: Phase 14.

| Slice | Deliverable | Status | Completion evidence |
|---|---|---|---|
| 15.0 | Review and obtain owner approval for GDR-0007 inventory/loot/container semantics | **Not Started** | Approved scenarios and named demo/tests cover ownership, loot scope, privacy, contention, equipment, and reconnect |
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

Implementation history: [Phase 15 notes](IMPLEMENTATION_NOTES.md#phase-15--inventory-equipment-and-container-transactions)

### Phase 16 — Combat, stats, magic, death, and resurrection

Status: **Not Started**

Outcome: combat consequences are validated and server-owned, including stats,
effects, projectiles, death, and resurrection.

Depends on: Phase 15.

| Slice | Deliverable | Status | Completion evidence |
|---|---|---|---|
| 16.0 | Review and obtain owner approval for GDR-0008 combat and consequence semantics | **Not Started** | Approved scenarios and named demo/tests cover lag handling, hit resolution, effects, projectiles, death, and resurrection |
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

Implementation history: [Phase 16 notes](IMPLEMENTATION_NOTES.md#phase-16--combat-stats-magic-death-and-resurrection)

### Phase 17 — Dialogue, journals, factions, and quests

Status: **Not Started**

Outcome: narrative progression has explicit per-player/shared ownership,
revisioned transitions, and deterministic multiplayer contention behavior.

Depends on: Phase 16.

| Slice | Deliverable | Status | Completion evidence |
|---|---|---|---|
| 17.0 | Review and obtain owner approval for GDR-0009 narrative state scope | **Not Started** | Approved scenarios explicitly classify dialogue, journal, faction, and quest state as per-player/shared/group/instance |
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

Implementation history: [Phase 17 notes](IMPLEMENTATION_NOTES.md#phase-17--dialogue-journals-factions-and-quests)

### Phase 18 — Time, weather, and durable world state

Status: **Not Started**

Outcome: shared world clocks, weather, globals, and other durable world values
have explicit ownership, evolution, interest, and presentation behavior.

Depends on: Phase 17.

| Slice | Deliverable | Status | Completion evidence |
|---|---|---|---|
| 18.0 | Review and obtain owner approval for GDR-0010 time/weather/world-state semantics | **Not Started** | Approved scenarios and named demo/tests cover scope, evolution, reset, offline time, reconnect, and persistence |
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

Implementation history: [Phase 18 notes](IMPLEMENTATION_NOTES.md#phase-18--time-weather-and-durable-world-state)

### Phase 19 — Versioned server scripting

Status: **Not Started**

Outcome: pinned server scripts receive immutable typed events and submit explicit
commands through the same validation path as every other authority.

Depends on: Phase 18.

| Slice | Deliverable | Status | Completion evidence |
|---|---|---|---|
| 19.1 | Review scripting options with the owner, approve ADR-0009, and pin the language/runtime/dependency | **Not Started** | Owner-approved isolation, determinism, limits, debugging, license, and support policy are recorded |
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

Implementation history: [Phase 19 notes](IMPLEMENTATION_NOTES.md#phase-19--versioned-server-scripting)

### Phase 20 — Transactional persistence and deterministic replay

Status: **Not Started**

Outcome: canonical state survives restart transactionally, evolves through
versioned schemas, can be backed up/restored, and is reproducible from command
and event records.

Depends on: Phase 19.

| Slice | Deliverable | Status | Completion evidence |
|---|---|---|---|
| 20.1 | Review persistence/replay options with the owner, approve ADR-0010, and implement the adapter boundary | **Not Started** | Owner approval is recorded and store/runtime types do not leak into server-core domain APIs |
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

Implementation history: [Phase 20 notes](IMPLEMENTATION_NOTES.md#phase-20--transactional-persistence-and-deterministic-replay)

### Phase 21 — Administration, moderation, and discovery

Status: **Not Started**

Outcome: operators can configure, observe, moderate, administer, and advertise a
server through authenticated, auditable, versioned interfaces.

Depends on: Phase 20.

| Slice | Deliverable | Status | Completion evidence |
|---|---|---|---|
| 21.0 | Review and obtain owner approval for ADR-0011 administration, audit, moderation, and discovery policy | **Not Started** | Roles, authorization boundaries, audit visibility, public/private exposure, and abuse scenarios are approved |
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

Implementation history: [Phase 21 notes](IMPLEMENTATION_NOTES.md#phase-21--administration-moderation-and-discovery)

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

Implementation history: [Phase 22 notes](IMPLEMENTATION_NOTES.md#phase-22--desktop-and-pc-vr-stabilization-and-release)

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
| 23.6 | Review evidence with the owner and accept ADR-0012 with an explicit go/no-go result and support scope | **Not Started** | Owner-approved decision, selected route, risks, upstream candidates, and Phase 24 activation are recorded |

Exit gate:

- An on-device prototype resolves the major toolchain, OpenXR, rendering,
  lifecycle, networking, storage, and performance unknowns.
- ADR-0012 makes an evidence-backed, owner-approved go/no-go decision. A go decision activates
  Phase 24; a no-go decision leaves Phase 24 **Not Started** and records the
  conditions that would justify reconsideration.
- Either result completes the feasibility phase without weakening Phase 22
  release criteria.

Implementation history: [Phase 23 notes](IMPLEMENTATION_NOTES.md#phase-23--meta-quest-3-feasibility-decision)

### Phase 24 — Meta Quest 3 multiplayer port

Status: **Not Started**

Outcome: if ADR-0012 records a go decision, a supportable Quest 3 client completes
the existing multiplayer slice and meets measured on-device budgets.

Depends on: Phase 23 with an ADR-0012 go decision.

| Slice | Deliverable | Status | Completion evidence |
|---|---|---|---|
| 24.1 | Productionize the Android ARM64/OpenXR build, packaging, signing, and dependency pipeline | **Not Started** | Reproducible APK artifacts and clean-machine build instructions pass |
| 24.2 | Implement Android lifecycle, suspend/resume, audio, networking, scoped storage, and legal data import | **Not Started** | On-device lifecycle/platform compliance suite passes |
| 24.3 | Compose the existing client session, adapter boundary, semantic commands, and `vr_pose` capability | **Not Started** | No Quest-specific protocol, durable state, or server branch is introduced |
| 24.4 | Complete Quest controller, locomotion, UI, pose presentation, and desktop fallback behavior | **Not Started** | Quest/desktop interaction behavior satisfies the existing VR authority rules |
| 24.5 | Pass Quest/desktop/PC VR interoperability and suspend/reconnect tests | **Not Started** | Quest completes connect/join/cell/movement/reconnect with both peer types |
| 24.6 | Pass loading, frame-time, memory, thermal, battery, and hardware soak budgets | **Not Started** | Hardware-runner evidence meets the ADR-0012 support policy |
| 24.7 | Package, document, and execute the Quest release-candidate matrix | **Not Started** | Installation, data import, permissions, update, recovery, and support docs are verified |

Exit gate:

- Quest 3 completes the same vNext vertical slice and interoperates with desktop
  and PC VR without platform-specific protocol or server logic.
- Suspend/resume safely resumes or performs an explicit clean reconnect.
- Real-hardware soak stays within declared performance, thermal, memory, and
  battery budgets.

Implementation history: [Phase 24 notes](IMPLEMENTATION_NOTES.md#phase-24--meta-quest-3-multiplayer-port)

## Phase update procedure

Every pull request that advances a slice must update this document in the same
change or immediately linked follow-up:

1. Hold the phase kickoff and present required ADR/GDR decision packets before
   dependent production implementation begins. Record explicit owner approval;
   do not infer it from silence or from approval of unrelated work.
2. Change the slice to **In Progress** when the first non-disposable artifact
   lands, and change the phase to **In Progress** if needed.
3. Append an implementation note to [`IMPLEMENTATION_NOTES.md`](IMPLEMENTATION_NOTES.md)
   with the date, commit/PR, important design details, deviations from this
   plan, and exact verification evidence.
4. Run the approved automated scenarios and demonstrate user-visible or
   architecturally significant behavior to the owner. Record feedback and any
   required correction.
5. Change a slice to **Implemented** only when its completion evidence and
   required owner acceptance exist.
6. Run the entire phase exit gate after all slices are implemented. Review the
   gate evidence with the owner, record approval to proceed, then change the
   phase and program tracker row to **Implemented**.
7. Add newly discovered work as a bounded slice in the correct phase. Do not hide
   required work in an implementation note or mark a phase complete with a known
   unmet gate.

Use this note format in `IMPLEMENTATION_NOTES.md` under the affected phase:

```markdown
- YYYY-MM-DD — Slice N.N — Status
  - Change: commit/PR and concise implementation description.
  - Decisions: important choices or approved deviation from the plan/ADR.
  - Verification: exact local commands and CI job/artifact links.
  - Owner review: decision/demo/exit-gate approval reference and resulting feedback.
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
