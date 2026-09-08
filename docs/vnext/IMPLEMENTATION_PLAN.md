# TES3MP vNext rolling implementation plan

Updated: 2026-09-07

This is the authoritative implementation tracker. It deliberately plans one
useful pass at a time instead of pre-authoring every future slice. Detailed
historical evidence remains in [IMPLEMENTATION_NOTES.md](IMPLEMENTATION_NOTES.md),
and the concise handoff is [STATE.md](STATE.md).

## How work rolls

Work has three horizons:

- **Now** — one active pass with a concrete outcome and proportionate proof.
- **Next** — the next one to three passes, ordered but still easy to reshape.
- **Later** — outcome-level direction only. It is not an implementation promise
  and has no predeclared micro-slices.

A pass is complete when its behavior works, relevant tests pass, important
failure paths are bounded, and the handoff is updated. We may split a pass when
implementation reveals a real boundary. We do not create slices merely to
schedule research, record routine choices, or manufacture approval checkpoints.

ADRs and GDRs are reserved for decisions that are expensive to reverse or that
change architecture, authority, durable state, compatibility, security, or
player-facing semantics. Routine implementation judgment stays in the active
pass and its tests. A blocked decision becomes a short options note only when
the decision is actually blocking progress.

Verification scales with risk:

- focused unit or contract tests while iterating;
- affected target builds and integration tests before completing a pass;
- full repository, platform, provenance, and soak gates at milestone or release
  boundaries, or when a change can plausibly affect them.

## Now

### Phase 14 — interactive-object command ordering and authoritative outcomes

Status: **Complete — ready for commit**

Baseline replication and most server composition are implemented and verified:
- Pinned FlatBuffers schemas (`reliable_interactive_object_interest_baseline.fbs`, `client_interact_object_command.fbs`) and size-prefixed verified wire codecs.
- Negotiated `interactiveObjectReplicationCapability()`.
- Exact-cell baseline projection (`projectInteractiveObjectInterestBaseline`, `projectCellInteractiveObjectBaseline`).
- Capability-gated client interaction decoding and submission to the bounded canonical command intake in `ConnectionSessionCoordinator`.
- Server application wiring `CanonicalInteractiveObjectWorld` and `InteractiveObjectCatalog` into `ServerApplicationWiring`.
- Optional bounded production object content loading, with capability advertisement only when the catalog and initial world load successfully.
- Server tick execution applying object mutations in place within one prepared batch snapshot, in shared command order, and atomically delivering updated cell baselines.
- Cell transitions projecting and atomically admitting object baselines alongside player observations and actor baselines.
- Scenario 11 (unloaded cell preservation without ticking) and Scenario 12 (late join and resync complete baseline) verified in `server_app_tests.cpp`.

Closure:
- Interaction commands use the canonical command-ordering/idempotency boundary, including finalized session sequence and command-ID history.
- Teleport-door player replacement and trap outcomes are composed into the same prepared command transaction; typed outcomes are included in canonical sink publications.
- Canonical player/object state commits only after affected outbound publications are admitted. Focused reducer and server-app tests cover mixed ordering, outcomes, and rollback.
- Network key-unlock intent is rejected until Phase 15 can prove key ownership from canonical inventory state; the reducer retains an explicit verified-key integration seam.

## Next

These are candidates, not locked slices:

1. Wire client session reception of `ReliableInteractiveObjectInterestBaseline`, client-local visual door swing animation, and OpenMW renderer scene node rotation.
2. Revisit Phase 12 PC-VR hardware capture before Phase 22 stabilization if
   hardware remains unavailable during Phase 14 presentation work.
3. Phase 15 — inventory, containers, and equipment baseline replication, including authoritative key ownership for object unlock commands.
4. Phase 16 — combat, stats, magic, death, and resurrection.

The list is rewritten after each completed pass. New evidence may reorder,
combine, or remove items.

## Later

The remaining roadmap is kept at outcome level:

| Horizon | Intended outcome |
|---|---|
| Player foundation | Player lifecycle, content identity, and non-fixture world visibility |
| Movement | Measured correction/remote playback, animation, and hardened optional pose presentation |
| World gameplay | Interactive object state, inventory, equipment, and containers |
| Rules | Combat, magic, death, respawn, dialogue, quests, factions, reputation, and time/weather |
| Platform services | Scripting, persistence/replay, administration, moderation, observability, and security hardening |
| Release | Cross-platform packaging, upgrades, performance/soak evidence, and migration documentation |
| Optional expansion | Standalone Quest feasibility only after the desktop/PC-VR release boundary |

The prior numbered roadmap roughly mapped these outcomes to Phases 10–24. Those
numbers remain useful historical labels in ADRs, GDRs, and implementation notes,
but they no longer force a predetermined sequence of micro-slices.

## Completed foundation

