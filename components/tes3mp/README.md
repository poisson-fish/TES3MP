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

The Slice 3.1 anchor translation units remain intentionally behavior-free. They
keep every approved target participating in compilation and linking while the
later slice sources below add only their explicitly approved APIs.

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

Slice 3.4 adds ADR-0017's passive deterministic facilities. Server core owns a
project-defined monotonic nanosecond observation seam, rational 30 Hz scheduler
with four-tick bounded catch-up, and numeric-keyed SplitMix64/xoshiro256** V1
streams with explicit state restore. It still owns no wall-clock adapter,
thread, sleep loop, reducer, command admission, or canonical checksum.

Test support owns the checked manual clock, independently bounded FIFO byte
duplex, and scripted exact-byte trace harness. `TestTraceDigestV1` is an FNV-1a
test diagnostic, not canonical state, protocol, persistence, or replay policy.
Production targets are checked against both reverse links to test support and
direct `tes3mp/test_support` includes. Production transport semantics remain
Phase 6 work.

Slice 3.5 adds ADR-0018's passive `FaultInjectingLink` wrapper in test support.
Direction plus a test-only numeric channel key selects an immutable bounded
profile. Separate version-one random streams drive integer-rate loss,
duplication, jitter, and reorder holds for each path. Explicit pump,
stall/resume, and disconnect controls remain single-threaded and use the
injected monotonic clock. These channel and lifecycle types make no production
transport claim; Phase 4 still owns message classification and Phase 6 owns the
real transport interface.

Slice 3.6 adds ADR-0019's opt-in target-scoped runtime-safety plumbing. The
standalone presets in this directory build every engine-independent library and
contract executable without OpenMW. Linux Clang 18 has separate ASan+UBSan and
ThreadSanitizer profiles; ASan+UBSan also builds one bounded libFuzzer harness
over the test-support-only spatial decoder. Normal presets remain
uninstrumented, incompatible profiles fail configuration, and project-owned
sources cannot silently omit the selected instrumentation.

The repository-owned entry points are:

```sh
python3 scripts/run_tes3mp_runtime_safety.py --profile asan-ubsan --fuzz-seconds 30
python3 scripts/run_tes3mp_runtime_safety.py --profile tsan
```

Both profiles retain exact toolchain, contract, instrumentation, corpus, log,
and result evidence under `build/`. Fuzz channels and bytes remain test-only;
the harness does not define a Phase 4 production decoder or wire format.

The accepted Slice 3.6 gate passes in hosted CI at `fc8f178081`: all five
contracts pass under separate Linux Clang 18 ASan+UBSan and ThreadSanitizer
profiles, and the bounded 30-second spatial-decoder fuzz smoke retains no
reproducer.

Slice 3.7 accepts ADR-0020 without changing the target graph. Server core owns
closed integer metric definitions, typed structured events, explicit non-owning
sink references, and explicit no-op sinks. Test support owns preallocated
bounded FIFO recorders with reject-newest overflow and visible dropped counts.
The interfaces accept no raw text, byte payload, wall-clock timestamp, mutable
state, or backend type; observations never affect canonical results or
checksums. No production logging/export backend, dispatcher thread, or queue
exists yet.
