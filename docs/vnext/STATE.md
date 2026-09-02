# vNext working state

Updated: 2026-09-02

Read this first, then only the linked active material. The authoritative tracker
remains the [implementation plan](IMPLEMENTATION_PLAN.md#phase-7--headless-end-to-end-multiplayer-slice).

## Current position

- Branch: `vnext`
- Phase 7: **In Progress**
- Slice 7.6: **In Progress**
- Governing decision: [ADR-0045](adr/ADR-0045-phase7-disconnect-resume-and-expiration-composition.md)
- Latest implementation commit: `5d680b9e78` (`Compose Phase 7 disconnect lifecycle`)

## Working synopsis

Prepared resume-token rotation and canonical disconnect/resume/expiration
transactions exist. Live joins register with the lifecycle coordinator. A live
transport close now atomically admits peer leave output, hides the canonical
session, and retains one bounded grace record.

Next eligible work is prepared resume authentication plus complete snapshot and
observation admission, followed by expiration pumping/output and the real
two-client disconnect/resume/expiration proof. Do not mark Slice 7.6
**Implemented** before its owner demo acceptance.

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
- Evidence: [Phase 7 notes](IMPLEMENTATION_NOTES.md#phase-7--headless-end-to-end-multiplayer-slice)

## Last verified

MSVC 19.51 server-app and lifecycle contracts, all 115 Python tests, staged
baseline provenance, staged legacy exclusion, and staged diff checks passed.

Refresh this file whenever implementation work changes the active slice,
governing decision, completed boundary, next work, relevant files, or evidence.
