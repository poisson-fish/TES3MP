# TES3MP engine-independent components

This directory owns the reusable TES3MP vNext libraries. They are separate from
OpenMW's monolithic `components` target and must remain buildable without OpenMW,
rendering, SDL, OSG, platform, or public GameNetworkingSockets types.

Application composition lives outside this directory:

- `apps/tes3mp-server` owns the dedicated-server process, strict configuration,
  and runtime composition.
- `apps/tes3mp-headless-client` owns the thin scripted client process.
- `apps/openmw/tes3mp` owns the OpenMW adapter.

## Target graph

```text
tes3mp_protocol
  ├──> tes3mp_transport
  ├──> tes3mp_server_core
  └──> tes3mp_client_session <── tes3mp_transport

tes3mp_test_support -> protocol + transport + server_core + client_session
openmw_tes3mp_adapter -> client_session + openmw-lib
```

Arrows point from a dependency to a target that consumes it. The server core
may depend on protocol value contracts but not on transport. The client session
composes protocol and transport. Test support may depend on all four production
libraries; production targets must never depend on test support.

## Responsibilities

- `protocol/` owns bounded framing, version and capability negotiation,
  authentication, reliable operations, snapshots, observations, generated
  FlatBuffers code, schemas, and verifier-first codecs.
- `transport/` owns the project-defined lifecycle and channel boundary plus the
  private GameNetworkingSockets adapter.
- `server_core/` owns deterministic scheduling, authentication policy,
  canonical state, command intake and reduction, publication, lifecycle,
  checksums, resynchronization, and typed sink boundaries.
- `client_session/` owns the reusable caller-pumped client session and headless
  façade.
- `test_support/` owns deterministic clocks, links, fault injection, scripted
  clients, recorders, and simulation helpers. It contains no production API.
- `tests/` owns focused contracts and fuzz corpora for these boundaries.

Library types from FlatBuffers, GameNetworkingSockets, OpenSSL, OpenMW, and
platform APIs must remain behind their owning private adapters. Public headers
expose project-owned values only.

## Boundary enforcement

`cmake/TES3MPVerifyTargetBoundaries.cmake` checks every direct link against the
ADR-0014 allowlist and rejects forbidden include families in independent
sources. `scripts/tests/test_tes3mp_target_boundaries.py` proves that both checks
fail closed with temporary miniature CMake projects.

The standalone presets in this directory build the independent libraries and
their contract executables without OpenMW. Runtime-safety entry points are:

```sh
python scripts/run_tes3mp_runtime_safety.py --profile asan-ubsan --fuzz-seconds 30
python scripts/run_tes3mp_runtime_safety.py --profile tsan
```

For the complete local workflow, use
[`docs/vnext/LOCAL_BASELINE_BUILD.md`](../../docs/vnext/LOCAL_BASELINE_BUILD.md).
For current phase and slice status, use the
[`implementation plan`](../../docs/vnext/IMPLEMENTATION_PLAN.md). Historical
slice-by-slice details belong in the
[`implementation notes`](../../docs/vnext/IMPLEMENTATION_NOTES.md), not here.
