# TES3MP vNext implementation plan

Document type: living implementation plan

Updated: 2026-08-26

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
| 3 | Independent targets and test scaffold | **In Progress** | Phase 2 |
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
13. Architecture and gameplay semantics are approved before their production
    schema, state model, or API lands. A convenient data layout must not decide
    product behavior by accident.
14. Tests must include multi-client scenarios that prove state scope and
    visibility, not merely that one client sees a successful interaction.
15. An implementation demo and owner review are completion evidence for
    user-visible or architecturally significant slices; passing tests alone is
    necessary but not sufficient.

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

Implementation notes:

- The direction document, permanent legacy archive tag, and reference-only
  legacy gameplay inventory are the completed slices.
- The existing `tes3mp-0.8.1` tag is a lightweight tag at `68954091c` and must
  not be moved. ADR-0001 must choose a new unambiguous archive-tag name.
- Branch `0.8.1` currently provides the intended `49be5b640` source reference,
  but a branch alone is not the permanent archive artifact required by the plan.
- No cutover command should be run until the dry-run result preserves the vNext
  documentation and produces an OpenMW 0.51 tree without a textual merge.
- 2026-08-25 — Slice 0.2 — Implemented
  - Change: created and pushed the permanent annotated tag
    `tes3mp-0.8.1-archive`; tag object
    `1f3bc4c651573a60b4326b5d4703b6fad4b7fccf` archives commit
    `49be5b6405d6ab427e06ed350cf76c715a1f3bdd`.
  - Decisions: project owner approved the recommended
    `tes3mp-0.8.1-archive` name on 2026-08-25. This approval does not select or
    authorize the baseline-cutover mechanics governed by ADR-0001.
  - Verification: `git cat-file -t tes3mp-0.8.1-archive` returned `tag`;
    `git rev-parse tes3mp-0.8.1-archive^{}` returned `49be5b6405d6ab427e06ed350cf76c715a1f3bdd`;
    and `git ls-remote --tags origin 'refs/tags/tes3mp-0.8.1-archive' 'refs/tags/tes3mp-0.8.1-archive^{}'`
    returned the tag object and the same peeled commit. `tes3mp-0.8.1` remains a
    lightweight tag at `68954091c54d0596037c4fb54d2812313b7582a1`.
  - Owner review: explicit tag-name approval received in the 2026-08-25 working
    session; published name and target match the approved option.
  - Follow-ups: Slice 0.5 still requires the complete ADR-0001 decision packet,
    owner approval, and disposable cutover rehearsal before any cutover command.
- 2026-08-25 — Slice 0.3 — Implemented
  - Change: this commit adds
    `docs/vnext/LEGACY_GAMEPLAY_FEATURE_INVENTORY.md`, a reference-only inventory
    grouped by user-visible gameplay and operational domains.
  - Decisions: none; the inventory explicitly rejects compatibility, authority,
    state-scope, architecture, and gameplay-semantics implications.
  - Verification: `git diff --check`; `test -e` over every source named in the
    inventory's repository-evidence section; and `rg -q` checks for all required
    inventory sections. No production, build, or test target changed.
  - Owner review: not required for completion because this historical
    documentation slice approves no architecture or behavior; included in the
    current working-tree handoff for review.
  - Follow-ups: missing behavior defined only by external CoreScripts, wiki, or
    OpenMW-VR sources must be researched in the relevant future ADR/GDR slice.
- 2026-08-25 — Slice 0.4 — In Progress
  - Change: added proposed
    [`ADR-0002`](adr/ADR-0002-platform-toolchain-policy.md) with desktop, PC VR,
    Quest, CI/toolchain, and dependency-policy options, scenarios, tradeoffs,
    recommendations, failure modes, review triggers, and acceptance evidence.
  - Decisions: none accepted. The decision packet recommends a bounded desktop
    matrix, Windows-first PC VR, deferred Quest production, and upstream-aligned
    GitHub Actions/toolchain/dependency policy; every recommendation remains
    subject to explicit project-owner approval.
  - Verification: compared the repository baseline with the pinned OpenMW 0.51
    CMake, CI, and dependency-version files; verified the official tag with
    `git ls-remote`; reviewed the OpenMW-VR versioning policy and the current
    GitHub-hosted runner catalog; then ran `git diff --check` and document-link
    checks recorded in the working-session handoff.
  - Owner review: decision packet presented on 2026-08-25; approval pending.
    Slice 0.4 remains **In Progress** and no dependent production work is
    authorized.
  - Follow-ups: record the owner's selected options and conditions in ADR-0002,
    then mark the ADR and slice **Implemented**. Phase 1 owns the desktop proof
    matrix, Phase 9 owns PC VR proof, and Phase 23 owns Quest feasibility
    evidence under the approved policy.
- 2026-08-25 — Slice 0.4 — Implemented
  - Change: accepted
    [`ADR-0002`](adr/ADR-0002-platform-toolchain-policy.md) after owner review
    and recorded the selected desktop, PC VR, Quest, CI/toolchain, and dependency
    policies.
  - Decisions: owner approved Option A for Decisions 1 through 4. macOS arm64
    and x86-64 desktop remain supported; Windows x86-64 is the required initial
    PC VR platform; Linux PC VR remains evidence-driven best effort; macOS PC VR
    is outside the initial release scope because it is currently nonfunctional;
    Quest production remains deferred; and the upstream-aligned GitHub Actions
    policy is accepted.
  - Verification: `git diff --check`; required ADR section/status checks; local
    link checks; and comparison of the accepted text with the four presented
    options and the owner's clarification.
  - Owner review: explicit approval received in the 2026-08-25 working session:
    “all A for all four,” with the macOS VR clarification recorded above.
  - Follow-ups: Phase 1 owns desktop CI/build proof, Phase 9 owns PC VR support
    evidence and any Linux promotion, and a future macOS PC VR expansion must
    reopen ADR-0002. Phase 23 owns Quest feasibility.
- 2026-08-25 — Slice 0.5 — In Progress
  - Change: added proposed
    [`ADR-0001`](adr/ADR-0001-baseline-cutover-git-mechanics.md) with cutover
    graph/tree options, recommended exact-tree mechanics, preserved-path scope,
    failure handling, verification, publication, and rollback policy.
  - Decisions: none accepted. The packet recommends a two-parent cutover whose
    tree is exact OpenMW 0.51 plus `docs/vnext/**`. ADR approval would authorize
    only the disposable production-form rehearsal, not the real cutover.
  - Verification: fetched the official tag into a disposable local clone and
    verified `f4bec41444214a7903bebd178389ca22ca13f646`; constructed synthetic
    one- and two-parent candidates without updating a shared ref; verified the
    recommended candidate's parent order, both-parent ancestry, OpenMW-only tree
    outside the three then-committed `docs/vnext` files, byte-identical preserved
    docs, absence of checked legacy paths, and exact-tree new-commit rollback.
  - Owner review: decision packet presented on 2026-08-25; approval pending.
    No production-form rehearsal or active-branch cutover is authorized.
  - Follow-ups: after approval, run and record the production-form
    merge/read-tree/restore rehearsal from the final committed inputs. Slice 0.6
    and Phase 0 exit-gate approval remain required before the real cutover.
- 2026-08-25 — Slice 0.5 — Implemented
  - Change: accepted
    [`ADR-0001`](adr/ADR-0001-baseline-cutover-git-mechanics.md) after owner
    approval and completed the approved merge/read-tree/restore rehearsal in an
    isolated clone and worktrees without changing the active repository.
  - Decisions: owner approved Option 1: a two-parent cutover with the final
    pre-cutover `vnext` commit first, OpenMW
    `f4bec41444214a7903bebd178389ca22ca13f646` second, and a tree equal to OpenMW
    plus only `docs/vnext/**`. Publication is fast-forward-only; published
    rollback is a new commit, never rewritten history.
  - Verification: disposable input `45fa537004aa19bef4b35b9c556351f8327bf75a`;
    cutover `e042db240fe2f109c47bedf0640e4724b7f6ea63`; tree
    `85d3390ca15a169e04e88d746b1a1d67de4c7a1b`; rollback
    `addf2c63ff64af3a12f09f71863ff1973f8601ac`. Exact parent order and ancestry,
    upstream identity outside five `docs/vnext` files, preserved-doc identity,
    legacy-path absence, clean worktrees, `git diff --check`, `git fsck
    --no-dangling`, merge-abort restoration, and exact-tree new-commit rollback
    all passed.
  - Owner review: Option 1 and its mechanics explicitly approved in the
    2026-08-25 working session. This approval covered the rehearsal only; it did
    not authorize the real cutover.
  - Follow-ups: Slice 0.6 must capture final pre-cutover provenance. Then repeat
    the approved input/tree checks against the clean published pre-cutover commit
    and hold the Phase 0 exit-gate review before any real cutover.
- 2026-08-25 — Slice 0.6 — Implemented
  - Change: commit `1dc1d5bd00519efa5b92a917c46f9407e9e28257`
    added and published
    [`PRE_CUTOVER_PROVENANCE.md`](PRE_CUTOVER_PROVENANCE.md), recording the clean
    published capture base, local and shared branch/tag refs, remote
    configuration, submodule declaration and initialization state, commit/tree
    IDs, vNext first-parent lineage, legacy-tree classification, and the final
    pre-cutover preflight.
  - Decisions: none. This is a read-only capture of existing repository state and
    does not authorize the real cutover, configure `openmw-upstream`, or mutate
    any branch, tag, submodule, or shared ref. The capture also records that the
    earlier lightweight `tes3mp-0.8.1` tag was no longer advertised locally or
    by `origin`; the required annotated `tes3mp-0.8.1-archive` tag remains intact.
  - Verification: before editing, `git status --porcelain=v1
    --untracked-files=all` and `git diff --check` produced no output;
    `vnext` and `origin/vnext` both resolved to
    `beb2f86cdf7d810f8eebf0207e266fb62c6e8fda`; `git fsck --no-dangling`
    passed; `git cat-file -t tes3mp-0.8.1-archive` returned `tag`; local and
    remote peel checks returned
    `49be5b6405d6ab427e06ed350cf76c715a1f3bdd`; `git submodule status` and
    `git ls-tree HEAD extern/breakpad` agreed on
    `e6d1c032baa222d8a8dc87813e9067199ec0266d`; and a read-only
    `git ls-remote https://gitlab.com/OpenMW/openmw.git` query resolved
    `openmw-0.51.0` to `f4bec41444214a7903bebd178389ca22ca13f646`.
    After editing, `git diff --check` passed; all local Markdown links under
    `docs/vnext` resolved; required provenance headings and every recorded local
    commit/tree/tag object ID were checked; and the changed-path check found only
    `docs/vnext/IMPLEMENTATION_PLAN.md` and
    `docs/vnext/PRE_CUTOVER_PROVENANCE.md`.
    The post-push preflight found a clean worktree; `HEAD`, local `vnext`,
    `origin/vnext`, and the direct remote query all resolved to the published
    commit; its tree was `994268dd4e8917a8337d1045aea57efa6728c0ef`;
    archive-tag and submodule checks remained unchanged; and `git diff --check`
    plus `git fsck --no-dangling` passed.
  - Owner review: no architecture or behavior decision is introduced, so no
    decision approval was required for the capture. The separate Phase 0
    exit-gate review remains pending.
  - Follow-ups: after this status-only documentation update is published, repeat
    the final clean/local/tracking/remote identity check, acknowledge the observed
    absence of the old lightweight tag during the exit-gate review, and obtain
    explicit project-owner approval before Phase 1 or any real cutover action.
- 2026-08-25 — Phase 0 exit gate — Implemented
  - Change: the project owner reviewed the completed Slice 0.1–0.6 evidence and
    approved advancing to Phase 1, including authorization to execute and
    publish the real ADR-0001 cutover.
  - Decisions: no ADR was changed. The review accepted the documented absence
    of the old lightweight `tes3mp-0.8.1` tag because the required permanent
    annotated `tes3mp-0.8.1-archive` tag remains published and correct.
  - Verification: before the review, `HEAD`, local `vnext`, `origin/vnext`, and
    a direct `git ls-remote --heads origin refs/heads/vnext` query all resolved
    to `e68728ab6a03559e26564f683fbb4007c0e9c727`; the worktree was clean;
    `git diff --check` and `git fsck --no-dangling` produced no output; the
    archive tag was an annotated tag locally and remotely and peeled to
    `49be5b6405d6ab427e06ed350cf76c715a1f3bdd`; and the official OpenMW tag
    query resolved to `f4bec41444214a7903bebd178389ca22ca13f646`.
  - Owner review: explicit Phase 0 exit-gate approval and real-cutover
    authorization received in the 2026-08-25 working session.
  - Follow-ups: execute Phase 1 Slices 1.1 and 1.2 exactly as ADR-0001 specifies.

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

