# TES3MP vNext rolling implementation plan

Updated: 2026-09-06

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

### Phase 12 — manifest movement profiles and collision kernel seam

Status: **Ready**

The owner-approved A/A/A/A/A package is recorded in
[ADR-0059](adr/ADR-0059-phase12-production-movement-architecture.md) and
[GDR-0019](gdr/GDR-0019-phase12-production-movement-package.md). The unsafe
signed-64-bit version 1.2 input path is closed by a checked compatibility
envelope before player spatial mutation.

Add bounded manifest-scoped walk/run/sneak/jump profiles and an engine-neutral
content-collision query boundary. Implement the deterministic fixed-tick kernel
behind that boundary with tests proving that only server collision can author
the canonical root, invalid profiles fail closed, and Phase 11 exact-cell
interest/resync behavior is unchanged. Do not add client prediction,
presentation tuning, world objects, or persistence in this pass.

## Next

These are candidates, not locked slices:

1. Add the versioned locomotion input/schema and bounded client input history,
   then reconcile local prediction by replay without feedback into input.
2. Add bounded adaptive remote playback, canonical locomotion-driven animation,
   reliable one-shots, and stale-pose fallback without pose authority.
3. Run hardware/content-backed desktop and PC-VR captures, then ratify measured
   profile and presentation budgets.

The list is rewritten after each completed pass. New evidence may reorder,
combine, or remove items.

## Later

The remaining roadmap is kept at outcome level:

| Horizon | Intended outcome |
|---|---|
| Player foundation | Player lifecycle, content identity, and non-fixture world visibility |
| Movement | Bounded server-authored locomotion and collision, measured reconciliation, animation, and hardened optional pose presentation |
| World gameplay | Actors, cells, object state, inventory, equipment, and containers |
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
