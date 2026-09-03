# vNext working state

Updated: 2026-09-02

Read this first, then only the linked active material. The authoritative tracker
remains the [implementation plan](IMPLEMENTATION_PLAN.md#phase-8--openmw-desktop-vertical-slice).

## Current position

- Branch: `vnext`
- Phase 7: **Implemented**
- Phase 8: **In Progress**
- Slice 8.1: **In Progress** (hook inventory and owner review)
- Governing decision: [ADR-0007](adr/ADR-0007-openmw-hook-patch-queue-policy.md)
- Latest implementation commit: `e01967ee8e` (`Prevent contradictory same-tick snapshots`)

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
gate on 2026-09-02. Phase 8 Slice 8.1 is now active; exact OpenMW hook paths and
call sites require inventory and owner approval before implementation.

## Active files

- Governing policy: `docs/vnext/adr/ADR-0007-openmw-hook-patch-queue-policy.md`
- Adapter anchor/target: `apps/openmw/tes3mp/{adapter.cpp,CMakeLists.txt}`
- Candidate lifecycle/frame seam: `apps/openmw/engine.{hpp,cpp}`
- Executable/target composition: `apps/openmw/{main.cpp,CMakeLists.txt}`
- Evidence: [Phase 8 notes](IMPLEMENTATION_NOTES.md#phase-8--openmw-desktop-vertical-slice)

## Last verified

MSVC 19.51 server-app, authentication, lifecycle, complete protocol contracts,
the real-process queue-drain and 60-second soak proofs, and all 117 Python tests pass. Staged baseline
provenance passes with 290 intentional
differences and 69 dependency inputs; staged legacy exclusion and diff checks
pass.

Refresh this file whenever implementation work changes the active slice,
governing decision, completed boundary, next work, relevant files, or evidence.
