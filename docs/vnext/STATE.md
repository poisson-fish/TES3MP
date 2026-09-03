# vNext working state

Updated: 2026-09-02

Read this first, then only the linked active material. The authoritative tracker
remains the [implementation plan](IMPLEMENTATION_PLAN.md#phase-8--openmw-desktop-vertical-slice).

## Current position

- Branch: `vnext`
- Phase 7: **Implemented**
- Phase 8: **In Progress**
- Slice 8.1: **Implemented**
- Slice 8.2: **In Progress** (canonical-revision migration and OpenMW composition remain)
- Governing decisions: [ADR-0007](adr/ADR-0007-openmw-hook-patch-queue-policy.md),
  [ADR-0047](adr/ADR-0047-phase8-adapter-lifecycle-and-provider-boundary.md),
  [ADR-0048](adr/ADR-0048-canonical-revision-and-simulation-tick-separation.md)
- Latest implementation commit: `bf9a7bbd3f` (`Migrate headless client to bounded runtime`)

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
correct-then-command frame order are approved. The reusable client-session
runtime now owns bounded typed inbound drain and queued outbound flush with
failure closure, and the headless executable uses that boundary. Provider
contracts, the optional engine coordinator seam, and semantic patch registry
are implemented. The caller migration reproduced the different-publication,
same-tick contradiction; the owner-approved breaking ADR-0048 domain/wire
migration is now the next work before OpenMW composition.

## Active files

- Governing policy: `docs/vnext/adr/ADR-0007-openmw-hook-patch-queue-policy.md`
- Adapter anchor/target: `apps/openmw/tes3mp/{adapter.cpp,CMakeLists.txt}`
- Reusable runtime: `components/tes3mp/{include/tes3mp/client_session_runtime.hpp,client_session/client_session_runtime.cpp,tests/headless_client_tests.cpp}`
- Provider/coordinator contracts: `apps/openmw/tes3mp/{providers.hpp,engine_coordinator.hpp,adapter_tests.cpp}`
- Candidate lifecycle/frame seam: `apps/openmw/engine.{hpp,cpp}`
- Executable/target composition: `apps/openmw/{main.cpp,CMakeLists.txt}`
- Patch registry: `docs/vnext/OPENMW_PATCH_REGISTRY.json`,
  `scripts/verify_openmw_patch_registry.py`
- Evidence: [Phase 8 notes](IMPLEMENTATION_NOTES.md#phase-8--openmw-desktop-vertical-slice)

## Last verified

Fresh MSVC 19.51 Debug headless runtime and full protocol contracts, the GNS
headless/server build, and all 120 Python tests pass. The real-process proof
reproduces the expected ADR-0048 same-tick contradiction gate. Staged
baseline provenance passes with 299 intentional differences and 69 dependency
inputs; staged legacy exclusion, patch-registry verification, and diff checks
pass. A fresh MSVC 19.51 full configuration confirms the pinned Bullet 3.25
bundle uses double precision, and the RelWithDebInfo `openmw.exe` build passes.

Refresh this file whenever implementation work changes the active slice,
governing decision, completed boundary, next work, relevant files, or evidence.