Implementation notes:

- Preserve `docs/vnext/` and the minimum required repository metadata during the
  cutover as explicit vNext-owned differences.
- Baseline fixes required merely to build on supported runners should be isolated
  and documented; they must not become a place to reintroduce legacy multiplayer
  changes.
- Record CI images, compilers, CMake version, dependency source, and cache keys in
  ADR-0002 so a green baseline can be reproduced outside CI.
- 2026-08-25 — Slice 1.1 — Implemented
  - Change: configured `openmw-upstream` with
    `https://gitlab.com/OpenMW/openmw.git` and fetched the official
    `openmw-0.51.0` tag and its reachable history.
  - Decisions: none; the remote URL and pinned commit were already accepted by
    ADR-0001.
  - Verification: `git config --get remote.openmw-upstream.url` returned the
    official URL; `git rev-parse openmw-0.51.0` returned
    `f4bec41444214a7903bebd178389ca22ca13f646`; and `git cat-file -t
    openmw-0.51.0` returned `commit`.
  - Owner review: Phase 0 exit-gate approval authorized this Phase 1 slice in the
    2026-08-25 working session; no new architecture or behavior decision arose.
  - Follow-ups: Slice 1.2 performs the approved cutover from the final published
    pre-cutover documentation commit.
- 2026-08-25 — Slice 1.2 — In Progress
  - Change: began the production cutover using the accepted ADR-0001 exact-tree,
    two-parent procedure; no cutover ref has been created or published yet.
  - Decisions: none; the owner explicitly authorized the real cutover during the
    Phase 0 exit-gate review.
  - Verification: pending the final documentation commit, repeated preflight,
    disposable-worktree tree/ancestry checks, and publication verification.
  - Owner review: real-cutover authorization received in the 2026-08-25 working
    session.
  - Follow-ups: publish this final pre-cutover documentation state, repeat all
    ADR-0001 preconditions, construct and verify the cutover, then record its
    exact commit/tree evidence before marking Slice 1.2 **Implemented**.
- 2026-08-25 — Slice 1.2 — Implemented
  - Change: published the ADR-0001 two-parent exact-tree cutover as
    `6cdaddda60e9e1f02cc6b5029dd2b589a9f9d11b`, with tree
    `0824acb059d88ef65be3a7277f6e48a3f9eab04d`. Its first parent is the final
    published pre-cutover documentation commit
    `85d92afed0e454c2599cf9b4c0b11788af3e9007`, and its second parent is OpenMW
    `f4bec41444214a7903bebd178389ca22ca13f646`.
  - Decisions: none; the production cutover used the owner-approved ADR-0001
    parent order, exact upstream tree, `docs/vnext/**` overlay, fail-closed
    checks, fast-forward-only publication, and no-history-rewrite policy.
  - Verification: before preparation, the worktree was clean; local, tracking,
    and directly queried remote `vnext` refs matched the final pre-cutover
    commit; the upstream URL and commit matched ADR-0001; the archive tag was
    annotated locally and remotely and peeled to the approved legacy commit;
    `git diff --check` and `git fsck --no-dangling` passed; and the preparation
    branch name was unused. Before publication, the cutover had exactly the two
    approved parents in order and both were ancestors; `git diff --quiet`
    proved byte/mode/tree identity with OpenMW outside `docs/vnext/**` and with
    the pre-cutover parent inside `docs/vnext/**`; the only six upstream
    differences were the six reviewed vNext documents; all explicit legacy
    path and `RakNet|CrabNet|CoreScripts` component checks were empty; local
    Markdown links, `git diff --check`, clean-worktree, and `git fsck
    --no-dangling` checks passed; and the concurrent-advance check passed.
    After publication, `HEAD`, local `vnext`, `origin/vnext`, and a direct
    remote query all resolved to the cutover commit, with a clean worktree. The
    disposable worktree and merged preparation branch were then removed.
  - Owner review: the project owner explicitly authorized the real cutover in
    the 2026-08-25 Phase 0 exit-gate review; observed production evidence
    matched the approved ADR scenarios with no deviation.
  - Follow-ups: Slice 1.3 adds the machine-checkable baseline provenance
    manifest/check. Build and upstream-test proof remains owned by Slices
    1.4–1.7, and compiled legacy exclusion proof remains Slice 1.8.
- 2026-08-25 — Slice 1.3 — Implemented
  - Change: this commit adds the machine-readable
    [`BASELINE_PROVENANCE.json`](BASELINE_PROVENANCE.json), the cross-platform
    `scripts/verify_vnext_baseline.py` checker, and isolated Git-fixture tests.
    The manifest records the pinned OpenMW commit/tree, approved cutover graph,
    every intentional path difference, and SHA-256 identities for the baseline
    CMake and platform dependency-declaration inputs.
  - Decisions: none; this mechanical control implements ADR-0001 and ADR-0002
    without changing their approved baseline, preserved-path, platform, or
    dependency policies. Inherited runner-resolved and floating dependency
    inputs are recorded as Phase 1 follow-ups, not silently approved as
    reproducible.
  - Verification: `python -m unittest
    scripts.tests.test_verify_vnext_baseline -v` passed six success/failure
    scenarios; `python -m py_compile scripts/verify_vnext_baseline.py
    scripts/tests/test_verify_vnext_baseline.py` passed; staged-tree
    `python scripts/verify_vnext_baseline.py --index` enumerated exactly nine
    intentional additions and verified ten dependency-declaration file hashes;
    `git diff --check` and `git fsck --no-dangling` passed.
  - Owner review: no new architecture, authority, state-scope, security,
    gameplay, or user-visible behavior decision was introduced; no separate
    decision approval or implementation demo is required for this mechanical
    provenance slice.
  - Follow-ups: Slice 1.4 establishes the local preset and reproducibly resolved
    dependency evidence; Slices 1.5–1.7 integrate the checker and archive exact
    platform dependency/license manifests; Slice 1.8 proves compiled legacy
    exclusion.
