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
ThreadSanitizer profiles; ASan+UBSan also builds four bounded libFuzzer harnesses
covering the test-support spatial decoder and every production Phase 4 decoder.
Normal presets remain
uninstrumented, incompatible profiles fail configuration, and project-owned
sources cannot silently omit the selected instrumentation.

The repository-owned entry points are:

```sh
python3 scripts/run_tes3mp_runtime_safety.py --profile asan-ubsan --fuzz-seconds 30
python3 scripts/run_tes3mp_runtime_safety.py --profile tsan
```

Both profiles retain exact toolchain, contract, instrumentation, decoder
registry, corpus, log, and result evidence under `build/`. Fuzz channels and
bytes remain test-only; the harnesses exercise production decoders but do not
define wire behavior.

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

Hosted runtime-safety evidence at `57973b65c7` instruments all three new
observability sources, runs all six contracts under separate ASan+UBSan and
ThreadSanitizer profiles, and retains no sanitizer finding or fuzz reproducer.

Phase 4 Slice 4.1 accepts ADR-0021 and adds the first production protocol
codec boundary. A fixed 12-byte little-endian `T3MP` format-one header selects
one of three closed message classes and five initial stable kind identifiers
before payload work. Class payload budgets are fixed at 4 KiB for session
control, 16 KiB for reliable operations, and 64 KiB for latest-wins snapshots.

The decoder rejects bad magic/version, unknown or mismatched class/kind pairs,
empty or oversized payloads, truncation, length mismatch, trailing bytes, and
concatenated frames before allocating a payload. Success returns one owned
bounded byte vector; failure returns only closed enum and numeric context with
no text or packet view. FlatBuffers payload schemas and generated headers do not
land until Slice 4.2. The framing contract and production-decoder fuzz target
remain independent of OpenMW, transport libraries, platform APIs, and test
support.

Slice 4.2 accepts ADR-0022 and adds three separately identified, size-prefixed
FlatBuffer control payloads: `T3CH` client offers, `T3SH` negotiated results,
and `T3RJ` typed rejections. The generated FlatBuffers views and pinned
header-only runtime remain private to `tes3mp_protocol`; callers receive only
owned version, capability, hello, rejection, or closed error values.

Each offer carries one major, an inclusive minor range, and at most 32 sorted
nonzero optional plus 32 sorted nonzero required capability IDs. Negotiation is
a pure operation that selects the highest overlapping minor, checks requirements
in both directions, and returns the sorted supported intersection. No gameplay
capability ID or release version is assigned yet. Four initial bytes classify
short, `T3MP`, and legacy/unknown preambles; non-vNext input produces no wire
reply. A dedicated eighth contract, generated-code drift check, and bounded
handshake-decoder fuzz target cover the new boundary without adding session,
authentication, transport, OpenMW, authority, or gameplay behavior.

Slice 4.3 accepts ADR-0023 and adds explicit client/server session machines in
the existing `tes3mp_client_session` and `tes3mp_server_core` targets. Both
require an encrypted-ready event before hello negotiation and authentication.
The server owns one pollable authentication operation, transfers one move-only
opaque value of at most 256 bytes, accepts only the matching attempt and session
generation, and retains only a nonzero `PrincipalId` on success.

Caller-supplied policies bound transport/negotiation, authentication-input, and
provider stages from 1 millisecond through 120 seconds using the injected
monotonic clock. Timeout, cancellation, stale completion, illegal transition,
and lifecycle observations use closed enum/numeric values. The ninth contract
exercises exact boundaries, terminal atomicity, provider lifetime, and secret
redaction shape. Authentication remains typed in-memory composition: no new
wire kind, credential schema, real provider, resumption, authority, durable
state, or gameplay behavior is introduced.

Slice 4.4 accepts ADR-0024 and adds two fully initialized protocol-owned header
values without adding a wire root. `ReliableOperationHeader` carries one
existing client command header plus an explicit optional entity precondition;
it cannot carry writer admission, canonical results, a batch, or generic bytes.
`LatestWinsSnapshotHeader` binds a server publication tick and optional
highest-contiguous-finalized command acknowledgement to a target session and
generation; it adds no acceptance flag, global entity revision, or separate
snapshot sequence. Complete typed `T3RO`/`T3LS` roots were separately gated on
Slice 4.5's reviewed command/snapshot bodies and are described below.

Slice 4.5 accepts ADR-0025 and GDR-0011 and adds closed typed `T3RO`
velocity-intent and `T3LS` spatial-view payloads, verifier-first owned codecs,
role-specific established-session guards, atomic confirmed client snapshots,
and a synchronous in-memory fake peer. The exchange remains deliberately
limited to one velocity intent and a target-session-selected view of at most 256
strictly entity-ordered entries; it adds no reducer, canonical mutation,
prediction, rendering, real transport, OpenMW dependency, or broader gameplay.

Slice 4.6 hardens the completed Phase 4 surface without changing production
behavior. Deterministic property contracts cover scalar and collection
boundaries, exhaustive single-bit mutations must reject or normalize through an
owned round trip, and checked-in valid golden seeds are regenerated and verified
by the contract executables. A fail-closed registry maps all seven bounded
decoders to one of the four sanitizer-backed fuzz targets and corpora, pins each
production golden seed by SHA-256, and records that mapping in runtime-safety
evidence.

Phase 5 Slice 5.1 accepts ADR-0026 and adds a server-core-owned typed command
proposal plus a single-threaded intake/tick coordinator. The coordinator
composes the existing 30 Hz scheduler, seals FIFO writer-observed prefixes,
assigns eligible ticks and global ingress ordinals only while draining, and
enforces hard ceilings of 4,096 pending commands globally, 128 per session
generation, and 1,024 per tick. Full queues reject the new proposal with a typed
observable result; tick-limited suffixes remain queued without loss, overwrite,
or early stamps. Scheduler and ordinal exhaustion fail closed without publishing
a partial batch. This slice deliberately adds no canonical store, command
validation, deduplication, reducer, acknowledgement, disconnect action, or
gameplay behavior.

Phase 5 Slice 5.2 accepts ADR-0027 and adds one immutable canonical server-state
value with separately scoped player-entity and active-session-progress
partitions. Complete inputs must be strictly identity-ordered, remain within
hard 256/256 limits, use unique player/entity/session identities, and carry
explicit one-to-one active bindings before owned vectors are constructed.
Const spans and binary-search lookups expose no general mutation path. A pure
checked operation atomically replaces cell/root transform and velocity under
one incremented entity revision while preserving identity and authority epoch;
tick regression and revision exhaustion return typed errors. Session-generation
acknowledgement is optional contiguous-finalized disposition progress only.
Lifecycle, persistence, reducers, acknowledgement advancement, publication,
movement, interest, resync, and gameplay remain later gated work.