| Milestone | Status | Result |
|---|---|---|
| Phases 0–6 | **Complete** | Baseline, architecture, protocol, transport, canonical server core, and reusable client session |
| Phase 7 | **Complete** | Headless authenticated join, observation, movement, disconnect/resume, fault, and soak flow |
| Phase 8 | **Complete** | OpenMW desktop vertical slice with semantic input and renderer-only remote actors |
| Phase 9 | **Complete** | Shared desktop/PC-VR client composition, optional bounded pose protocol, isolated pose transport, authority-checked relay, sampled OpenXR input, and desktop safe fallback |
| Phase 10 | **Complete** | Durable player credential identity and exact content-manifest negotiation |
| Phase 11 | **Complete** | Exact bounded cell catalog, server-owned same-cell interest, reliable membership baseline, and bounded authenticated resync |
| Phase 12 discovery | **Complete** | Repository-backed movement trace, measured fixture bounds, production seams, named proofs, and owner decision options |
| Phase 12 evidence | **Complete** | Bounded identity-free movement observations and comparable deterministic desktop/PC-VR direct, jitter, loss, and stall captures |
| Phase 12 decision/safety | **Complete** | Approved production movement package plus a reducer-owned legacy velocity envelope that rejects unsafe input before spatial mutation |
| Phase 12 profiles/kernel | **Complete** | Manifest locomotion profiles and a deterministic collision-query movement kernel with a bounded version 1.2 compatibility path |
| Phase 12 input/replay | **Complete** | Additive version 1.3 locomotion semantics, ordered bounded input, and shared replay-only local reconciliation |
| Phase 12 collision provider | **Complete** | Manifest-bound static collision content with complete exact-cell coverage, bounded swept root volumes, and fail-closed server composition |
| Phase 12 remote presentation | **Complete** | Fixture-bounded adaptive playback, additive canonical locomotion snapshots, shared remote animation selection, reliable one-shot separation, and stale optional-pose fallback |
| Phase 13 actor core | **Complete** | Manifest-bound actor identity/catalog, separate canonical actor world, server-only bounded idle/travel/wander simulation, checked collision, inactive-cell freeze, and atomic failure |
| Phase 13 actor composition/replication | **Complete** | Required bounded actor content, fixed-tick server composition, optional separate actor replication, exact-cell lifecycle completion, and shared renderer-only desktop/PC-VR presentation |
| Phase 13 lifecycle closure | **Complete** | Representative-content two-client cell leave/re-entry, four resumes, authenticated resync/rejoin, stable actor identity/revisions, and cross-lane ordering fixes |
| Phase 13 live presentation follow-up | **Complete** | Canonical-only actor translation, deterministic bounded multi-player spawn points, and representative interior demo placement |
| Phase 14 discovery | **Complete** | OpenMW door/lock/trap/activation trace, legacy requirements evidence, clean lane separation, named proofs, and owner decision options |
| Phase 14 canonical core | **Complete** | Manifest-bound catalog, immutable canonical object world, reach/cell/revision/key validation, and discrete state outcomes |
| Phase 14 replication/server composition | **Complete** | Capability-gated wire protocol, exact-cell baselines, shared canonical command ordering/idempotency, atomic player/object commit, teleport replacement, and trap outcome publication |

### Phase 9 completion record

Phase 9 was closed as three practical passes rather than continuing the old
9.4–9.7 ceremony:

1. **VR connection baseline** — the VR executable uses a distinct provider leaf
   that reuses the shared semantic input and canonical presentation source.
2. **Pose loop** — capable products negotiate `vr_pose`; clients sample at
   20 Hz independently of render/tracking rate; pose has a separate coalescing
   unreliable lane; the server validates session, generation, entity, authority
   epoch, sequence, and cell before relaying.
3. **Interoperability closure** — desktop clients safely accept/ignore optional
   articulated pose while continuing canonical root presentation; clients
   without the capability remain unchanged; desktop and VR targets share the
   protocol, runtime, and server path with no server platform branch.

Shared implementation commit: `617b8bc3a1`.
VR provider implementation commit: `af52e60659`.

Articulating remote skeleton head/hands, pose loss blending, compression
hardening, and gameplay reach remain later movement/presentation work. Pose is
ephemeral and cannot author canonical movement or gameplay.

## Invariants

- The server is the sole writer of durable canonical state.
- Network and script inputs are bounded and validated before mutation.
- Reliable operations, canonical latest-wins snapshots, and ephemeral
  presentation samples cannot replace one another in queues.
- Pose and animation degradation cannot block or author gameplay.
- Protocol, client-session, and server-core targets contain no OpenMW, renderer,
  OpenXR, platform, or transport-library types.
- Desktop and VR produce the same semantic commands; fork-specific tracking is
  confined to the VR provider leaf.
- Failed decoding, validation, queue admission, lifecycle transitions, and
  persistence operations do not partially commit.
- Credentials and sensitive user data never enter logs, metrics, fixtures, or
  retained evidence.

## Durable records

Accepted architectural and gameplay decisions remain under [adr/](adr/) and
[gdr/](gdr/). They describe durable constraints, not day-to-day scheduling.
The OpenMW and OpenMW-VR patch/provenance registries remain machine-verified.
Chronological commands, hashes, historical owner reviews, and old slice evidence
remain in [IMPLEMENTATION_NOTES.md](IMPLEMENTATION_NOTES.md); that file is an
archive, not a second status tracker.
