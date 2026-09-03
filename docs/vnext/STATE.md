# vNext working state

Updated: 2026-09-02

Read this first, then only the linked active material. The authoritative tracker
remains the [implementation plan](IMPLEMENTATION_PLAN.md#phase-8--openmw-desktop-vertical-slice).

## Current position

- Branch: `vnext`
- Phase 7: **Implemented**
- Phase 8: **In Progress**
- Slice 8.1: **Implemented**
- Slice 8.2: **In Progress** (owner demo remains)
- Slice 8.3: **In Progress**
- Governing decisions: [ADR-0007](adr/ADR-0007-openmw-hook-patch-queue-policy.md),
  [ADR-0047](adr/ADR-0047-phase8-adapter-lifecycle-and-provider-boundary.md),
  [ADR-0048](adr/ADR-0048-canonical-revision-and-simulation-tick-separation.md),
  [ADR-0049](adr/ADR-0049-phase8-desktop-connection-composition.md)
- Latest implementation commit: `eafc110d16` (`Unify client runtime orchestration`)

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
executable attachment is implemented. The owner confirmed one `/MD` Windows
network runtime policy; the pinned OpenMW dependency bundle must be rebuilt from
its current `/MT` profile before the real-network executable proof can pass.
Visible status UI and the owner demo remain.

## Active files

- Governing policy: `docs/vnext/adr/ADR-0007-openmw-hook-patch-queue-policy.md`
- Adapter coordinator/target: `apps/openmw/tes3mp/{adapter.hpp,adapter.cpp,CMakeLists.txt}`
- Desktop composition: `apps/openmw/tes3mp/{desktop_connection.hpp,desktop_connection.cpp}`
- Reusable runtime: `components/tes3mp/{include/tes3mp/client_session_runtime.hpp,client_session/client_session_runtime.cpp,tests/headless_client_tests.cpp}`
- Provider/coordinator contracts: `apps/openmw/tes3mp/{providers.hpp,engine_coordinator.hpp,adapter_tests.cpp}`
- Candidate lifecycle/frame seam: `apps/openmw/engine.{hpp,cpp}`
- Executable/target composition: `apps/openmw/{main.cpp,CMakeLists.txt}`
- Desktop options: `apps/openmw/options.cpp`
- Patch registry: `docs/vnext/OPENMW_PATCH_REGISTRY.json`,
  `scripts/verify_openmw_patch_registry.py`
- Evidence: [Phase 8 notes](IMPLEMENTATION_NOTES.md#phase-8--openmw-desktop-vertical-slice)

## Last verified

Fresh MSVC 19.51 Debug OpenMW adapter contracts, all 120 Python tests, patch
registry verification, and diff checks pass for the current slice. RelWithDebInfo
OpenMW without networking builds. The GNS-backed adapter compiles, but final
link verification is pending an approved `/MD` rebuild of the pinned OpenMW
dependency bundle; the installed `/MT` bundle fails the expected runtime-library
check and is not passing evidence. Earlier verified Phase 7 evidence remains:
MSVC headless runtime, server-app, full protocol, and pinned FlatBuffers proof.
The real-process lifecycle proof passes movement, convergence, stale-view,
32-cycle reconnect, expiration, fresh identity, and zero final queue depth. Staged
baseline provenance passes with 299 intentional differences and 69 dependency
inputs; staged legacy exclusion, patch-registry verification, and diff checks
pass. A fresh MSVC 19.51 full configuration confirms the pinned Bullet 3.25
bundle uses double precision, and the RelWithDebInfo `openmw.exe` build passes.

Refresh this file whenever implementation work changes the active slice,
governing decision, completed boundary, next work, relevant files, or evidence.
