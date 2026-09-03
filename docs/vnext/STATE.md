# vNext working state

Updated: 2026-09-02

Read this first, then only the linked active material. The authoritative tracker
remains the [implementation plan](IMPLEMENTATION_PLAN.md#phase-7--headless-end-to-end-multiplayer-slice).

## Current position

- Branch: `vnext`
- Phase 7: **In Progress**
- Slice 7.6: **Implemented**
- Slice 7.7: **In Progress**
- Governing decision: [ADR-0046](adr/ADR-0046-phase7-adverse-network-and-soak-matrix.md)
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
Owner implementation-demo and Phase 7 exit-gate reviews remain pending.

## Active files

- Server composition: `apps/tes3mp-server/server_application.{hpp,cpp}`
- Connection/auth flow: `apps/tes3mp-server/connection_session_coordinator.{hpp,cpp}`
- Join/output composition: `apps/tes3mp-server/authenticated_join_composition.{hpp,cpp}`
- Lifecycle core: `components/tes3mp/include/tes3mp/server_lifecycle.hpp`,
  `components/tes3mp/server_core/server_lifecycle.cpp`
- Resume tokens: `components/tes3mp/include/tes3mp/server_authentication.hpp`,
  `components/tes3mp/server_core/server_authentication.cpp`
- Focused tests: `apps/tes3mp-server/server_app_tests.cpp`,
  `components/tes3mp/tests/server_lifecycle_tests.cpp`,
  `components/tes3mp/tests/server_authentication_tests.cpp`
- Process proof: `apps/tes3mp-headless-client/main.cpp`,
  `apps/tes3mp-server/phase7_queue_telemetry.{hpp,cpp}`,
  `scripts/run_phase7_join_demo.py`, `scripts/run_phase7_soak.py`
- Adverse profiles: `components/tes3mp/include/tes3mp/test_support/phase7_adverse_profiles.hpp`,
  `components/tes3mp/tests/fault_injection_tests.cpp`
- Evidence: [Phase 7 notes](IMPLEMENTATION_NOTES.md#phase-7--headless-end-to-end-multiplayer-slice)

## Last verified

MSVC 19.51 server-app, authentication, lifecycle, complete protocol contracts,
the real-process queue-drain and 60-second soak proofs, and all 117 Python tests pass. Staged baseline
provenance passes with 290 intentional
differences and 69 dependency inputs; staged legacy exclusion and diff checks
pass.

Refresh this file whenever implementation work changes the active slice,
governing decision, completed boundary, next work, relevant files, or evidence.
