# TES3MP vNext rolling implementation plan

Updated: 2026-09-05

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

### Phase 11 — production cells, interest, and resync decision

Status: **Awaiting owner approval**

The discovery pass is complete. It found one end-to-end fixture path:

1. OpenMW reports `Scene::hasCellChanged`; the desktop provider maps only one
   interior record or one exterior worldspace into `FixtureCellTransition`.
2. The client queues one reliable transition plus one coalesced deferred value.
   Server dispatch validates session/generation and the reducer admits only the
   manifest's interior or exterior `(0,0)` cell before changing canonical state.
3. Server-app projection recomputes same-cell equality, emits reliable deltas and
   a complete latest-wins view, admits output before the canonical commit, and the
   client intersects both feeds for presentation.
4. Join and resume synthesize ordinary `Enter` batches. The only resync API returns
   an unfiltered internal publication; no wire request or explicit completion
   marker exists.

Approval choices:

1. **Cell catalog — A recommended:** a bounded, sorted manifest catalog declares
   up to 256 unique cell spaces and 4,096 exact allowed cells. Interior IDs and
   exterior worldspace/grid tuples are explicit. The OpenMW leaf owns a complete
   injective ID-to-`ESM::RefId` map, rejects missing records and case-insensitive
   record collisions, and sends no record names or paths. **B:** exterior bounding
   rectangles; smaller authoring data, but admits holes and arbitrary empty cells.
   **C:** any signed-32-bit grid in a known worldspace; simplest, but too broad for
   hostile client proposals.
2. **Interest — A recommended:** active players observe exactly the active players
   in their canonical `CellId`; the server alone derives membership. This preserves
   current visible behavior while removing fixture identity. **B:** include adjacent
   exterior grids; useful at cell edges, but selects a new visibility radius and
   inactive-cell presentation policy. **C:** client-selected sets; rejected because
   clients would choose authoritative visibility.
3. **Baseline/resync — A recommended:** add a dedicated reliable complete-membership
   baseline carrying session/generation, canonical revision, state version, tick,
   and a sorted bounded member set. Initial join, resume, and an authenticated,
   one-pending-per-generation metadata-only resync request produce that baseline
   plus a latest snapshot. Readiness/resync completes only after both the baseline
   and a snapshot at or after its revision; applying a baseline atomically replaces
   membership. **B:** mark the existing delta batch and match it to an exact
   latest-wins snapshot; smaller schema growth, but that snapshot may be coalesced
   before delivery. **C:** infer completion from snapshot contents or silence;
   rejected because loss and empty sets are ambiguous.

Recommended bounded implementation: record A/A/A in GDR-0003 and a compact ADR,
replace the scalar manifest/mapping and every production `Fixture*` seam, add the
baseline and metadata-only resync roots, and compose them through join, resume,
runtime resync, client readiness, and presentation replacement. Keep the existing
256-player/view/change bounds and 16/64 KiB reliable/latest payload budgets; the
membership-only baseline fits the reliable budget without moving spatial samples
off the latest-wins lane.

Named proof: catalog empty/overflow/duplicate/kind/mapping/coordinate failures; multiple
interiors and negative/boundary exterior grids; unknown-cell atomic rejection;
same-cell-only membership; baseline-before/after-snapshot ordering; quiet-stream
non-completion; duplicate/stale/contradictory baseline handling; wrong-generation,
state-upload, and resync-flood rejection; join/resume/disconnect/expiration
reconstruction; and encoding/queue failure without partial commit.

Compatibility: this deliberately advances protocol 1.1 to 1.2 and replaces the
single-cell server/client configuration. The player registry remains keyed by the
same exact manifest ID and needs no format migration. Canonical authority and
checksum encoding do not change.

Limits: no movement tuning, collision, prediction, teleport legality, persistence,
world-object streaming, adjacent-cell visibility, or general character state.

## Next

These are candidates, not locked slices:

1. Record the approved Phase 11 cell/interest/resync package and implement it.
2. Run focused protocol/server/client/adapter tests plus the lifecycle integration
   proof with multi-interior and negative-grid transitions.
3. Reassess production movement after production cell and visibility semantics
   exist.

The list is rewritten after each completed pass. New evidence may reorder,
combine, or remove items.

## Later

The remaining roadmap is kept at outcome level:

| Horizon | Intended outcome |
|---|---|
| Player foundation | Player lifecycle, content identity, and non-fixture world visibility |
| Movement | Production movement, animation, correction, and hardened optional pose presentation |
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
