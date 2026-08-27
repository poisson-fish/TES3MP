# TES3MP target boundaries

This directory owns the engine-independent TES3MP libraries. They are separate
from OpenMW's monolithic `components` target and must remain buildable without
OpenMW, rendering, SDL, OSG, platform, or GameNetworkingSockets types.

The direct dependency graph is:

```text
tes3mp_protocol
  ^       ^
  |       |
  |   tes3mp_server_core
  |
tes3mp_transport
  ^
  |
tes3mp_client_session

tes3mp_test_support -> protocol + transport + server_core + client_session
openmw_tes3mp_adapter -> client_session + openmw-lib
```

Arrows point from a target to a target that depends on it. The adapter lives in
`apps/openmw/tes3mp`; it is not part of this engine-independent directory.

`cmake/TES3MPVerifyTargetBoundaries.cmake` checks every direct link against the
ADR-0014 allowlist and rejects known forbidden include families in independent
sources. `scripts/tests/test_tes3mp_target_boundaries.py` proves both checks fail
closed using temporary miniature CMake projects. Later Phase 3 slices add
compile-time public-header isolation checks as real APIs appear.

The current translation units are intentionally empty anchors. They prove that
each approved target participates in compilation and linking without claiming
an API or runtime behavior owned by a later slice.

Slice 3.2 adds the first public API in `include/tes3mp/value_types.hpp`. It owns
the ten ADR-0015 semantic `uint64_t` types, explicit validity factories,
same-type ordering/hash/debug formatting, and checked counter advancement. The
types do not allocate identities or include OpenMW, FlatBuffers,
GameNetworkingSockets, formatting-library, or platform headers.

Slice 3.3 adds ADR-0016's value-only spatial and command/snapshot primitives.
`CellId` distinguishes a content-context-scoped interior cell from an exterior
worldspace/grid cell. Fixed-point position, turn orientation, transform, and
linear-velocity values carry no engine behavior. Command headers, writer stamps,
entity preconditions, and spatial snapshots carry provenance/state values but
do not define payloads, collections, codecs, transport delivery, reducers, or
gameplay validation. The deterministic byte round trip is test-support-only and
is not a wire format.
