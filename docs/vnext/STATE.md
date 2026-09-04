# vNext working state

Updated: 2026-09-04

Read this first, then only the linked active material. The authoritative tracker
is the [implementation plan](IMPLEMENTATION_PLAN.md#phase-9--pc-vr-interoperability-gate).

## Current position

- Branch: `vnext`
- Phase 7: **Implemented**
- Phase 8: **Implemented**
- Phase 9: **In Progress** (Slice 9.3 implementation acceptance gate)
- Slice 9.1: **Implemented**
- Slice 9.2: **Implemented**
- Slice 9.3: **In Progress** (verified candidate pending owner acceptance)
- Slice 8.1: **Implemented**
- Slice 8.2: **Implemented**
- Slice 8.3: **Implemented**
- Slice 8.4: **Implemented**
- Slice 8.5: **Implemented**
- Slice 8.6: **Implemented**
- Slice 8.7: **Implemented**
- Governing decisions: [ADR-0007](adr/ADR-0007-openmw-hook-patch-queue-policy.md),
  [ADR-0008](adr/ADR-0008-pc-vr-fork-worktree-maintenance-policy.md),
  [ADR-0054](adr/ADR-0054-phase9-dual-engine-adapter-composition.md),
  [ADR-0055](adr/ADR-0055-phase9-optional-vr-pose-capability-and-schema.md),
  [ADR-0047](adr/ADR-0047-phase8-adapter-lifecycle-and-provider-boundary.md),
  [ADR-0048](adr/ADR-0048-canonical-revision-and-simulation-tick-separation.md),
  [ADR-0049](adr/ADR-0049-phase8-desktop-connection-composition.md),
  [ADR-0050](adr/ADR-0050-phase8-cell-and-remote-presentation.md),
  [ADR-0051](adr/ADR-0051-phase8-provisional-spatial-intent-concurrency.md),
  [ADR-0052](adr/ADR-0052-phase8-remote-motion-smoothing.md),
  [ADR-0053](adr/ADR-0053-phase8-desktop-reconnect-and-automation-composition.md),
  [GDR-0013](gdr/GDR-0013-phase8-cell-transition-presentation.md), and
  [GDR-0014](gdr/GDR-0014-phase8-desktop-movement-and-correction.md), and
  [GDR-0015](gdr/GDR-0015-phase8-remote-motion-presentation.md), and
  [GDR-0016](gdr/GDR-0016-phase8-disconnect-and-resume-presentation.md)
- Latest implementation commit: `c229946842` (`Add optional bounded VR pose
  protocol`)
- Latest accepted VR target: `6945e9bf48` (`Merge Slice 9.2 acceptance`)
- Latest VR implementation candidate: `eda058b92e` (`Merge Slice 9.3 optional VR
  pose protocol`)

## Working synopsis

The complete Slice 7.6 implementation and real two-client process proof pass;
the project owner accepted the implementation demo on 2026-09-02.
Same-pump closes use one bounded canonical batch; disconnect captures current
canonical progress; resume preserves identity, generation progression, revision,
and acknowledgements; exact expiration rejects the old token and fresh join
creates a new identity. Credentials remain in memory and absent from evidence.

Slice 7.7 layered matrix, thresholds, and RSS-window rule are approved. The
bounded named profiles, seeded 10,000-tick matrix, paced 32-cycle reconnect,
queue high-water/zero-drain proof, and 60-second two-client real-process soak
pass replay, fault, convergence, identity/progress, matching-view, bounded RSS,
and queue contracts. An owner-approved two-pass application pump now coalesces
intermediate same-tick views before transport; the deterministic regression and
optimized lifecycle demo pass without weakening client contradiction rejection.
The project owner accepted the Slice 7.7 implementation demo and Phase 7 exit
gate on 2026-09-02. The owner approved the exact Slice 8.1 P8-001 through P8-003
hook inventory on 2026-09-02. Slice 8.2 two-provider architecture and
correct-then-command frame order are approved. The owner then approved breaking
Option D after caller migration exposed split orchestration ownership. The
runtime now owns protocol/session progression, binding/application, command
sequencing, bounded queues, and failure closure. The headless proof caller and
the concrete OpenMW coordinator core use that one path. Configuration-gated
executable attachment is implemented. The owner-confirmed `/MD` Windows network
runtime policy is now applied to the exact locked OpenSSL, Protobuf/Abseil, GNS,
and c-ares inputs. Both dependency proofs pass, the manifest is regenerated, and
the real-network RelWithDebInfo `openmw.exe` links. Sanitized runtime failures
now appear through the approved normal OpenMW message-box path. The accepted
content-backed run exercises this complete composition.

Slice 8.4 is implemented. The owner approved the breaking corrective
package and P8-004 on 2026-09-03. Explicit snapshot target identity, tracked
cell-transition receipts with one coalesced deferred transition, explicit
fixture content mapping, typed provider failure closure, and renderer-only
remote avatar reconciliation are implemented. C-R1 replaces the proxy with a
first-class default-deny replica and the complete content-backed run passes.

Slice 8.5 is implemented under owner-approved ADR-0051/GDR-0014. The two
provisional spatial intent bodies treat entity revision as observed context
while retaining session, generation, entity, authority-epoch, identity,
sequence, and domain validation. Desktop action input maps deterministically to
bounded world velocity; one pending plus one latest desired intent bounds
traffic; newer same-cell snapshots correct local position exactly. No new
OpenMW hook was needed. Automated and accepted content-backed gates pass.

Slice 8.6 is implemented under owner-approved ADR-0052/GDR-0015. A
provider-owned four-sample client-local buffer now advances each frame from the
injected monotonic clock, interpolates two ticks behind, extrapolates at most
three ticks before holding, blends bounded corrections, hard-snaps larger or
discontinuous state, clears with observation lifetime, and emits adapter-owned
typed metrics. Automated and content-backed gates pass. No
protocol, canonical-state, authority, or OpenMW hook changed.

Slice 8.7 is implemented under ADR-0053/GDR-0016. The adapter owns one
bounded sequential-runtime reconnect supervisor, memory-only rotating
credentials, the original token deadline, one-second pre-auth retry pacing, and
a resumed complete-snapshot continuity barrier. Disconnect clears remotes and
suppresses multiplayer input until resume. A `BUILD_TESTING`-only typed driver
and external flow/reconnect/soak harness are implemented. Automated and
content-backed gates pass, including 32 resumes and the 60-second soak. No new
OpenMW hook, protocol, canonical state, authority, or persistence was added.

The owner approved the GDR-0013 Option A demo mapping: interior
`Seyda Neen, Census and Excise Office`, worldspace `sys::default`, and avatar
NPC `player`. Licensed Steam content later appeared at the default path. The
runner forwards explicit fallback archives and isolates proof phases across the
server disconnect grace. With the pinned dependency runtime paths,
`Morrowind.bsa`, and avatar `player`, the complete flow/reconnect/soak gate now
passes. No alternate content record or presentation behavior was chosen.

Phase 9 Slice 9.1 is implemented. Primary-source research found
the newest immutable OpenMW 0.51 VR target at `openmw-vr-0.51-rc1`, commit
`56a8e01390507375c9c2f2593e1c09e0df88c505`. Its exact GitHub Windows 2022 and
Ubuntu workflow passed, but retained artifacts have expired; its GitLab
pipeline passed Linux and Windows MSBuild jobs but failed Windows Ninja jobs.
The fork and desktop baseline diverge after common commit `e0efd3b1`; the VR
side has 197 commits / 341 changed paths and overlaps 10 registered P8 patch
paths. The owner approved ADR-0008 A/A/A. Read-only remote
`openmw-vr-upstream` has push disabled. Same-repository sibling worktree
`TES3MP-vr` uses `vnext-vr`; merge `0e8e85e0b5` records desktop and exact VR
parents. Proof commit `c590e81cc5` owns every local non-adapter OpenMW delta,
records all 10 P8 overlaps and six merge conflicts, and adds exact verified,
offline-reusable OpenXR acquisition. Fresh Windows RelWithDebInfo
`openmw_vr.exe` link and desktop regressions pass. The owner separately approved
temporary composition Option A: VR excludes desktop-only composition until
Slice 9.2. No authority, canonical state, protocol, pose, locomotion,
interaction, or gameplay behavior changed. The owner accepted the verified
implementation on 2026-09-03.

Slice 9.2 is implemented under approved ADR-0054 Option A. One engine-neutral
adapter/client-connection target is shared by both
executables; desktop concrete providers live in a separate target, while VR
links the same adapter and fails explicit multiplayer enable before credential,
transport, or runtime work until Slice 9.4 provides approved VR providers. The
desktop and VR executables link, focused contracts and registries pass, and the
shared implementation commit is in VR history. No new hook or gameplay/state
behavior was added. The owner accepted Slice 9.2 implementation on 2026-09-03.

The remote-avatar architecture decision was reopened. Legacy TES3MP used real
dynamic NPC actors and then patched dedicated-player exceptions through 16
OpenMW engine files. The superseded nominal renderer-only seam avoided world
registration but still initialized full NPC stats, spells, AI, inventory,
auto-equipment, PRNG state, and an inventory render listener. ADR-0050,
GDR-0013, and P8-004 require correction. The owner approved focused Option B
research and then explicitly approved package C-R1 on 2026-09-03: a
first-class, protocol-agnostic, default-deny replicated actor with rendering and
passive renderer-local neutral idle only. The exact replacement and complete
content-backed evidence pass. The owner accepted the implementation and Phase 8
exit evidence on 2026-09-03; Phase 9 is now eligible to begin.

The focused OpenMW 0.51 boundary audit and complete
[remote actor owner decision packet](REMOTE_ACTOR_OWNER_DECISION_PACKET.md) are
now prepared. The audit additionally confirms that normal scene insertion fans
an actor into mechanics, physics/navigation, Lua, and other gameplay systems;
the current proxy registers nested inventory pointers, consumes the world PRNG,
is focus/activation-visible through a renderer `PtrHolder`, and lacks the normal
mechanics animation controller. The packet compares all three models,
recommends default-deny package C-R1, names the exact replacement patch surface,
and defines 16 acceptance scenarios. C-R1 is approved and implemented with its
focused and real-content evidence. The owner accepted the implementation and
Phase 8 exit on 2026-09-03.

## Next-session handoff

Read this file and accepted
[ADR-0008](adr/ADR-0008-pc-vr-fork-worktree-maintenance-policy.md). Phase 8 is
accepted and complete at implementation `93690354d1`. Slice 9.1's approved
maintenance target and verified candidate are complete on `vnext-vr` at
`c590e81cc5`, with rehearsal-safety proof at `ff04803ec2`; owner implementation
acceptance is recorded. Slice 9.2's ADR-0054 Option A is implemented at
`54999379a2` and owner-accepted on the VR target at `6945e9bf48`. Slice 9.3's
approved ADR-0055 A/A/A/A/A package is implemented at `c229946842` and merged
into `vnext-vr` at `eda058b92e`. All applicable protocol, compatibility,
boundary, desktop/VR build, and repository gates pass. Obtain explicit owner
implementation acceptance before Slice 9.4. Retain desktop regression evidence and C-R1 boundaries;
do not choose VR authority, state, pose, locomotion, interaction, or gameplay
behavior implicitly.

## Active files

- Phase 9 tracker: [implementation plan](IMPLEMENTATION_PLAN.md#phase-9--pc-vr-interoperability-gate)
- Platform scope: `docs/vnext/adr/ADR-0002-platform-toolchain-policy.md`
- OpenMW hook/VR boundary: `docs/vnext/adr/ADR-0007-openmw-hook-patch-queue-policy.md`
- Accepted desktop baseline:
  `docs/vnext/REMOTE_ACTOR_OWNER_DECISION_PACKET.md` and
  [Phase 8 notes](IMPLEMENTATION_NOTES.md#phase-8--openmw-desktop-vertical-slice)
- Maintained patch/provenance policy: `docs/vnext/OPENMW_PATCH_REGISTRY.json`,
  `docs/vnext/BASELINE_PROVENANCE.json`, and
  `scripts/verify_openmw_patch_registry.py`
- Phase 9 decision packet:
  `docs/vnext/adr/ADR-0008-pc-vr-fork-worktree-maintenance-policy.md`
- Slice 9.2 decision packet:
  `docs/vnext/adr/ADR-0054-phase9-dual-engine-adapter-composition.md`
- Slice 9.3 decision packet:
  `docs/vnext/adr/ADR-0055-phase9-optional-vr-pose-capability-and-schema.md`
- VR target records (on `vnext-vr`): `docs/vnext/OPENMW_VR_PROVENANCE.json`,
  `docs/vnext/OPENMW_VR_PATCH_REGISTRY.json`, and
  `scripts/verify_openmw_vr_target.py`

## Last verified

Slice 9.3 reserves optional capability ID 1 and adds distinct bounded client and
server pose roots under a 1 KiB presentation frame class. The pure verifier-first
codecs own all values, require head tracking, preserve independently absent hands,
enforce root-relative numeric and sequence/generation bounds, accept verified
additive optional fields, and reject malformed classification or payload data.
Production capability offers, dispatch, transport mapping, persistence,
authority, and gameplay behavior remain absent. Commit `c229946842` passes the
full MSVC 19.51 protocol aggregate and all 145 repository Python tests; the exact
pinned FlatBuffers regeneration/proof passes with `flatc` 25.12.19. The same
commit is merged into `vnext-vr` at `eda058b92e`; its full protocol aggregate and
31 focused protocol/safety/proof tests pass, and the VR target verifier passes.
RelWithDebInfo `openmw.exe` and `openmw_vr.exe` link with SHA-256
`c53fa7538a1ffc8cdcd88c74ef47127d023c9a72884d9a2dbe820b1093a4fd15` and
`504910034174f9696488beb69949155cc76b68940871af94a303df377ff89e19`.
Slice 9.3 remains **In Progress** pending owner implementation acceptance.

Slice 9.2 separates the engine-neutral adapter/client connection from the
desktop provider target and links the same adapter/session lineage into desktop
and VR. MSVC 19.51 RelWithDebInfo `openmw.exe` and `openmw_vr.exe` link with
SHA-256 `c1d1240cee9ccbea75566f054d11befba22603efbebce5d96533ec5a1c114523`
and `74c7862860d0dfaa8f0db5451e6e705e5bd4c642914d453d75f1a3482ee7b9ce`.
Desktop Debug adapter contracts, all 141 desktop repository Python tests, the
desktop registry, VR Debug adapter contracts, the exact 11 VR tests, the VR
registry/verifier, shared-commit ancestry, and diff hygiene pass. Slice 9.2
is **Implemented** after owner acceptance on 2026-09-03.

Slice 9.1 pins the VR source to
`56a8e01390507375c9c2f2593e1c09e0df88c505`, the OpenXR-SDK tag used by that
tree to commit `1ca7bec6b531185530c9b4f1e7a50e1fd55e7641`, and its observed source
archive SHA-256 to
`afc4c7c59dc0e427f03fc655e84d4394eb2d6070630924a63e547e4055ab816d`.
The explicit merge `0e8e85e0b5` has desktop parent `411c93594b` and exact VR
parent `56a8e01390`; proof commit `c590e81cc5` records and verifies its complete
maintenance surface. Repository-owned acquisition passed online and offline
reuse, and the verified source rebuilt and linked MSVC 19.51 RelWithDebInfo
`openmw_vr.exe` with SHA-256
`283e8974ddee1c696381315f02138b624873896a2f7f28653f3a4dae4324bc1c`.
The VR six-test gate, desktop patch registry, and all 137 desktop Python tests
pass. The owner accepted Slice 9.1 implementation on 2026-09-03; it is
**Implemented**.

C-R1 is implemented without copying the legacy actor model. The old transient
proxy and NPC custom-data initializer delta are removed. A separate renderer
collection owns non-interactive replica animations behind adapter-owned RAII
handles; typed creation/update failures, field-exact idempotent snapshot
reconciliation, and bounded allocation-free desired-set scratch space remain at
the provider boundary. Focused engine, adapter, and source-boundary tests pass.

The `c-r1-final-5` real-content run passes two-client movement and interior /
exterior leave-return, exactly 32 sequential resumes, two successful 60-second
soak clients, the unchanged RSS-window rule, and zero final reliable/latest
queue depth. Final RSS medians were 536,868,864 and 540,196,864 bytes for the
clients and 13,791,232 bytes for the server, each within its observed reference
window range. The retained evidence contains no credential and no typed replica
failure. All 137 repository-owned Python tests, patch-registry verification,
staged 342-entry provenance with 69 dependency inputs, staged legacy exclusion,
and diff hygiene pass. The owner accepted this implementation and the shared
Slices 8.2–8.7 demonstration on 2026-09-03. The Phase 8 exit gate is closed;
Phase 9 Slice 9.1 is **Implemented**.

Slice 8.7 content-backed demo preflight rebuilt the MSVC 19.51 RelWithDebInfo
`tes3mp_server.exe` and passed all four focused desktop-harness Python contracts
on 2026-09-03. The matching `openmw.exe` is present. Licensed `Morrowind.esm`
was not found in the local Steam libraries, checked conventional install paths,
or an exact-name search across local C:, D:, F:, and G: drives. The owner
approved the exact Option A fixture mapping, but no content-backed result or
owner-demo claim was made.

Slice 8.7 focused MSVC 19.51 Debug reconnect/coordinator and token-recovery
contracts, full protocol aggregate, headless-client contracts, all 128
repository-owned Python tests, patch registry, staged baseline provenance with
331 intentional differences and 69 dependency inputs, staged legacy exclusion,
diff hygiene, and networking-enabled RelWithDebInfo `openmw.exe` links with
`BUILD_TESTING` both on and off pass on 2026-09-03. The content-backed desktop
harness and owner demo remain.

Slice 8.6 focused MSVC 19.51 Debug adapter/smoother contracts, full protocol
aggregate, headless-client contracts, all 124 repository-owned Python tests,
patch registry, staged baseline provenance with 325 intentional differences and
69 dependency inputs, staged legacy exclusion, diff hygiene, and the full
networking-enabled RelWithDebInfo `openmw.exe` build/link pass on 2026-09-03.

Slice 8.5's focused reducer/property and movement/coordinator contracts, full
protocol aggregate, headless-client contracts, adapter contracts, patch
registry, all 124 repository-owned Python tests, diff hygiene, staged baseline
provenance with 321 intentional differences and 69 dependency inputs, and full
RelWithDebInfo `openmw.exe` build/link pass on 2026-09-03.

Fresh MSVC 19.51 `/MD` GameNetworkingSockets and c-ares dependency proofs pass,
their exact transport manifest is regenerated, and direct object inspection
reports `MD_DynamicRelease`/`MSVCRT`. The full RelWithDebInfo OpenMW tree builds
with networking enabled and links `build/slice82-openmw-full/RelWithDebInfo/openmw.exe`;
the adapter, GNS transport, and credential-crypto contracts pass from that tree.
The same target recompiles and links with sanitized runtime failures presented
through the normal OpenMW message-box path. All 124 repository-owned Python
tests and patch-registry verification pass. The dependency/runtime link and
visible-status gates are closed. Earlier verified Phase 7 evidence remains:
MSVC headless runtime, server-app, full protocol, and pinned FlatBuffers proof.
The real-process lifecycle proof passes movement, convergence, stale-view,
32-cycle reconnect, expiration, fresh identity, and zero final queue depth. Staged
baseline provenance passes with 299 intentional differences and 69 dependency
inputs; staged legacy exclusion, patch-registry verification, and diff checks
pass. A fresh MSVC 19.51 full configuration confirms the pinned Bullet 3.25
bundle uses double precision, and the RelWithDebInfo `openmw.exe` build passes.

Refresh this file whenever implementation work changes the active slice,
governing decision, completed boundary, next work, relevant files, or evidence.