- 2026-08-25 — Slice 1.4 — In Progress
  - Change: this commit adds versioned cross-platform CMake configure/build
    presets, `scripts/run_vnext_baseline.py`, a commit- and hash-pinned Windows
    dependency lock/provisioning path, runner unit tests, and
    [`LOCAL_BASELINE_BUILD.md`](LOCAL_BASELINE_BUILD.md). The runner verifies
    baseline provenance before configuration and retains JSON test and
    environment/dependency evidence under the ignored build directory.
  - Decisions: none; the implementation applies accepted ADR-0002 Option A.
    The Windows preset configures the existing `openmw-cs` target because the
    pinned upstream CMake assigns its Windows manifest whenever `WIN32` is true;
    the build preset still selects only the three upstream test targets, so no
    baseline-source patch or additional product target is introduced.
  - Verification: `python -m unittest
    scripts.tests.test_run_vnext_baseline
    scripts.tests.test_verify_vnext_baseline -v` passed 13 tests;
    `python -m py_compile scripts/run_vnext_baseline.py
    scripts/verify_vnext_baseline.py
    scripts/tests/test_run_vnext_baseline.py
    scripts/tests/test_verify_vnext_baseline.py` passed; JSON parsing of
    `CMakePresets.json`, the dependency lock, and the provenance manifest
    passed; `python scripts/verify_vnext_baseline.py --index` enumerated exactly
    14 intentional differences and verified 13 dependency-declaration inputs;
    and `git diff --cached --check` passed. From an MSVC 2022 v143 x86-64
    developer environment, `python scripts/run_vnext_baseline.py all --index`
    configured with CMake 3.31.6/Ninja 1.12.1 and compiled all 1,245 Ninja
    actions; after correcting the runner's DLL search path, `python
    scripts/run_vnext_baseline.py test` passed `components-tests` (1,395),
    `openmw-tests` (490), and `openmw-cs-tests` (154), with zero failures or
    disabled tests. A clean-checkout rerun remains pending before completion.
  - Owner review: no new architecture, authority, state-scope, security,
    gameplay, or user-visible behavior decision was introduced. This mechanical
    slice does not require a separate decision review or implementation demo.
  - Follow-ups: run the committed source through the complete command from a
    clean disposable worktree, record its exact evidence, then mark Slice 1.4
    **Implemented**. Slices 1.5–1.7 still own supported-platform CI and retained
    CI environment/license artifacts.
- 2026-08-25 — Slice 1.4 — Implemented
  - Change: implementation commit
    `67afd26db8648a14d560a55600d51b96789acac3` establishes the versioned local
    presets, fail-closed runner, pinned Windows dependency provisioning,
    machine-readable evidence, tests, and local build documentation described
    above. This status update records the completed clean-checkout gate.
  - Decisions: none; the completed implementation remains within accepted
    ADR-0002 Option A and does not change platform support, dependency policy,
    compiler policy, target ownership, state scope, or gameplay behavior.
  - Verification: from a clean detached worktree at exact implementation commit
    `67afd26db8648a14d560a55600d51b96789acac3`, `python
    scripts/run_vnext_baseline.py all` passed the provenance check for exactly
    14 intentional differences and 13 dependency-declaration inputs,
    configured with CMake 3.31.6, Ninja 1.12.1, MSVC 19.44.35228.0/v143, and Qt
    6.6.3, and completed all 1,245 Ninja actions. The generated JSON reports
    record `components-tests` (1,395), `openmw-tests` (490), and
    `openmw-cs-tests` (154) with zero failures and zero disabled tests (2,039
    total). The evidence artifact records the exact source commit, dependency
    lock SHA-256
    `f5c6e975a8c2340959f95c1f417b86581fbc5c9441c197b052fabcb5499d66ee`,
    vcpkg archive SHA-512, 120 package list files, and 119 copyright files.
    The runner/verifier unit suite also passed all 13 tests; Python compilation,
    JSON parsing, staged provenance verification, and staged diff checks passed.
  - Owner review: no new architecture, authority, state-scope, security,
    gameplay, or user-visible behavior decision was introduced. The clean run
    matched the already accepted build-policy decision, so no additional demo
    gate is required for this mechanical slice.
  - Follow-ups: Slices 1.5–1.7 add and retain evidence from supported Linux,
    Windows, and macOS CI. Slice 1.8 proves compiled legacy multiplayer
    exclusion; Phase 1 remains **In Progress** until those gates pass.
- 2026-08-25 — Slice 1.5 — In Progress
  - Change: this commit adds a separate Ubuntu 24.04 baseline workflow for the
    approved GCC 13 and Clang 18 gates, removes the inherited floating
    `ubuntu-latest` job, adds an inherited Linux CI preset that builds and
    installs the complete upstream desktop target set, and routes configure,
    build, upstream tests, install, and evidence through the repository-owned
    baseline runner. A new fail-closed capture script archives resolved dpkg
    versions, apt sources/policy, runner metadata, and dereferenced package
    copyright files alongside the installed tree and JSON test reports.
  - Decisions: none; the runner label, compiler matrix, Ninja generator,
    platform-native dependency flow, evidence requirements, and per-change
    cadence implement owner-approved ADR-0002 Option A. The workflow pins
    `actions/checkout` v6.0.2 and `actions/upload-artifact` v4.6.2 by exact
    commits and introduces no architecture, authority, state-scope, or gameplay
    behavior decision.
  - Verification: `python -m unittest
    scripts.tests.test_capture_vnext_linux_ci
    scripts.tests.test_run_vnext_baseline
    scripts.tests.test_verify_vnext_baseline -v` passed 23 tests; `python -m
    py_compile` passed for both runners and their tests; JSON parsing of
    `CMakePresets.json` and `BASELINE_PROVENANCE.json` passed; staged
    `python scripts/verify_vnext_baseline.py --index` and `git diff --cached
    --check` passed. Read-only `git ls-remote` checks resolved checkout v6.0.2
    to `de0fac2e4500dabe0009e67214ff5f5447ce83dd` and upload-artifact v4.6.2
    to `ea165f8d65b6e75b540449e92b4886f43607fa02`. SHA-256-verified actionlint
    v1.7.12 accepted the workflow, and Visual Studio's bundled CMake
    4.3.1-msvc1 parsed the preset file and listed the Linux CI build preset; the
    host condition correctly hides the Linux configure preset on Windows.
  - Owner review: no new decision or user-visible behavior is introduced. The
    implementation follows the already approved ADR-0002 scenarios; hosted CI
    evidence still requires review before this slice can be **Implemented**.
  - Follow-ups: publish the commit through the normal review path, require both
    Linux compiler jobs to pass from the committed tree, inspect the retained
    environment/package/license/install artifacts, record the workflow run URLs
    and artifact evidence, and then mark Slice 1.5 **Implemented**. Slices
    1.6–1.8 remain separate Phase 1 work.

- 2026-08-25 — Slice 1.6 — In Progress
  - Change: this commit adds a dedicated Windows Server 2022 baseline workflow,
    an inherited full-desktop Windows CI preset, and fail-closed capture of the
    resolved runner/MSVC/SDK environment, dependency manifest and lock, vcpkg
    package and license evidence, pinned Qt license reference, installed tree,
    and JSON test reports. It removes the duplicate inherited Windows push job
    while leaving its reusable packaging workflow available to upstream release
    jobs.
  - Decisions: none; Windows Server 2022, Visual Studio 2022/v143, Ninja,
    repository-owned commands, native pinned dependencies, and per-change CI
    implement owner-approved ADR-0002 Option A. Exact action commits are pinned.
    The current local host exposes only Visual Studio 18, so the runner rejected
    it instead of silently expanding the approved toolchain.
  - Verification: `python -m unittest
    scripts.tests.test_capture_vnext_windows_ci
    scripts.tests.test_capture_vnext_linux_ci
    scripts.tests.test_run_vnext_baseline
    scripts.tests.test_verify_vnext_baseline -v` passed 33 tests; `python -m
    py_compile` passed for both platform evidence scripts, the baseline runner,
    verifier, and their tests; JSON parsing and `git diff --cached --check`
    passed; staged `python scripts/verify_vnext_baseline.py --index` enumerated
    exactly 22 intentional differences and verified 17 dependency-declaration
    files. SHA-256-verified actionlint v1.7.12 accepted the Windows, Linux, and
    inherited push workflows. Read-only upstream checks resolved
    `ilammy/msvc-dev-cmd` v1 to
    `0b201ec74fa43914dc39ae48a89fd1d8cb592756`, and the pinned provisioning
    command verified the OpenMW manifest, dependency archive, and existing Qt
    install. The complete hosted Windows build/test/install/evidence run remains
    pending.
  - Owner review: no new architecture, authority, state-scope, security,
    gameplay, or user-visible behavior decision is introduced. The work follows
    the accepted ADR-0002 scenarios; hosted evidence still requires review
    before this slice can be **Implemented**.
  - Follow-ups: publish the committed workflow, require its Windows Server 2022
    job to pass, inspect the retained environment/package/license/install
    artifact, record the run URL and evidence, then mark Slice 1.6
    **Implemented**. Slices 1.7 and 1.8 remain separate Phase 1 work.
- 2026-08-25 — Slice 1.5 — In Progress
  - Change: diagnosed the first two hosted Linux runs and added a bounded repair
    that configures both full-build CI presets with the repository-local
    `build/vnext-baseline-install` prefix. OpenMW 0.51 computes several Linux
    install destinations from the configure-time prefix, so the runner's later
    `cmake --install --prefix` argument alone could not redirect those cached
    absolute paths away from `/usr/local`.
  - Decisions: none; this fixes the already approved ADR-0002 repository-owned
    build/install path without changing platform support, dependencies,
    architecture, authority, state scope, or gameplay behavior.
  - Verification: hosted Linux runs
    [32918583062](https://github.com/poisson-fish/TES3MP/actions/runs/32918583062)
    and
    [32919556702](https://github.com/poisson-fish/TES3MP/actions/runs/32919556702)
    both configured and built the full GCC 13/Clang 18 matrix and passed all
    three upstream test binaries before failing closed when install attempted
    `/usr/local/etc/openmw`. After the repair, `python -m unittest
    scripts.tests.test_capture_vnext_windows_ci
    scripts.tests.test_capture_vnext_linux_ci
    scripts.tests.test_run_vnext_baseline
    scripts.tests.test_verify_vnext_baseline -v` passed 33 tests; Python
    compilation and JSON parsing passed; Visual Studio's bundled CMake
    4.3.1-msvc1 listed the updated Windows presets; and `git diff --check`
    passed. `python scripts/verify_vnext_baseline.py --index` enumerated exactly
    22 intentional differences, verified 17 dependency-declaration inputs, and
    passed; hosted proof remains pending.
  - Owner review: no new decision or user-visible behavior is introduced. The
    repair remains within the accepted ADR-0002 policy and does not require a
    separate decision or demo review.
  - Follow-ups: publish the repair, require the GCC 13 and Clang 18 hosted jobs
    to build, test, install wholly under the repository, capture evidence, and
    upload their retained artifacts before marking Slice 1.5 **Implemented**.
- 2026-08-25 — Slice 1.7 — In Progress
  - Change: this commit adds a dedicated macOS baseline workflow, an inherited
    full-desktop macOS CI preset, commit- and hash-pinned OpenMW dependency
    provisioning for arm64 and x86-64, and fail-closed capture of runner,
    Xcode, AppleClang, Homebrew formula/version/license, vcpkg
    package/license, Qt license, install-tree, and upstream-test evidence. The
    obsolete inherited push workflow is removed after all three platform gates
    received dedicated replacements; the reusable upstream macOS packaging
    workflow remains available to release composition.
  - Decisions: none; macOS 15, Xcode 16/AppleClang, Ninja, arm64 per-change
    coverage, and x86-64 scheduled/release/manual coverage implement the
    owner-approved ADR-0002 Option A. The dependency repository commit and both
    architecture bundle hashes reuse OpenMW 0.51's accepted `2026-02-24`
    dependency generation.
  - Verification: the two commit-pinned OpenMW dependency manifests resolved
    to repository commit `0224d1bb5c7910024be22dc967828f8bf7a41817` and
    matched recorded SHA-256 values
    `6e8cd833e575d66727446ff445546e830254ae76299fa7f6b647a458706262a5`
    (arm64) and
    `f9f9ca974fcbbad9f3298c90d495da28e4de56aa03176e4c0399c183c725fc29`
    (x86-64); their declared archives are independently SHA-512 pinned.
    `python -m unittest scripts.tests.test_capture_vnext_macos_ci
    scripts.tests.test_capture_vnext_windows_ci
    scripts.tests.test_capture_vnext_linux_ci
    scripts.tests.test_run_vnext_baseline
    scripts.tests.test_verify_vnext_baseline -v` passed 43 tests; Python
    compilation and all modified JSON parsing passed; SHA-256-verified
    actionlint v1.7.12 accepted the dedicated and retained macOS workflows;
    Visual Studio's bundled CMake 4.3.1-msvc1 parsed the cross-platform preset
    file; `git diff --check` passed; and `python
    scripts/verify_vnext_baseline.py --index` enumerated exactly 25 intentional
    differences and verified 18 dependency-declaration inputs. Hosted evidence
    remains to be finalized after publication. The first published arm64 run
    (`32923415388`) provisioned the pinned dependencies and completed all 1,343
    Ninja build actions, including the three upstream test executables, before
    failing closed because deployment-mode CMake places those executables in
    `OpenMW.app/Contents/MacOS` while the runner still checked the build root.
    The follow-up maps Darwin test execution to the bundle runtime directory
    and adds a regression assertion for that platform-specific path. Exact
    repair run `32925227820` then executed the arm64 test binary and exposed
    seven upstream Euler-angle assertions whose strict one-float-epsilon bound
    rejects valid Apple Silicon results at singular angles; all observed
    differences are below `1e-6`, the tolerance already used by the adjacent
    Euler-angle test. The bounded follow-up changes only that test tolerance;
    production math and runtime behavior are unchanged.
  - Owner review: no architecture, authority, state-scope, security, gameplay,
    or user-visible behavior decision is introduced. The slice follows the
    already approved ADR-0002 scenarios and does not require another decision
    review or implementation demo.
  - Follow-ups: publish the workflow, require the macOS 15 arm64 job to pass on
    the committed tree, manually dispatch and require the macOS 15 Intel job to
    pass, inspect both retained evidence artifacts, and then mark Slice 1.7
    **Implemented**.
- 2026-08-25 — Slice 1.5 — Implemented
  - Change: accepted the dedicated Ubuntu 24.04 GCC 13 and Clang 18 baseline
    gates after the repository-local install-prefix repair passed on the final
    Slice 1.7 tree.
  - Decisions: none; the jobs implement the already approved ADR-0002 Linux
    policy and introduce no architecture or gameplay behavior.
  - Verification: hosted push run
    [`32927109314`](https://github.com/poisson-fish/TES3MP/actions/runs/32927109314)
    passed both compiler jobs on commit
    `8e378c2c39f52ccd09962e368c3d810398203b4f`. Exact-run artifacts
    `9592632556` (GCC 13, SHA-256
    `17d35e9bdc25dfdd7a721806fd18eda25a4bcbf7f836e77737034df474fe7321`)
    and `9592800250` (Clang 18, SHA-256
    `2657c2718210298f660a62f1d3f4cbd034e943db7ffb14dff4340c3b9460c272`)
    were retained. Content review of the immediately preceding successful
    artifacts confirmed Ubuntu 24.04.4 x86-64, GNU 13.3.0 and Clang 18.1.3,
    CMake 3.31.6, Ninja 1.13.2, 390 installed files per job, and 1,397 + 490
    + 154 upstream tests with zero failures or disabled tests. The GCC artifact
    contained 1,446 package rows and 1,435 copyright records; Clang contained
    1,445 and 1,434 respectively, and every recorded evidence hash matched the
    downloaded file.
  - Owner review: no additional approval is required; the slice is mechanical
    evidence for accepted ADR-0002.
  - Follow-ups: Slice 1.8 remains the only incomplete Phase 1 slice.
- 2026-08-25 — Slice 1.6 — Implemented
  - Change: accepted the dedicated Windows Server 2022/MSVC v143 baseline gate
    after full build, upstream tests, repository-local install, evidence
    capture, and retention passed on the final Slice 1.7 tree.
  - Decisions: none; the job implements the accepted ADR-0002 Windows policy
    and introduces no architecture or gameplay behavior.
  - Verification: hosted push run
    [`32927109296`](https://github.com/poisson-fish/TES3MP/actions/runs/32927109296)
    passed on commit
    `8e378c2c39f52ccd09962e368c3d810398203b4f`; exact-run artifact
    `9592404044` was retained with SHA-256
    `df0e11751e1dac8de012d61eb3e1d864182f4b028d98eeaa192e5f0db09b8435`.
    Downloaded evidence from the immediately preceding successful tree
    confirmed Windows Server 2022 x64, MSVC 19.44.35228/v143, CMake 3.31.6,
    Ninja 1.13.2, Qt 6.6.3, 435 installed files, and 1,395 + 490 + 154
    upstream tests with zero failures or disabled tests. Its hash-verified
    dependency archive contained 120 package lists and 120 license records.
  - Owner review: no additional approval is required; the slice is mechanical
    evidence for accepted ADR-0002.
  - Follow-ups: Slice 1.8 remains the only incomplete Phase 1 slice.
- 2026-08-25 — Slice 1.7 — Implemented
  - Change: accepted the dedicated macOS 15 gates after both supported
    architectures passed full build, all upstream tests, repository-local app
    bundle installation, fail-closed evidence capture, and artifact retention.
    The final bounded baseline fix uses the same `1e-6` single-precision
    tolerance as the adjacent Euler-angle test; production math is unchanged.
  - Decisions: none; the workflow cadence and toolchains implement accepted
    ADR-0002, while the test-only portability repair changes no architecture,
    authority, state scope, gameplay behavior, or user-visible behavior.
  - Verification: per-change arm64 run
    [`32927109264`](https://github.com/poisson-fish/TES3MP/actions/runs/32927109264)
    and manually dispatched arm64/Intel run
    [`32927114955`](https://github.com/poisson-fish/TES3MP/actions/runs/32927114955)
    passed on commit
    `8e378c2c39f52ccd09962e368c3d810398203b4f`. Reviewed exact-run artifacts
    `9592528855` (arm64, SHA-256
    `9a98a8f478b9c735b497e3895fb72c7f2ab64782f8fea4bfd07ec5050174f6ab`)
    and `9593222068` (Intel, SHA-256
    `24dd32264d75d7c402d2929709d021abade8fd5e98509b66a3aaaabefe9973a3`)
    recorded macOS 15/Darwin 24.6.0, Xcode 16.0/AppleClang 16.0.0, CMake
    4.4.0, Ninja 1.13.2, Qt 6.11.1, 1,020 installed paths, and 1,397 + 490
    + 154 upstream tests with zero failures or disabled tests on each
    architecture. Each contained 120 vcpkg package lists and 118 license files;
    the arm64 and Intel archives recorded 168 and 187 Homebrew formulas. All
    six recorded evidence-file hashes matched for both artifacts, including
    dependency-archive SHA-256 values
    `4a8f9437923216dc901fdd193a0ef07d9cc24b98c82813c6381cb82fbb71c81b`
    and `bcba13d79e7b978f2e9baa684b75308a2e6c9586ffca4d6278d473a238c64b36`.
  - Owner review: no additional approval is required; the slice satisfies the
    already approved ADR-0002 scenarios without a new design or demo gate.
  - Follow-ups: Slice 1.8 is now the next eligible implementation slice. Phase
    1 remains **In Progress** until compiled legacy multiplayer exclusion is
    proven.

- 2026-08-26 — Slice 1.8 — Implemented
  - Change: implementation commit `13b4282cfa9918a932d36825479b645c0127e4ff`
    adds `scripts/verify_vnext_legacy_exclusion.py`, its
    failure-case suite, baseline-runner integration, retained JSON evidence,
    provenance coverage, and local documentation. Every supported platform's
    existing `all --ci` path now fails during configuration if an archived
    TES3MP server/browser, packet-processor, RakNet/CrabNet, or CoreScripts
    path or token is tracked by the active tree or reachable from generated
    compilation commands or Ninja build edges.
  - Decisions: none; this is a mechanical Phase 1 exclusion control over the
    already approved OpenMW baseline and existing Ninja CI path. It introduces
    no vNext multiplayer target, architecture, authority, state-scope,
    security, gameplay, or user-visible behavior choice.
  - Verification: `python -m unittest discover -s scripts/tests -v` passed all
    51 repository-owned Python tests, including clean evidence generation and
    rejection of a tracked legacy path, RakNet CMake declaration, legacy
    compilation source, `tes3mp-server` Ninja target, and empty compilation
    database. `python -m py_compile` passed for the new verifier/tests and
    modified runner/tests. `python scripts/verify_vnext_baseline.py --index`
    accounted for exactly 28 intentional differences and verified 19 hashed
    dependency-declaration inputs. From an MSVC 2022 v143 x86-64 developer
    environment, `python scripts/run_vnext_baseline.py all --index` configured
    with CMake 3.31.6/Ninja 1.12.1 and MSVC 19.44.35228.0, verified 3,724 staged
    tracked paths, 53 CMake metadata files, 1,232 compilation commands, and
    1,875 Ninja build edges, rebuilt the affected baseline targets, and passed
    `components-tests` (1,395), `openmw-tests` (490), and `openmw-cs-tests`
    (154) with zero failures. `git diff --cached --check` passed.
    A subsequent committed-tree rerun at
    `13b4282cfa9918a932d36825479b645c0127e4ff` passed `python -m unittest
    discover -s scripts/tests -v` (51 tests), Python compilation, `python
    scripts/verify_vnext_baseline.py`, and `python
    scripts/verify_vnext_legacy_exclusion.py`. From the same MSVC 2022 v143
    x86-64 environment, `python scripts/run_vnext_baseline.py all` then passed
    a fresh configure, the same 3,724-path/53-metadata/1,232-command/1,875-edge
    exclusion proof, the build, and all 2,039 upstream tests with zero failures.
    The retained local evidence records exact source commit `13b4282cfa`, CMake
    3.31.6, Ninja 1.12.1, MSVC 19.44.35228.0, and Qt 6.6.3. `git diff --check`
    passed before this status update.
    Hosted push runs
    [`32934599668`](https://github.com/poisson-fish/TES3MP/actions/runs/32934599668),
    [`32934599672`](https://github.com/poisson-fish/TES3MP/actions/runs/32934599672),
    and
    [`32934599670`](https://github.com/poisson-fish/TES3MP/actions/runs/32934599670)
    passed on exact source `2cad2dbb28b63848f2b43d533d5ebab26aa79cff`.
    Linux GCC 13 and Clang 18 each verified 3,724 tracked paths, 53 CMake files,
    1,216 compile commands, and 1,725 Ninja edges, then passed 1,397 + 490 +
    154 upstream tests. Windows MSVC 2022 v143 verified 3,724/53/1,310/2,166
    and passed 1,395 + 490 + 154 tests. macOS 15 arm64/Xcode 16 verified
    3,724/53/1,284/2,349 and passed 1,397 + 490 + 154 tests. All jobs passed
    baseline provenance for 19 dependency inputs and uploaded retained evidence
    artifacts `9595155142`, `9595191295`, `9595061296`, and `9595066122`, whose
    names bind them to the exact source commit and whose successful upload steps
    include `vnext-legacy-exclusion.json`.
  - Owner review: on 2026-08-26 the project owner explicitly directed acceptance
    of the successful current-HEAD Linux, Windows, and macOS baseline runs for
    Slice 1.8 and directed that the cancelled manual macOS rerun not be used.
    This accepts the three-OS implementation evidence; it does not by itself
    approve the separate Phase 1 exit gate or authorize Phase 2 production.
  - Follow-ups: Phase 1 exit-gate approval is recorded below; hold the Phase 2
    kickoff and obtain the required ADR approvals before dependent production
    implementation begins.

- 2026-08-26 — Phase 1 exit gate — Implemented
  - Change: marked Phase 1 and its program-tracker row **Implemented** after all
    Slices 1.1–1.8 and the clean OpenMW 0.51 baseline exit criteria passed.
  - Decisions: no architecture, authority, state-scope, or gameplay decision
    changed. The owner accepted the successful current-HEAD Linux, Windows, and
    macOS arm64 Slice 1.8 matrix and excluded the cancelled manual macOS rerun
    from evidence; the already accepted Slice 1.7 Intel baseline evidence
    remains unchanged.
  - Verification: the active tree remains based on pinned OpenMW commit
    `f4bec41444214a7903bebd178389ca22ca13f646`; the baseline verifier accounts
    for exactly 28 intentional differences and 19 dependency-declaration
    inputs; supported desktop build/install/upstream-test evidence is recorded
    in Slices 1.4–1.7; and Slice 1.8 records fail-closed tracked-tree,
    CMake-metadata, compilation-database, and Ninja-graph exclusion proof from
    local Windows plus hosted Linux GCC/Clang, Windows MSVC, and macOS arm64
    runs. All accepted jobs passed with retained evidence artifacts.
  - Owner review: explicit Phase 1 exit-gate approval and authorization to mark
    the phase complete received on 2026-08-26. This approval makes Phase 2
    eligible for kickoff; it does not pre-approve any Phase 2 ADR choice or
    dependent production implementation.
  - Follow-ups: hold the Phase 2 kickoff, present ADR-0003 through ADR-0007 and
    deterministic simulation/protocol compatibility decision packets in their
    eligible order, and obtain explicit owner approval before production work.

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

Implementation notes:

- Authentication must be an owned interface; choosing a transport does not imply
  exposing that library's identity or connection types outside the adapter.
- The protocol version is semantic product metadata, not an engine commit hash.
- Deferred gameplay-specific details may be appended to ADR-0006 before their
  phases, but the initial command/state ownership model must be decided now.
- A selection proof is disposable evidence, not the production wrapper created
  in Phase 6.

- 2026-08-26 — Slice 2.1 — Implemented
  - Change: added accepted
    [`ADR-0003`](adr/ADR-0003-hostile-internet-threat-model.md), defining the
    hostile-Internet threat posture, assets, trust boundaries, attacker
    capabilities, abuse scenarios, phase-owned mitigations, required
    verification, and explicit deferrals.
  - Decisions: the project owner approved recommended Option A. Every client and
    network input, including authenticated clients, remains untrusted; the
    dedicated-server host/operator is the initial trusted boundary; secure
    transport and authentication do not grant gameplay authority; and future
    scripts, persistence, metrics, and administration remain behind separate
    typed least-privilege boundaries.
  - Verification: `python scripts/verify_vnext_baseline.py --index` accounts for
    the new ADR as the 29th intentional difference while retaining all 19
    dependency-input hashes; `git diff --cached --check`, local Markdown-link
    checks, required ADR section/status checks, and the 51-test repository-owned
    Python suite pass.
  - Owner review: Option A explicitly approved in the 2026-08-26 working
    session after presentation of hostile-Internet, cooperative-client, and
    hostile-host options with scenarios, tradeoffs, recommendation, and proposed
    acceptance evidence.
  - Follow-ups: Slice 2.2 is the next eligible decision slice. ADR-0004 must be
    approved before a schema/codec selection or dependent production code.

- 2026-08-26 — Slice 2.2 — In Progress
  - Change: accepted
    [`ADR-0004`](adr/ADR-0004-protocol-schema-codec-evolution-policy.md); added
    an exact source/archive/license lock, safe proof runner, explicitly numbered
    v1/v2 schemas and pinned generated C++, an isolated C++20 proof decoder,
    schema-policy checks, deterministic fuzz corpus generation, a Clang
    ASan/UBSan/libFuzzer target, retained evidence, Android ARM64 assessment,
    and the accepted Windows/Linux/macOS workflow matrix.
  - Decisions: the project owner approved Option A without changes. FlatBuffers
    `v25.12.19` is pinned to commit `7e163021e59cca4f8e1e35a7c828b5c6b7915953`;
    only generated accessors/builders and verifier are permitted; generated
    views remain private and are converted to owned validated values; explicit
    stable IDs and additive optional evolution are mandatory; and all excluded
    dynamic, reflection, nested, mutable, object, RPC, and 64-bit-offset
    surfaces remain prohibited.
  - Verification: local Windows x86-64 MSVC 2022 v143 proof builds pinned `flatc`
    from the SHA-256-verified source, reproduces the committed headers, and
    passes exact framing, every-truncation, corrupt input, byte/depth/table,
    unknown-union, UTF-8/numeric, pre-allocation, no-partial-output, owned
    lifetime, and bidirectional v1/v2 evolution scenarios. The repository-owned
    Python suite passes 64 tests; Actionlint 1.7.12 accepts the new workflow;
    `python scripts/verify_vnext_baseline.py --index` accounts for all 45
    intentional differences and verifies all 34 dependency-input hashes; and
    local Markdown-link, ADR section/status, generated-drift, JSON/compile,
    `git diff --cached --check`, and clean staged-tree checks pass.
  - Owner review: Option A and its complete restricted profile and proposed
    acceptance tests explicitly approved in the 2026-08-26 working session.
  - Follow-ups: obtain and review retained GCC 13, Clang 18 sanitizer/fuzzer,
    macOS arm64, macOS x86-64, and Windows hosted artifacts before changing
    Slice 2.2 to **Implemented**. No Phase 4 production codec or dependent Phase
    3 implementation may begin before the remaining Phase 2 gates.

- 2026-08-26 — Slice 2.2 — In Progress
  - Change: inspected hosted run `32946871261`; Linux GCC 13, Linux Clang 18
    with the sanitizer/fuzzer smoke, and macOS arm64 passed, while Windows
    failed the generated-header byte comparison because Git's Windows checkout
    conversion was not constrained. Added a narrow `.gitattributes` rule that
    pins only the committed proof headers to LF, plus a regression test and
    proof documentation.
  - Decisions: none. This preserves the approved schema, generator arguments,
    generated output, dependency pin, supported matrix, and exact byte-drift
    policy; it only makes the repository representation platform-independent.
  - Verification: hosted run `32946871261` passed Linux GCC 13, Linux Clang 18
    with its 30-second ASan/UBSan/libFuzzer smoke, and macOS arm64. Locally,
    `python -m unittest discover -s scripts/tests -v` passes 65 tests;
    `python scripts/run_vnext_flatbuffers_proof.py` passes from the approved
    Visual Studio 2022 v143 x64 environment and retains evidence; `python -m
    py_compile` passes for the proof runner, its tests, and the baseline
    verifier; JSON parsing passes; cached `git check-attr` reports `text: set`
    and `eol: lf` for both generated headers; `python
    scripts/verify_vnext_baseline.py --index` accounts for all 48 intentional
    differences and verifies all 35 dependency inputs; and `git diff --cached
    --check` passes. Replacement hosted Windows and macOS x86-64 evidence is
    still pending.
  - Owner review: no architecture, authority, state-scope, security, gameplay,
    or user-visible behavior decision is introduced by the line-ending repair.
  - Follow-ups: publish the repair so the Windows proof can rerun, then manually
    dispatch and review the full workflow including macOS x86-64 before marking
    Slice 2.2 **Implemented**.

- 2026-08-26 — Slice 2.2 — In Progress
  - Change: push run `32989871740` passed Windows MSVC, Linux GCC 13, Linux
    Clang 18 with the 30-second sanitizer/fuzzer smoke, and macOS arm64 at
    `da918dd462`; manually dispatched run `32995432424` then passed the full
    five-job matrix including macOS x86-64. Retained-artifact review found that
    Windows checkout conversion changed the recorded lock and schema hashes,
    and that unspecified C++ function-argument evaluation order made three seed
    corpus files differ between compiler families. The working-tree repair pins
    every byte-hashed text input to LF, sequences FlatBuffer builder mutations,
    pins all five deterministic seed hashes in the dependency lock, fails the
    proof on seed drift, and excludes libFuzzer-added working entries from the
    retained seed-corpus identity.
  - Decisions: none. The change enforces the already approved exact-input and
    deterministic-corpus proof requirements without altering the schema,
    protocol evolution policy, dependency selection, or production behavior.
  - Verification: all five jobs and artifacts from run `32995432424` were
    reviewed; their dependency, generated-header, compiler, test, corpus, and
    license fields exposed the two issues above. Locally, the full Windows MSVC
    2022 v143 selection proof passes with the five pinned seed identities;
    `python -m unittest discover -s scripts/tests -v` passes 66 tests; `python
    -m py_compile` passes for the proof runner, its tests, and the baseline
    verifier; `python scripts/verify_vnext_baseline.py --index` accounts for all
    48 intentional differences and verifies all 35 dependency inputs; and `git
    diff --cached --check` passes. Replacement hosted artifact evidence remains
    pending.
  - Owner review: no new architecture, authority, state-scope, security,
    gameplay, or user-visible behavior decision is introduced. Slice 2.2 stays
    **In Progress** because passing jobs alone did not satisfy retained-evidence
    consistency.
  - Follow-ups: publish the determinism repair, rerun the full five-job matrix,
    and compare retained exact input, generated, seed-corpus, license, compiler,
    and test identities before owner review and any **Implemented** status
    change.

- 2026-08-26 — Slice 2.2 — In Progress
  - Change: commit `da24423a1a56ac0e499eebd962b6499db3866b0f`
    published the exact-input and deterministic-seed repair. Manually dispatched
    [run `32996083180`](https://github.com/poisson-fish/TES3MP/actions/runs/32996083180)
    executed the complete five-job selection-proof matrix at that commit.
  - Decisions: none; the repair implements the already approved ADR-0004 proof
    contract and changes no schema, codec selection, protocol behavior, or
    dependency version.
  - Verification: Windows MSVC 2022 v143, Linux GCC 13, Linux Clang 18 with the
    30-second ASan/UBSan/libFuzzer smoke, macOS arm64 Xcode 16, and macOS x86-64
    Xcode 16 all passed. Retained artifacts `9616640336`, `9616634804`,
    `9616631479`, `9616613594`, and `9616613060` were downloaded and compared
    against the committed lock. All five contain the identical lock SHA-256
    `a1cce8c4475460a5dcc29c0164b6743b3e228019068c08d6453c462512a95766`,
    two schema hashes, two generated-header hashes, five pinned seed hashes, and
    Apache-2.0 license SHA-256
    `cfc7749b96f63bd31c3c42b5c471bf756814053e847c10f3eb003417bc523d30`;
    platform/compiler identities and declared proof/fuzz tests also match the
    approved matrix.
  - Owner review: technical completion evidence is ready for review. Explicit
    owner acceptance is pending, so Slice 2.2 remains **In Progress**.
  - Follow-ups: obtain owner completion acceptance, then change Slice 2.2 to
    **Implemented**. Phase 2 still separately requires Slices 2.3–2.6.

- 2026-08-26 — Slice 2.2 — Implemented
  - Change: recorded completion of the owner-approved restricted FlatBuffers
    selection and its exact cross-platform proof at implementation commit
    `da24423a1a56ac0e499eebd962b6499db3866b0f` and evidence-record commit
    `f99239244cf689cdb494615785a95360520ff2f3`.
  - Decisions: no new decision; ADR-0004 remains accepted without amendment.
  - Verification: full five-job run `32996083180`, retained artifacts
    `9616640336`, `9616634804`, `9616631479`, `9616613594`, and `9616613060`,
    cross-platform exact-identity comparison, the local Windows proof, 66
    repository-owned Python tests, Python compilation, baseline provenance,
    JSON, Markdown-link, and diff checks all passed as recorded above.
  - Owner review: the project owner explicitly approved the Slice 2.2
    completion evidence in the 2026-08-26 working session and authorized the
    **Implemented** status change.
  - Follow-ups: none for Slice 2.2. Phase 2 remains **In Progress** and next
    requires completion of Slices 2.3–2.6.

- 2026-08-26 — Slice 2.3 — In Progress
  - Change: accepted and then amended
    [`ADR-0005`](adr/ADR-0005-transport-security-authentication-resumption.md)
    to select standalone GameNetworkingSockets `v1.6.0` direct-IP transport with
    automatic basic encryption, a separate optional shared join-password
    authenticator, and automatic short-lived single-use resume tokens. Updated
    ADR-0003 with the corresponding explicitly accepted active endpoint
    impersonation risk and retained the trust-integration audit as a resolved
    historical gate rather than a patch proposal.
  - Decisions: production transport must be encrypted but is intentionally not
    endpoint-authenticated for the first community-server milestone. Steam,
    relay, P2P, operator certificates, trust anchors, pinning, certificate
    revocation, private upstream APIs, and a maintained GameNetworkingSockets
    patch are excluded. A join password is sent only after encrypted connection
    establishment, is treated as a low-value server-specific secret, and is not
    replaced by a replayable static password hash. Persistent accounts and
    administrative credentials remain outside this decision.
  - Verification: primary upstream release, API, platform, build, security,
    license, maintenance, and standards sources were reviewed on 2026-08-26;
    the tagged-source public/private certificate paths were audited; the
    repository-owned Python suite passes 64 tests; local Markdown links and
    manifest JSON parsing pass; `git diff --cached --check` passes; and
    `python scripts/verify_vnext_baseline.py --index` accounts for all 47
    intentional differences and verifies all 34 dependency-declaration inputs.
    Cross-platform selected-library proof evidence is still pending.
  - Owner review: the owner explicitly approved the amended automatic-encryption
    recommendation on 2026-08-26 after reviewing password handling, resume-token
    behavior, operator burden, the lack of dependency patches, and the active
    man-in-the-middle limitation. This supersedes the earlier configured-trust
    profile and rejects all A1 patch/CA work.
  - Follow-ups: create the disposable GameNetworkingSockets/OpenSSL/Protocol
    Buffers dependency lock and selection proof against the amended ADR-0005
    acceptance tests. Slice 2.2 hosted evidence remains independently in
    progress and was intentionally not monitored in this working session.

- 2026-08-26 — Slice 2.3 — In Progress
  - Change: added the disposable selected-library proof, exact source/archive/
    license lock, safe source acquisition and extraction, restricted static
    build, real encrypted direct-IP client/server capture harness, fault and
    lifecycle scenarios, retained evidence, Android ARM64 assessment, and the
    required five-job desktop workflow. This remains selection evidence and
    creates no production multiplayer target or Phase 6 wrapper.
  - Decisions: after explicit owner approval of dependency Option A, ADR-0005
    records GameNetworkingSockets `1.6.0`, OpenSSL `3.5.8` LTS, Protobuf C++
    `6.33.4`, and Abseil `20250512.1`. The proof uses unmodified upstream
    sources, static libraries, OpenSSL, and the already approved exclusions.
    Proof-only resource budgets are test limits, not production interface or
    gameplay-state decisions.
  - Verification: a clean local Windows x86-64 MSVC 2022 run verified every
    pinned archive and license, built all four source dependencies with the
    approved profile, and passed encryption/plaintext rejection, passive
    password/token canary capture, authentication ordering and failure,
    resume-token contention/rotation/invalidation, generation teardown,
    bounded queue/flood, reliable ordering, separate-lane head-of-line, and
    stable telemetry scenarios. The lock validator passes; the repository-owned
    Python suite passes 75 tests; Python compilation passes; Actionlint 1.7.12
    accepts the workflow; and JSON, Markdown-link, provenance, and diff checks
    pass. Hosted evidence is still pending.
  - Owner review: the project owner explicitly approved dependency Option A on
    2026-08-26 before implementation. No additional architecture, authority,
    state-scope, or gameplay-behavior decision was made by this proof slice.
  - Follow-ups: extend the proof through the remaining ADR-0005 slow-reader,
    excessive-segment, handshake/disconnect-flood, and actual close/unread-data
    lifecycle cases without treating proof budgets as production policy. Then
    publish and run the complete Windows, GCC 13, Clang 18 ASan/UBSan, macOS
    arm64, and scheduled/manual macOS x86-64 matrix; compare retained exact
    dependency, license, toolchain, build-profile, test, and redaction evidence;
    and request owner completion acceptance before changing Slice 2.3 to
    **Implemented**. Slices 2.4–2.6 remain separately required.

- 2026-08-26 — Slice 2.3 — In Progress
  - Change: completed the remaining disposable hostile-resource and actual
    lifecycle proof cases. The real GameNetworkingSockets harness now exercises
    slow-reader receive byte/message limits and recovery, excessive segments per
    packet, fail-closed maximum-message handling, a 32-connection handshake and
    disconnect flood behind an eight-connection admission cap and bounded
    callback drain, and close-with-unread-data handle invalidation. The exact
    proof budgets and retained scenario list are locked and regression-tested.
  - Decisions: none. The added numeric limits are disposable proof workloads,
    not production transport budgets, architecture, authority, state-scope, or
    gameplay behavior. They exercise the already owner-approved ADR-0005
    acceptance scenarios without changing the selected dependencies or profile.
  - Verification: a clean Windows x86-64 MSVC 2022 v143 run of `python
    scripts/run_vnext_gamenetworkingsockets_proof.py` reverified all four source
    archives and five licenses, rebuilt OpenSSL 3.5.8, Protobuf 6.33.4/Abseil
    20250512.1, and unmodified GameNetworkingSockets 1.6.0, then passed all nine
    proof scenarios in 2.2 seconds and retained lock hash
    `d5263f5cd197ee39f2ed862097b691396f4ad7759c19d715d98eb3c562ae295c`.
    An incremental MSVC `/W4 /WX` build passed, and `ctest --repeat
    until-fail:20` passed 20 consecutive runs in 33.06 seconds. `python -m
    unittest discover -s scripts/tests` passed 76 tests; both changed Python
    files passed `py_compile`; lock validation passed; `python
    scripts/verify_vnext_baseline.py --index` accounted for all 56 intentional
    differences and verified all 43 dependency inputs; and `git diff --cached
    --check` passed. Both changed JSON files parsed, all 32 local vNext Markdown
    links resolved, and the retained proof evidence contained neither password
    nor resume-token canaries.
  - Owner review: no new owner decision was required. Slice 2.3 completion
    acceptance remains pending the approved hosted matrix and retained-artifact
    consistency review.
  - Follow-ups: publish the completed proof, run Windows MSVC 2022, Linux GCC
    13, Linux Clang 18 ASan/UBSan, macOS arm64, and manual macOS x86-64; compare
    exact dependency, license, lock, toolchain, build-profile, budget, test, and
    redaction evidence; then request owner completion acceptance before changing
    Slice 2.3 to **Implemented**. Slices 2.4–2.6 remain separately required.

- 2026-08-26 — Slice 2.3 — In Progress
  - Change: diagnosed the first complete hosted proof run and corrected one
    portable API type mismatch in the disposable proof. GameNetworkingSockets'
    `SendMessages` result parameter uses its public `int64` typedef; using
    `std::int64_t` happened to match on Windows but is `long` rather than the
    required `long long` on the hosted Linux ABI.
  - Decisions: none. The one-line correction adopts the selected library's
    public API type and changes no dependency, proof budget, transport behavior,
    architecture, authority, state scope, or gameplay behavior.
  - Verification: hosted run
    [`33011891871`](https://github.com/poisson-fish/TES3MP/actions/runs/33011891871)
    passed Windows MSVC 2022 v143 and macOS arm64 Xcode 16, while GCC 13 job
    `98320008864` and Clang 18 ASan/UBSan job `98320008970` both failed at the
    same compile-time pointer-type mismatch; the push-only macOS x86-64 job was
    correctly skipped. Successful retained artifacts `9623265318` and
    `9623081766` were downloaded and compared: both retain the identical lock
    SHA-256 `d5263f5cd197ee39f2ed862097b691396f4ad7759c19d715d98eb3c562ae295c`,
    dependency and license identities, build profile, budgets, nine-test list,
    passing CTest output, and no password or resume-token canaries. After the
    correction, the incremental MSVC 2022 `/W4 /WX` build and all nine proof
    scenarios passed locally in 2.09 seconds. The 76-test repository-owned
    Python suite, exact lock validation, both changed JSON files, all 32 local
    vNext Markdown links, and staged-diff checks passed; baseline provenance
    accounted for all 56 intentional differences and verified all 43 dependency
    inputs.
  - Owner review: no new owner decision was required. Completion acceptance
    remains pending a fully passing replacement matrix and retained-artifact
    consistency review.
  - Follow-ups: publish the portable type correction, require replacement GCC
    13 and Clang 18 ASan/UBSan evidence plus the already approved Windows and
    macOS jobs, run the manual macOS x86-64 job, compare all five retained
    artifacts, and request owner completion acceptance before changing Slice
    2.3 to **Implemented**.

- 2026-08-26 — Slice 2.3 — In Progress
  - Change: amended the approved sanitizer proof profile after the first
    portable replacement run reached execution. The Clang job now builds
    Protobuf and Abseil with the same ASan/UBSan instrumentation as their
    GameNetworkingSockets consumers, keeps OpenSSL explicitly unsanitized, and
    excludes UBSan `function` only on the unmodified GameNetworkingSockets
    target. The proof harness retains the check; container-overflow, leak, and
    halt-on-error behavior remain enabled; and regression tests reject broad
    runtime suppression.
  - Decisions: the owner explicitly approved recommended sanitizer Option A on
    2026-08-26. Keep the pinned, unmodified dependency selection and make its
    C++ instrumentation coherent; document the one narrow upstream callback
    cast exclusion; and treat any remaining container report as a dependency
    blocker. The owner rejected immediate patch/upgrade/replacement work and
    broad process-wide sanitizer suppression.
  - Verification: hosted run
    [`33015281679`](https://github.com/poisson-fish/TES3MP/actions/runs/33015281679)
    passed GCC 13 job `98331778758`, Windows MSVC 2022 v143, and macOS arm64
    Xcode 16. Clang 18 job `98331778984` compiled successfully, then reported
    the pinned GameNetworkingSockets callback type-erasure under UBSan and a
    Protobuf repeated-field container report across the previously mixed
    instrumentation boundary; manual macOS x86-64 was correctly skipped. After
    the approved amendment, lock validation and 12 focused runner tests passed,
    and a clean locked Windows MSVC 2022 dependency rebuild passed all nine
    scenarios in 2.16 seconds with retained evidence and lock SHA-256
    `367fd5820e41b77c5857ccf1dff9b2d0698cebf8dba5385f81204f1dba64350c`.
    The complete repository-owned Python suite passed 78 tests; Python
    compilation, both changed JSON files, all 32 local vNext Markdown links,
    retained-evidence canary scanning, and staged-diff checks passed. Baseline
    provenance accounted for all 56 intentional differences and verified all
    43 dependency inputs.
  - Owner review: the sanitizer-scope decision and ADR-0005 amendment are
    explicitly approved. Slice completion acceptance remains pending a fully
    passing hosted matrix and five-artifact consistency review.
  - Follow-ups: publish the approved sanitizer profile, require its replacement
    Clang 18 ASan/UBSan run to pass without the container report or any broad
    suppression, run manual macOS x86-64, compare all five retained artifacts,
    and request owner completion acceptance before changing Slice 2.3 to
    **Implemented**.

- 2026-08-26 — Slice 2.3 — Implemented
  - Change: recorded completion of the owner-approved restricted standalone
    GameNetworkingSockets selection and its five-platform proof at implementation
    commit `d5d7a1d1f49715bd41f2eb090393785e67924598`.
  - Decisions: no new transport or security decision was introduced. The owner
    accepted the completed evidence for ADR-0005 and authorized the Slice 2.3
    status change. The approved automatic-encryption profile, explicit lack of
    endpoint authentication, shared join-password boundary, single-use resume
    tokens, dependency pins, and narrow sanitizer scope remain unchanged.
  - Verification: push run
    [`33020799953`](https://github.com/poisson-fish/TES3MP/actions/runs/33020799953)
    passed Linux GCC 13, Linux Clang 18 ASan/UBSan, Windows MSVC 2022 v143, and
    macOS arm64. Manually dispatched run
    [`33022971583`](https://github.com/poisson-fish/TES3MP/actions/runs/33022971583)
    passed the complete matrix, including macOS x86-64. Retained artifacts
    `9627645351`, `9627621339`, `9627619303`, `9627437471`, and `9627400661`
    were downloaded and compared against the committed lock. All five contain
    identical dependency, transitive-license, build-profile, budget,
    endpoint-identity-claim, and nine-scenario identities; every embedded lock
    matches SHA-256
    `367fd5820e41b77c5857ccf1dff9b2d0698cebf8dba5385f81204f1dba64350c`;
    every retained license matches its manifest; and every scenario passes.
    Exactly one artifact uses the approved Clang 18.1.3 ASan/UBSan profile, with
    no sanitizer report, runtime error, credential canary, or broad suppression.
  - Owner review: the project owner explicitly approved completion Option A in
    the 2026-08-26 working session and authorized the **Implemented** status.
  - Follow-ups: none for Slice 2.3. Phase 2 remains **In Progress**; Slice 2.4 is
    next and requires owner review of authority and state-scope options before
    ADR-0006 or dependent implementation work.

- 2026-08-26 — Slice 2.4 — In Progress
  - Change: added proposed
    [`ADR-0006`](adr/ADR-0006-authority-state-scope-prediction-presentation.md)
    with separate proposal/validation/commit authority, state-scope,
    visibility, lifetime, prediction, presentation, and delegation concepts;
    five option sets; representative single-player, two-player, late-join,
    reconnect, contention, hostile-client, VR, delegation, script/admin/replay,
    and resync scenarios; an initial subsystem mapping; all ten domain GDR
    question sets; and named acceptance tests/demo steps. No production schema,
    state model, reducer, API, or multiplayer target was added.
  - Decisions: none accepted. The proposal recommends Option A for Decisions
    1–5: server command/reducer mutation; explicit scope and lifetime with
    global physical-world and per-player progression defaults; reversible local
    prediction with authoritative reconciliation; typed non-durable
    presentation samples; and no delegation in the first slice with future
    server-validated epoch leases. All five remain pending owner review.
  - Verification: `python -m unittest discover -s scripts/tests -v` passed all
    78 repository-owned tests. `python scripts/verify_vnext_baseline.py --index`
    accounted for exactly 57 intentional differences, including the proposed
    ADR, and verified all 43 dependency-declaration inputs. `python -m
    json.tool docs/vnext/BASELINE_PROVENANCE.json`, `git diff --cached
    --check`, a local-link scan across all 16 vNext Markdown files, and focused
    checks for proposed status, required ADR sections, five decision sets, and
    GDR-0001 through GDR-0010 coverage all passed. No production target changed,
    so a product build or runtime test is not applicable to this decision-packet
    step.
  - Owner review: decision packet prepared for the 2026-08-26 working session;
    explicit approval or amendment is pending. Slice 2.4 remains **In Progress**
    and dependent production work is not authorized.
  - Follow-ups: review Decisions 1–5 with the owner, amend or accept ADR-0006,
    run the approved scenario/document checks, and request completion acceptance
    before marking Slice 2.4 **Implemented**. Slices 2.5 and 2.6 remain gated.

- 2026-08-26 — Slice 2.4 — Implemented
  - Change: accepted
    [`ADR-0006`](adr/ADR-0006-authority-state-scope-prediction-presentation.md)
    and recorded the approved initial authority, state-scope, prediction,
    presentation-sample, and future-delegation framework. The ADR and project
    status now make Slice 2.5 the next eligible Phase 2 decision slice. No
    production schema, state model, reducer, API, or multiplayer target was
    added.
  - Decisions: the owner approved Option A for Decisions 1–5. Canonical mutation
    uses server commands/reducers; scope and lifetime are explicit with the
    recommended bounded defaults; prediction is reversible presentation;
    presentation samples are typed, bounded, latest-wins, and non-durable; and
    the first slice has no delegation while later delegation requires
    server-validated epoch leases. For Decision 1, the owner required a
    friends-server validation profile: retain structural/safety, authority,
    revision/idempotency/epoch, and basic domain checks, but do not add elaborate
    anti-cheat or duplicate complete OpenMW mechanics solely to police input.
  - Verification: `python -m unittest discover -s scripts/tests -v` passed all
    78 repository-owned tests. `python scripts/verify_vnext_baseline.py --index`
    accounted for exactly 57 intentional differences and verified all 43
    dependency-declaration inputs. `python -m json.tool
    docs/vnext/BASELINE_PROVENANCE.json`, `git diff --cached --check`, a
    local-link scan across all 16 vNext Markdown files, and focused assertions
    for accepted status/date, all five approvals, the friends-server condition,
    GDR-0001 through GDR-0010 coverage, Slice 2.4 **Implemented**, Phase 2 **In
    Progress**, Phase 3 **Not Started**, and the Slice 2.5 next pointer all
    passed. No production target changed, so a product build or runtime test is
    not applicable to this decision-only completion.
  - Owner review: explicit A/A/A/A/A approval received in the 2026-08-26 working
    session, including the Decision 1 validation-depth condition. The owner
    approved this architecture framework only; domain gameplay details remain
    gated by GDR-0001 through GDR-0010.
  - Follow-ups: none for Slice 2.4. Phase 2 remains **In Progress**; Slice 2.5
    must present OpenMW hook and patch-queue options for owner approval before
    ADR-0007 or dependent implementation work.

- 2026-08-26 — Slice 2.5 — In Progress
  - Change: inspected the pinned OpenMW 0.51 composition, frame ordering, input,
    world/player/cell operations, Lua synchronization/events, CMake target, and
    application-test seams; confirmed that the active OpenMW application tree
    still has no vNext delta; and added proposed
    [`ADR-0007`](adr/ADR-0007-openmw-hook-patch-queue-policy.md). The packet
    presents four decision sets, repository-backed scenarios, a bounded initial
    hook budget, patch-registry fields, upstreaming criteria, named acceptance
    tests, failure modes, and Phase 8.1's exact-hook review boundary. No adapter,
    hook, patch registry, CMake target, or production code was added.
  - Decisions: none accepted. The proposal recommends Option A for Decisions
    1–4: a native app-local C++ adapter/coordinator; one main-thread frame seam
    plus feature-specific semantic hooks only as approved behavior requires;
    normal buildable Git commits plus a machine-checked semantic patch registry;
    and upstreaming general engine seams/fixes while retaining multiplayer
    policy locally. All four remain pending owner review.
  - Verification: `python -m unittest discover -s scripts/tests -v` passed all
    78 repository-owned tests. `python scripts/verify_vnext_baseline.py --index`
    accounted for exactly 58 intentional differences and verified all 43
    dependency-declaration inputs. `python -m json.tool
    docs/vnext/BASELINE_PROVENANCE.json`, `git diff --cached --check`, a
    local-link scan across all 17 vNext Markdown files, and focused ADR-0007
    structure/status and Phase 2/3 gate assertions passed. Repository-source
    assertions verified input/Lua/state/mechanics/physics/world frame ordering,
    `main.cpp`/`Engine` composition, Lua's separate-thread and synchronized-
    update behavior, no active OpenMW application/test delta from pinned
    `f4bec4144`, and no existing adapter or client-session CMake target.
  - Owner review: decision packet prepared for the 2026-08-26 working session;
    explicit approval or amendment is pending. Slice 2.5 remains **In Progress**
    and no OpenMW hook or dependent adapter implementation is authorized.
  - Follow-ups: review Decisions 1–4 with the owner, amend or accept ADR-0007,
    rerun the accepted document/provenance gates, and request completion
    acceptance before marking Slice 2.5 **Implemented**. Slice 2.6 and all Phase
    3 production targets remain gated.

- 2026-08-26 — Slice 2.5 — Implemented
  - Change: accepted
    [`ADR-0007`](adr/ADR-0007-openmw-hook-patch-queue-policy.md) and recorded the
    approved native integration, bounded semantic-hook, buildable-commit/patch-
    registry, and local upstream-candidate preparation framework. No local
    upstream-preparation clone was created because no engine patch or upstream
    candidate exists yet; its path remains developer-local and it is created
    only if a real candidate needs a clean changeset.
  - Decisions: the owner approved Option A for Decisions 1–3 and amended Option
    A for Decision 4. The project will not proactively contact OpenMW or submit
    patches for now. A separate local OpenMW repository may later prepare clean
    general-purpose changesets, but it is disposable and cannot be a submodule,
    source of truth, build input, CI requirement, or replacement for vNext's
    commits, patch registry, and baseline manifest. Upstream contact requires a
    later explicit owner decision.
  - Verification: all 78 repository-owned Python tests pass; indexed baseline
    provenance passes with 59 intentional differences and 43 dependency inputs;
    JSON parsing/path ordering, all 40 local Markdown links, the accepted ADR-
    0007/local-repository constraints, Phase 2/3 status gates, the absence of a
    staged OpenMW source delta, and staged/unstaged diff checks pass. Source
    inspection reconfirmed the current OpenMW main-thread frame order, Lua's
    synchronized application point, and the monolithic `openmw-lib` composition.
  - Owner review: explicit approval received in the 2026-08-26 working session,
    including the Decision 4 local-repository/no-current-upstreaming amendment.
    Exact Phase 8 hook call sites and gameplay semantics remain separately
    gated.
  - Follow-ups: none for Slice 2.5. Phase 2 remains **In Progress**; Slice 2.6 is
    the next eligible decision slice and all Phase 3 production targets remain
    gated until the Phase 2 exit review.

- 2026-08-26 — Slice 2.6 — In Progress
  - Change: inspected the accepted deterministic, protocol-evolution, and
    engine-independence constraints and added proposed
    [`ADR-0013`](adr/ADR-0013-deterministic-simulation-protocol-compatibility.md).
    It presents five owner-reviewable option sets, adversarial scenarios, named
    acceptance tests, consequences, failure mitigations, and replacement
    triggers. No production target or runtime behavior was added.
  - Decisions: none accepted. The proposal recommends Option A for all five
    decisions: a 30 Hz logical tick with bounded catch-up; checked integer/fixed-
    point canonical numerics; writer-assigned eligible tick and ingress order;
    injected time plus versioned labeled random streams and canonical bytes;
    and a released current-plus-previous-minor compatibility window with
    bounded required/optional capability negotiation.
  - Verification: all 78 repository-owned Python tests pass; indexed baseline
    provenance passes with 59 intentional differences and 43 dependency inputs;
    JSON parsing/path ordering, all 40 local Markdown links, ADR-0013's proposed
    five-decision/owner-pending state, Slice 2.6/Phase 2/Phase 3 status gates,
    and staged/unstaged diff checks pass.
  - Owner review: pending explicit approval or amendment of Decisions 1–5.
    Exact gameplay-domain values remain GDR-gated and are not selected here.
  - Follow-ups: present the five decision sets to the owner. Slice 2.6 remains
    **In Progress**, Phase 2 remains **In Progress**, and Phase 3 production work
    remains gated.

- 2026-08-26 — Slice 2.6 — Implemented
  - Change: accepted
    [`ADR-0013`](adr/ADR-0013-deterministic-simulation-protocol-compatibility.md)
    and recorded the approved deterministic tick, canonical numeric, command-
    ordering, logical-time/randomness/canonical-encoding, and protocol-
    compatibility contracts. No production target or runtime behavior was
    added.
  - Decisions: the owner approved Option A for Decisions 1–5: 30 Hz with no more
    than four due ticks per scheduler pump; checked `1/1024`-unit fixed-point
    positions and 32-bit turn orientation; single-writer eligible-tick/ingress-
    ordinal order; injected logical time and versioned labeled random streams;
    and current-plus-previous-minor capability negotiation within one major.
  - Verification: all 78 repository-owned Python tests pass; indexed baseline
    provenance passes with 59 intentional differences and 43 dependency inputs;
    JSON parsing/path ordering, all 41 local Markdown links, ADR-0013's accepted
    five-decision state, Slice 2.6/Phase 2/Phase 3 status gates, and staged/
    unstaged diff checks pass.
  - Owner review: explicit approval received in the 2026-08-26 working session.
    Exact gameplay-domain values remain GDR-gated. This approval does not itself
    approve the Phase 2 exit review or Phase 3 kickoff.
  - Follow-ups: none for Slice 2.6. Every Phase 2 slice is implemented, but Phase
    2 remains **In Progress** pending its required explicit exit-gate review;
    Phase 3 remains **Not Started**.

- 2026-08-26 — Phase 2 — Implemented
  - Exit review: the owner explicitly approved the Phase 2 exit and movement to
    Phase 3 in the 2026-08-26 working session.
  - Gate evidence: ADR-0003 through ADR-0007 and ADR-0013 are accepted; the
    selected FlatBuffers and GameNetworkingSockets proof targets build on the
    supported Linux, Windows, and macOS matrices without OpenMW coupling; and
    ADR-0003 covers every threat category named by the exit gate.
  - Verification: all 78 then-existing repository-owned Python tests passed;
    indexed baseline provenance passed with 59 intentional differences and 43
    dependency inputs; JSON, all 41 local Markdown links, accepted-decision,
    phase-status, and diff checks passed.
  - Follow-ups: none. Later gameplay decisions remain GDR-gated, OpenMW hook
    sites remain Phase 8-gated, and the owner separately approved ADR-0014 for
    the Phase 3 kickoff.

### Phase 3 — Independent targets and test scaffold

Status: **In Progress**

Outcome: dependency boundaries are enforced by the build, and all later work has
deterministic testing, fuzzing, sanitizers, fault injection, and observability.

Depends on: Phase 2.

| Slice | Deliverable | Status | Completion evidence |
|---|---|---|---|
| 3.1 | Add empty protocol, transport, server-core, client-session, adapter, and test-support targets | **Implemented** | Accepted [`ADR-0014`](adr/ADR-0014-phase3-target-topology-boundary-enforcement.md) fixes the six-target topology; the independent and adapter graphs build, focused tests prove forbidden direct links/includes fail closed, and the owner accepted the implementation demo on 2026-08-26 |
| 3.2 | Add strong value types for IDs, ticks, sequences, revisions, command IDs, and authority epochs | **Implemented** | Accepted [`ADR-0015`](adr/ADR-0015-strong-value-types-identity-counter-policy.md) is implemented by ten explicit semantic types and independent compile-time/runtime boundary tests; the owner accepted the implementation demo on 2026-08-26 |
| 3.3 | Add canonical `CellId`, transform, velocity, and platform-neutral command/snapshot primitives | **Implemented** | Accepted [`ADR-0016`](adr/ADR-0016-canonical-spatial-command-snapshot-primitives.md) is implemented by engine-independent spatial/metadata values and a test-support-only byte round trip; the owner accepted the implementation demo on 2026-08-26 |
| 3.4 | Add injected clock, deterministic RNG, deterministic scheduler, and in-memory link | **Implemented** | Accepted [`ADR-0017`](adr/ADR-0017-deterministic-facilities-harness-boundaries.md) is implemented by passive server-core clock/scheduler/RNG facilities and a bounded test-support link/exact-trace harness; independent long-run, vector, isolation, backpressure, and repeatability contracts pass and the owner accepted the implementation demo on 2026-08-26 |
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

- 2026-08-26 — Slice 3.1 — In Progress
  - Change: added the accepted
    [`ADR-0014`](adr/ADR-0014-phase3-target-topology-boundary-enforcement.md)
    target topology, five engine-independent C++20 static libraries under
    `components/tes3mp`, one app-local `openmw_tes3mp_adapter`, stable `TES3MP::`
    aliases, a direct-dependency allowlist, forbidden-include-family checks, and
    focused fail-closed CMake graph tests. The adapter is not linked into the
    OpenMW executable and all translation units are behavior-free anchors.
  - Decisions: the owner approved Phase 3 kickoff and Option A: separate
    `tes3mp_protocol`, `tes3mp_transport`, `tes3mp_server_core`,
    `tes3mp_client_session`, and `tes3mp_test_support` targets plus the app-local
    adapter. GameNetworkingSockets and gameplay/OpenMW lifecycle behavior remain
    outside this slice.
  - Verification: all 83 repository-owned Python tests pass, including the five
    focused boundary tests. Those tests independently
    configure/build the five non-OpenMW targets, configure/build the adapter
    against an isolated `openmw-lib` leaf, reject an undeclared direct link,
    reject a forbidden include family, and reject a reverse production-to-test-
    support edge. The real `build/vnext-baseline` graph configures and builds
    `tes3mp_test_support` and `openmw_tes3mp_adapter` with MSVC/Ninja. The real
    legacy-exclusion gate passes across 3,767 tracked paths, 58 CMake files,
    1,239 compile commands, and 1,935 Ninja edges. Indexed provenance passes with
    73 intentional differences and 43 dependency inputs; JSON/path ordering,
    all 43 local Markdown links, ADR/status/graph assertions, and staged/
    unstaged diff checks pass.
  - Owner review: the architecture is approved; implementation-demo acceptance
    is pending before Slice 3.1 may become **Implemented**.
  - Follow-ups: run the full repository and provenance gates, present the target
    graph/failure evidence, and request demo acceptance. Slice 3.2 production
    work remains gated.

- 2026-08-26 — Slice 3.1 — Implemented
  - Change: retained the exact accepted ADR-0014 target graph and fail-closed
    checks demonstrated in the preceding in-progress note; no runtime behavior
    was added during acceptance.
  - Decisions: no amendment. The owner accepted the six-target Option A
    implementation as demonstrated.
  - Verification: all 83 repository-owned Python tests, focused independent/
    adapter configure-build checks, intentional forbidden-link/include failures,
    real MSVC/Ninja targets, real legacy exclusion, indexed provenance with 73
    intentional differences and 43 dependency inputs, all 43 local Markdown
    links, graph/status assertions, and diff checks pass.
  - Owner review: implementation-demo acceptance received in the 2026-08-26
    working session.
  - Follow-ups: none for Slice 3.1. Slice 3.2 is the next eligible slice; its
    strong-type architecture requires owner approval before public APIs land.

- 2026-08-26 — Slice 3.2 — In Progress
  - Change: inspected OpenMW's existing `Misc::StrongTypedef`, ESM identity,
    ordering, hashing, formatting, and overflow conventions; added proposed
    [`ADR-0015`](adr/ADR-0015-strong-value-types-identity-counter-policy.md)
    with five option sets, scopes, scenarios, acceptance tests, consequences,
    failure mitigations, and replacement triggers. No production header or
    strong value implementation was added.
  - Decisions: none accepted. The proposal recommends Option A for all five
    decisions: ten scoped `u64` semantic types; zero-invalid identities/one-based
    counters with tick zero valid; a project-owned explicit policy template;
    optional-returning checked advancement; and type-qualified debug formatting
    with codec/allocation separation.
  - Verification: all 83 repository-owned Python tests pass; indexed provenance
    passes with 74 intentional differences and 43 dependency inputs; JSON/path
    ordering, all 45 local Markdown links, ADR-0015's proposed five-decision/
    owner-pending state, Slice 3.1/3.2 and Phase 2/3 status assertions, and
    staged/unstaged diff checks pass.
  - Owner review: pending explicit approval or amendment of Decisions 1–5.
  - Follow-ups: present the five decisions to the owner. Slice 3.2 production
    work remains gated.

- 2026-08-26 — Slice 3.2 — In Progress
  - Change: accepted ADR-0015 and added `tes3mp/strong_value.hpp` plus
    `tes3mp/value_types.hpp` to `tes3mp_protocol`. The API defines ten scoped
    semantic `uint64_t` types, zero-invalid named factories except zero-valid
    `ServerTick`, declared counter `initial()`/checked `next()`, explicit raw
    access, same-type total ordering/hash, and stable `TypeName{decimal}` text.
    A standalone C++20 contract executable is composed through
    `tes3mp_test_support`; codecs, allocation, persistence, schemas, OpenMW, and
    gameplay behavior remain absent.
  - Decisions: the owner approved Option A for Decisions 1–5: the ten-type `u64`
    catalog and scopes, unrepresentable invalid/default state, project-owned
    explicit policy template, optional-returning no-wrap advancement, and stable
    type-qualified formatting with explicit external boundaries.
  - Verification: all 84 repository-owned Python tests pass, including six
    focused boundary tests. Their isolated CMake
    projects compile/link all independent targets and aliases, run the C++
    semantic contract, build the adapter leaf, and prove forbidden direct links
    and includes fail closed. The C++ contract contains compile-time negative
    construction/conversion/comparison/arithmetic checks, underlying size/
    alignment/trivial-copy assertions, plus runtime zero/start/order/hash/
    formatting/advance/exhaustion checks. The real MSVC/Ninja baseline graph
    builds and runs `tes3mp_protocol_tests_run`. Real legacy exclusion passes
    across 3,771 tracked paths, 58 CMake files, 1,240 compile commands, and 1,942
    Ninja edges. Indexed provenance passes with 77 intentional differences and
    43 dependency inputs; JSON/path ordering, all 45 local Markdown links,
    accepted ADR/catalog/isolation/status assertions, and staged/unstaged diff
    checks pass.
  - Owner review: ADR approval is recorded; implementation-demo acceptance is
    pending before Slice 3.2 may become **Implemented**.
  - Follow-ups: run all gates and present boundary/max-value evidence. Slice 3.3
    production work remains gated.

- 2026-08-26 — Slice 3.2 — Implemented
  - Change: retained the exact accepted ADR-0015 API and contract tests
    demonstrated in the preceding in-progress note; no API or runtime behavior
    changed during acceptance.
  - Decisions: no amendment. The owner accepted the ten-type Option A
    implementation as demonstrated.
  - Verification: all 84 repository-owned Python tests and six focused target-
    boundary tests pass. The real MSVC/Ninja graph builds and runs
    `tes3mp_protocol_tests_run`; real legacy exclusion passes across 3,771
    tracked paths, 58 CMake files, 1,240 compile commands, and 1,942 Ninja
    edges. Indexed provenance with 77 intentional differences and 43 dependency
    inputs, JSON/path ordering, all 45 local Markdown links, status/contract
    assertions, and staged/unstaged diff checks pass.
  - Owner review: implementation-demo acceptance received in the 2026-08-26
    working session.
  - Follow-ups: none for Slice 3.2. Slice 3.3 is the next eligible slice, and
    its spatial/state API decisions require owner approval before production
    headers land.

- 2026-08-26 — Slice 3.3 — In Progress
  - Change: inspected OpenMW 0.51 cell, worldspace, exterior-grid, position,
    and rotation representations plus the later protocol, server-core,
    content-identity, cell/interest, and movement gates. Added proposed
    [`ADR-0016`](adr/ADR-0016-canonical-spatial-command-snapshot-primitives.md)
    with five option sets, scenarios, acceptance tests, failure mitigations,
    and replacement triggers. No Slice 3.3 production header, codec, schema,
    state storage, reducer, adapter conversion, or gameplay behavior was added.
  - Decisions: none accepted. The proposal recommends Option A for Decisions
    1–5: a tagged context-scoped interior/exterior cell value; contextual
    absolute exterior coordinates; three-axis fixed-point transform values with
    fixed right-handed composition; linear velocity only; and value-only
    command/order metadata plus per-entity snapshot values.
  - Verification: all 84 repository-owned Python tests pass, including the six
    focused independent-target tests and the existing C++ strong-value
    contract. Indexed provenance passes with 78 intentional differences and 43
    dependency inputs; JSON/path ordering, all 55 local Markdown links,
    ADR-0016's proposed five-decision/owner-pending state, Slice 3.2/3.3 and
    Phase 3 status assertions, and staged/unstaged diff checks pass. A new
    product build/runtime test is not applicable because this step changes only
    the decision packet and production code remains gated.
  - Owner review: pending explicit approval or amendment of Decisions 1–5.
  - Follow-ups: present Decisions 1–5 to the owner, amend or accept ADR-0016,
    then implement only the approved Slice 3.3 value surface and independent
    contract tests.

- 2026-08-26 — Slice 3.3 — In Progress
  - Change: accepted ADR-0016 and added `CellSpaceId`, tagged interior/exterior
    `CellId`, signed fixed-point `Position3`, modular `Turn32`, three-axis
    `Orientation3`, `Transform`, and per-tick `LinearVelocity3` values to
    `tes3mp_protocol`. Added fully initialized client-command, optional entity-
    precondition, writer-admission, and authoritative spatial-snapshot value
    records. A bounded little-endian snapshot round trip exists only in
    `tes3mp_test_support`; it is not a protocol schema or wire contract.
  - Decisions: the owner approved Option A for Decisions 1–5: context-scoped
    tagged cells, contextual absolute exterior coordinates, right-handed
    `Rz * Ry * Rx` fixed-turn orientation, linear velocity only, and value-only
    metadata/snapshot records. Content mapping, final cell rules, gameplay
    validation, movement, codecs, collection budgets, reducers, delivery, and
    OpenMW conversions remain deferred to their owning gates.
  - Verification: all 84 repository-owned Python tests pass, including six
    focused boundary tests whose independent graph builds and runs both C++
    value-contract executables without OpenMW. The real MSVC/Ninja baseline
    graph configures, builds, and runs `tes3mp_protocol_tests_run`. Real legacy
    exclusion passes across 3,777 tracked paths, 58 CMake files, 1,242 compile
    commands, and 1,947 Ninja edges. Indexed provenance passes with 83
    intentional differences and 43 dependency inputs. JSON/path ordering,
    all 55 local Markdown links, accepted ADR/decision/status assertions, and
    staged/unstaged diff checks pass.
  - Owner review: ADR approval is recorded; implementation-demo acceptance is
    pending before Slice 3.3 may become **Implemented**.
  - Follow-ups: run all gates and present the interior/exterior, negative-grid,
    numeric-boundary, orientation-basis, command/admission-separation, malformed
    test-encoding, and round-trip evidence. Slice 3.4 remains gated.

- 2026-08-26 — Slice 3.3 — Implemented
  - Change: retained the exact accepted ADR-0016 value surface and independent
    contract tests demonstrated in the preceding in-progress note; no API or
    runtime behavior changed during acceptance.
  - Decisions: no amendment. The owner accepted the five-decision Option A
    implementation as demonstrated.
  - Verification: all 84 repository-owned tests, six focused independent-target
    checks, both C++ contracts in the real MSVC/Ninja graph, real legacy
    exclusion across 3,777 paths/58 CMake files/1,242 compile commands/1,947
    Ninja edges, indexed provenance with 83 intentional differences and 43
    dependency inputs, all 55 local Markdown links, ADR/status assertions, and
    staged/unstaged diff checks pass.
  - Owner review: implementation-demo acceptance received in the 2026-08-26
    working session.
  - Follow-ups: none for Slice 3.3. Slice 3.4 is the next eligible slice; its
    deterministic facility interfaces and target ownership require owner
    approval before production APIs land.

- 2026-08-26 — Slice 3.4 — In Progress
  - Change: inspected ADR-0013's approved tick/randomness contracts, ADR-0014's
    target graph, the current engine-independent API, and later scheduler,
    fault, protocol-session, server-core, transport, and checksum gates. Added
    proposed [`ADR-0017`](adr/ADR-0017-deterministic-facilities-harness-boundaries.md)
    with five option sets, exact recommended RNG derivation, scenarios,
    acceptance tests, failure mitigations, and replacement triggers. No Slice
    3.4 production facility, link, trace harness, or runtime behavior was added.
  - Decisions: none accepted. The proposal recommends Option A for Decisions
    1–5: project-owned monotonic instants/interface; passive bounded server-core
    scheduler; numeric-keyed versioned server-core RNG streams; a bounded test-
    support-only duplex byte link; and exact trace bytes plus a non-canonical
    test diagnostic digest.
  - Verification: all 84 repository-owned Python tests pass, including the six
    focused independent-target tests and both existing C++ value contracts.
    Indexed provenance passes with 84 intentional differences and 43 dependency
    inputs; JSON/path ordering, all 57 local Markdown links, ADR-0017's proposed
    five-decision/owner-pending state, Slice 3.3/3.4 and Phase 3 status
    assertions, and staged/unstaged diff checks pass. A new product build/runtime
    test is not applicable because this step changes only the decision packet
    and production code remains gated.
  - Owner review: pending explicit approval or amendment of Decisions 1–5.
  - Follow-ups: present Decisions 1–5 to the owner, amend or accept ADR-0017,
    then implement only the approved deterministic facility and harness APIs.

- 2026-08-26 — Slice 3.4 — Implemented
  - Change: accepted ADR-0017 and added the project-owned monotonic nanosecond
    instant/clock seam, passive rational 30 Hz scheduler with four-tick bounded
    catch-up, numeric-keyed SplitMix64/xoshiro256** V1 streams with snapshot/
    restore and unbiased bounded sampling, checked manual clock, independently
    bounded FIFO byte duplex, stable typed test trace, and explicitly non-
    canonical `TestTraceDigestV1`. Added two independent C++ contract
    executables and a fail-closed production-to-test-support include check. No
    wall-clock adapter, thread, reducer, gameplay behavior, fault policy,
    production transport interface, or canonical checksum was added.
  - Decisions: the owner approved Option A for ADR-0017 Decisions 1–5 without
    amendment: project-owned monotonic observation, passive server-core
    scheduler, numeric-keyed versioned server-core RNG, test-support-only
    bounded link, and exact trace bytes plus test-only diagnostic digest.
  - Verification: `python -m unittest discover -s scripts/tests -v` passes all
    85 repository-owned tests, including seven focused target-boundary tests.
    The isolated MSVC 19.44/Ninja graph builds and runs all four C++ contract
    executables. The deterministic contracts prove exact 1/30-second rational
    boundaries and 18,000 sequential ticks over ten irregularly advanced
    minutes; four-tick stall batches; backwards-clock, deadline-overflow, and
    tick-exhaustion errors; pinned SplitMix64/xoshiro/derivation vectors;
    stream isolation, restore, rejection sampling, and zero-bound behavior;
    independent message/byte budgets, FIFO boundaries, directional close; and
    two byte-identical 198-byte scripted traces with pinned diagnostic digest
    `75802a50e6b6fc66`. The real MSVC/Ninja baseline graph configures, builds,
    and runs `tes3mp_protocol_tests_run`; real legacy exclusion passes across
    3,791 tracked paths, 58 CMake files, 1,249 compile commands, and 1,960 Ninja
    edges. Indexed provenance passes with 97 intentional differences and 43
    dependency inputs; JSON/path ordering and staged/unstaged diff checks pass.
  - Owner review: ADR approval and implementation-demo acceptance received in
    the 2026-08-26 working session. The accepted demo is conditional on the
    recorded gates passing without behavior or boundary deviations; they pass.
  - Follow-ups: none for Slice 3.4. Slice 3.5 is the next eligible slice and
    still owns latency, loss, jitter, duplication, reordering, stall, and
    disconnect fault controls.

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

Outcome: the in-memory session runs over a maintained encrypted real transport
with application session identity, channel semantics, backpressure, and
telemetry hidden behind owned APIs.

Depends on: Phase 5.

| Slice | Deliverable | Status | Completion evidence |
|---|---|---|---|
| 6.1 | Wrap connection/listen/connect/disconnect/cancellation/lifecycle in an owned transport interface | **Not Started** | No selected-library type crosses the adapter boundary |
| 6.2 | Map reliable operations and latest-wins snapshots to explicit transport channels | **Not Started** | Loss/reorder tests prove snapshots do not head-of-line block behind operations |
| 6.3 | Implement required encryption, optional join-password authentication, resume-token handling, and credential redaction per ADR-0005 | **Not Started** | Encryption ordering, unencrypted-mode rejection, password failure/rate-limit, token replay, timeout, and redaction tests pass |
| 6.4 | Implement bounded queues, priority, rate limits, backpressure, and slow-peer eviction | **Not Started** | Flood/slow-reader tests remain within configured memory/work budgets |
| 6.5 | Implement network telemetry and stable disconnect/rejection reasons | **Not Started** | Per-channel sent/received/dropped/retransmitted/queued metrics are asserted in tests |
| 6.6 | Integrate real sockets into the deterministic fault harness | **Not Started** | Localhost tests exercise faults above/below the adapter as supported |
| 6.7 | Prove Linux, Windows, and macOS build/test support | **Not Started** | Transport integration CI is green on every supported desktop platform |

Exit gate:

- A real client and server complete the Phase 4 exchange over the approved
  encrypted transport on all supported desktop platforms.
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
| 7.0 | Review and obtain owner approval for GDR-0001 vertical-slice behavior scenarios | **Not Started** | Approved authority/scope/contention/reconnect decisions and named demo/tests are recorded |
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
| 23.6 | Review evidence with the owner and accept ADR-0012 with an explicit go/no-go result and support scope | **Not Started** | Owner-approved decision, selected route, risks, upstream candidates, and Phase 24 activation are recorded |

Exit gate:

- An on-device prototype resolves the major toolchain, OpenXR, rendering,
  lifecycle, networking, storage, and performance unknowns.
- ADR-0012 makes an evidence-backed, owner-approved go/no-go decision. A go decision activates
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

Implementation notes:

- This phase is conditional and remains **Not Started** unless ADR-0012 records a
  go decision. A no-go decision in Phase 23 does not block program completion.
- Reuse the established `vr_pose` capability and platform-neutral authoritative
  root/capsule. Quest is another provider/composition target, not another core.
- Device-specific work belongs in the platform/adapter layer and a bounded patch
  queue with upstream candidates tracked explicitly.

## Phase update procedure

Every pull request that advances a slice must update this document in the same
change or immediately linked follow-up:

1. Hold the phase kickoff and present required ADR/GDR decision packets before
   dependent production implementation begins. Record explicit owner approval;
   do not infer it from silence or from approval of unrelated work.
2. Change the slice to **In Progress** when the first non-disposable artifact
   lands, and change the phase to **In Progress** if needed.
3. Append an implementation note with the date, commit/PR, important design
   details, deviations from this plan, and exact verification evidence.
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

Use this note format:

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
