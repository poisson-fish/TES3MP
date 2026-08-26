# ADR-0004: Protocol schema, codec, and evolution policy

Status: **Proposed — owner decision required**

Date opened: 2026-08-26

Decision owner: project owner

Needed by: Phase 2

## Decision requested

Select the schema and binary codec profile that Phase 4 will use for every
untrusted vNext protocol message. This decision also fixes the schema-evolution,
code-generation, dependency-pinning, and decoder-boundary rules that make the
codec safe to use under the hostile-Internet model accepted in ADR-0003.

No option is approved yet. The recommendation is **Option A: FlatBuffers with a
restricted verifier-first profile**. Approval of this ADR would authorize a
minimal dependency proof and the policy itself, not Phase 4 production schemas
or gameplay behavior.

## Why this decision is needed now

Phase 3 must create the protocol target without accidental engine, transport, or
platform coupling. Phase 4 then needs a schema and codec whose decoders can be
bounded, fuzzed, evolved, and converted into owned validated values before a
session or canonical reducer sees them.

The choice affects:

- the shape and lifetime of protocol values;
- where byte, depth, collection, allocation, and work limits are enforced;
- how old and new peers exchange optional fields and capabilities;
- generated-code and compiler pinning in every supported build;
- fuzz targets, golden files, and schema-compatibility checks; and
- the security and update surface exposed to hostile network input.

It does not select a transport, encryption, authentication provider, authority
model, state scope, gameplay rule, or OpenMW hook.

## Non-negotiable constraints

Every option must satisfy the accepted README, ADR-0002, and ADR-0003 rules:

1. An owned outer framing boundary rejects an undeclared or oversized byte
   length before allocating or invoking a payload decoder.
2. Structural decode and semantic validation finish in temporary storage before
   a session or canonical-state mutation can occur.
3. Every string, collection, nesting level, allocation, and unit of decode work
   has an explicit limit. An outer byte limit alone is necessary but not
   sufficient.
4. Codec-generated views and runtime types cannot escape the protocol codec
   implementation. Callers receive owned vNext value types or a structured
   decode error.
5. Unknown optional evolution is safe. Unknown required capabilities and
   message kinds fail clearly rather than being guessed from defaults.
6. Stable field, enum, union, message, capability, and protocol identifiers are
   never reused for a different meaning.
7. The codec runtime and code generator are exactly pinned, license-recorded,
   independently buildable on the accepted desktop matrix, and assessed for
   Android ARM64 feasibility.
8. Every decoder is registered with round-trip, golden-evolution, mutation, and
   sanitizer-backed fuzz coverage.

## Scenarios the decision must cover

1. **Malformed input:** a peer sends every truncation of a valid message,
   corrupt offsets or lengths, deep nesting, many tiny objects, invalid UTF-8,
   non-finite numerics, an unknown message kind, and trailing or concatenated
   bytes. Decode fails within declared budgets and produces no partial value.
2. **Allocation amplification:** a small input claims a huge string, vector, or
   object graph. The decoder rejects it before allocating the claimed domain
   collection, and verifier work is independently bounded.
3. **Compatible evolution:** an old peer reads a message containing a new
   optional field, while a new peer reads the old form. Both retain the approved
   old meaning and default; neither silently enables a capability.
4. **Incompatible evolution:** a field meaning, required behavior, or message
   shape must change. A new stable identifier and protocol/capability gate make
   the incompatibility explicit; an old identifier is not recycled.
5. **Tool mismatch:** generated sources come from a different generator than
   the pinned runtime or committed schema. The build or repository check fails
   before a release artifact is produced.
6. **Buffer lifetime:** transport storage is recycled immediately after decode.
   The delivered protocol value remains valid because it owns its data and no
   generated view or packet pointer escaped.

## Options considered

### Option A: FlatBuffers with a restricted verifier-first profile (recommended)

Pin the last normal stable release, FlatBuffers `v25.12.19` at commit
`7e163021e59cca4f8e1e35a7c828b5c6b7915953`, and use only its C++ generated
schema accessors, builder, and generated verifier.

The required profile would be:

- accept only a complete owned frame whose declared length exactly equals the
  received frame length and is below the message-class byte budget;
- give every root a file identifier and verify it before any generated accessor
  is used;
- configure verifier maximum size, depth, and table count below project-owned
  limits rather than accepting library defaults;
- inspect verified string/vector lengths against schema-specific limits before
  allocating or copying domain values;
- immediately copy into owned, semantically validated vNext types and discard
  every generated view when decode returns;
- use explicit field IDs and explicit enum/union discriminants from the first
  schema, add only optional fields, deprecate instead of delete, and never reuse
  an identifier;
- exclude FlexBuffers, reflection, runtime schema parsing, JSON/text parsing,
  nested FlatBuffers, mutable-buffer accessors, native object unpacking, RPC
  integration, and 64-bit offsets from the network decoder;
- check in generated C++ beside its schema, regenerate with the exact pinned
  `flatc`, and fail CI if regeneration changes the committed output; and
- treat schema compatibility as a vNext policy enforced by repository checks,
  because FlatBuffers' date-based library releases are not semantic-version
  guarantees for this product.

Why it is recommended:

- the generated verifier validates offsets, ranges, alignment, strings, depth,
  and table work without first constructing an allocation-heavy message tree;
- verified vector and string lengths can be checked before domain allocation;
- the runtime needed by generated C++ is header-only, small, and independent of
  transport, OpenMW, RPC, operating-system, and renderer types;
- forward/backward-compatible additive table evolution is sufficient when
  combined with vNext's explicit protocol and capability negotiation; and
- Apache-2.0 licensing and CMake/C++ portability fit ADR-0002, while Android
  targets need only generated code and runtime headers rather than a target-side
  schema compiler.

Tradeoffs and risks:

- zero-copy views are unsafe if their packet-buffer lifetime escapes; the owned
  conversion boundary must be enforced in code review and tests;
- field-order evolution is easy to misuse without explicit IDs and schema lint;
- structural verification does not enforce UTF-8, numeric ranges, per-field
  counts, or gameplay semantics; vNext must add those checks;
- the project has active maintenance but irregular, date-based releases. The
  February 2026 post-release snapshot is not proposed because its upstream
  release intent is unclear; and
- 2026 upstream reports include issues in reflection, FlexBuffers, and utility
  surfaces. The recommended profile excludes those surfaces, but any issue in
  generated verification or core accessors triggers an immediate pin review.

Primary evidence:

- [FlatBuffers C++ verifier at the proposed pin](https://github.com/google/flatbuffers/blob/v25.12.19/include/flatbuffers/verifier.h)
- [FlatBuffers schema-evolution rules](https://flatbuffers.dev/evolution/)
- [FlatBuffers release and maintenance history](https://github.com/google/flatbuffers/releases)
- [FlatBuffers Apache-2.0 license](https://github.com/google/flatbuffers/blob/v25.12.19/LICENSE)

### Option B: Protocol Buffers generated C++ messages

Pin Protocol Buffers `v35.0` at
`e59364c38e10de3686a3305ff11fbfc59a10dbd8` and use generated C++ messages with
an owned outer byte cap, recursion cap, schema-specific validation, unknown-field
policy, and immediate conversion to vNext values.

Advantages:

- the strongest ecosystem, documentation, evolution guidance, and tooling of
  the candidates;
- mature unknown-field and additive-evolution behavior; and
- active support, published source/protoc artifacts, BSD-3-Clause licensing,
  and broad desktop/Android portability.

Tradeoffs:

- generated parsing constructs strings, repeated fields, submessages, and
  unknown-field storage before vNext can apply per-field collection limits. A
  byte and recursion cap bounds the total damage, but does not meet the desired
  pre-allocation collection boundary as directly as Option A;
- the current C++ runtime brings a materially larger dependency surface,
  including its required supporting libraries;
- C++ generated code and runtime require an exact version match and provide no
  ABI stability across releases, increasing update and integration coupling;
  and
- serialization is not canonical, so wire bytes cannot be used directly for
  deterministic state checksums or replay equivalence.

Primary evidence:

- [C++ coded-stream byte limits](https://protobuf.dev/reference/cpp/api-docs/google.protobuf.io.coded_stream/)
- [schema evolution guidance](https://protobuf.dev/programming-guides/proto3/#updating)
- [C++ generated-code/runtime version rules](https://protobuf.dev/support/cross-version-runtime-guarantee/#cpp)
- [current support window](https://protobuf.dev/support/version-support/)
- [Protocol Buffers BSD-3-Clause license](https://github.com/protocolbuffers/protobuf/blob/v35.0/LICENSE)

### Option C: Cap'n Proto generated C++ readers

Pin Cap'n Proto `v1.5.0` at
`373e61ec89e2359f1c362e9b2eadc552f4779306` and use generated readers with
strict traversal/nesting limits followed by an owned domain copy.

Advantages:

- explicit security-oriented traversal and nesting budgets account for shared
  pointer amplification and deep/cyclic data;
- strong additive schema evolution and direct access without an allocation-heavy
  parse tree; and
- MIT licensing, current C++ toolchains, Windows support, and active real-world
  maintenance.

Tradeoffs:

- the generated API and runtime pull KJ types, exception behavior, word
  alignment, segment handling, and a larger framework surface into the codec
  implementation;
- byte-oriented transport buffers may require an aligned copy before reading,
  reducing its zero-copy advantage;
- the project published a broad security rollup in 2026 and documented delayed
  handling of some reports. The fixes are present in the proposed release, but
  the maintenance process is a selection risk; and
- its RPC/capability system is deliberately out of scope, so much of the
  ecosystem's distinguishing surface would remain unused.

Primary evidence:

- [Cap'n Proto reader limits at the proposed pin](https://github.com/capnproto/capnproto/blob/v1.5.0/c%2B%2B/src/capnp/message.h)
- [schema language and evolution](https://capnproto.org/language.html)
- [compiler/platform and MIT-license information](https://capnproto.org/install.html)
- [Cap'n Proto 1.5 security rollup](https://github.com/capnproto/capnproto/blob/v2/security-advisories/2026-07-09-capnproto-v1.5-rollup.md)

### Option D: project-owned custom binary codec

Design a compact tagged or fixed-layout codec and generator specifically around
vNext's limits.

This offers complete wire control, the smallest external dependency surface,
and direct expression of every project limit. It also makes this project solely
responsible for parser memory safety, integer arithmetic, evolution tooling,
unknown-field behavior, cross-language support, diagnostics, fuzz hardening,
and long-term maintenance. Those costs do not create player-visible value for
the first vertical slice, so this option is not recommended.

Schema-free formats such as JSON, CBOR, MessagePack, and FlexBuffers were not
promoted to full options. They would require a second project-owned schema and
evolution system and make it easier for unbounded dynamic values to cross the
network boundary.

## Recommendation and proposed acceptance tests

Approve Option A and the restricted profile above, subject to all of the
following evidence before Slice 2.2 is marked **Implemented**:

1. A repository-owned isolated proof builds pinned `flatc`, generated C++ code,
   and the header-only runtime on Windows x86-64/MSVC 2022, Linux x86-64/GCC 13
   and Clang 18, macOS arm64/AppleClang, and scheduled macOS x86-64/AppleClang.
   The proof includes a documented Android ARM64 source-build assessment.
2. The proof schema exercises a root identifier, explicit field IDs, strings,
   vectors, tables, enums, and a union. It round-trips a valid value and old/new
   schema fixtures in both compatible directions.
3. Negative tests cover every truncation, bad root/identifier/offset, declared
   length mismatch, trailing bytes, oversized string/vector, excessive depth
   and table count, unknown union/message kind, invalid UTF-8, non-finite and
   out-of-range numeric values, and failed semantic conversion.
4. Tests instrument allocations or a bounded test allocator to prove that
   claimed oversized collections are rejected before domain allocation and that
   a failed decode returns no partial owned value.
5. A corpus-backed fuzz target invokes the exact production verifier and
   conversion path under ASan/UBSan. The seed corpus includes valid old/new
   golden messages and minimized malformed cases.
6. A schema-policy check rejects missing/duplicate/reused IDs, removal without
   deprecation, unapproved required fields, generator drift, forbidden
   FlatBuffers features, and mismatch between the pinned generator and runtime.
7. Dependency evidence records the exact tag/commit and archive hash, license,
   supported platforms, build options, excluded surfaces, update owner, and a
   quarterly plus security-triggered review cadence.

The proof is disposable selection evidence. Production envelopes, messages,
limits, and wrappers remain Phase 4 work and require their own tests.

## Consequences if the recommendation is approved

- Protocol APIs expose owned vNext types, never FlatBuffers views or builders.
- Every decoder has two explicit stages: structural verification, then bounded
  semantic conversion. Neither stage may mutate session or canonical state.
- Schema files and generated sources are reviewed artifacts. Binary
  compatibility follows explicit IDs and repository policy rather than an
  assumption that all syntactically accepted schema edits are safe.
- Protocol/capability negotiation remains the authority for behavior. A new
  optional field cannot silently activate a feature.
- The protocol may replace FlatBuffers later behind the owned value boundary,
  but supported wire compatibility would require an explicit protocol-version
  migration rather than transparent codec substitution.

## Failure modes and mitigations

- **View lifetime escape:** keep generated headers private to codec targets;
  return owned values; poison/recycle the input buffer in tests after decode.
- **Verifier treated as semantic validation:** require separate per-field and
  cross-field validation with structured errors and negative tests.
- **Unbounded verifier work:** set low message-class byte/depth/table budgets;
  fuzz shared-reference and many-object inputs; meter decode work.
- **Schema ID misuse:** lint every schema and compare against retained prior
  schema descriptors/golden fixtures.
- **Generator/runtime drift:** pin both to one commit, regenerate in CI, and
  reject a dirty diff.
- **Upstream security defect:** track advisories and core verifier/accessor
  reports; patch or update the exact pin; reopen this ADR if the restricted
  profile cannot exclude or mitigate the affected path.
- **Over-general schema:** prohibit reflection, dynamic values, and nested
  payloads; add only fields required by an approved slice.

## Review and replacement triggers

Reopen this ADR if:

- the owner chooses another option or changes the restricted profile;
- the selected release cannot pass one supported desktop proof or the Android
  ARM64 feasibility assessment;
- allocation/work instrumentation or fuzzing shows a declared bound cannot be
  enforced before domain allocation or state delivery;
- a security report affects generated verification/accessors and no acceptable
  pinned fix or local mitigation exists;
- another implementation language becomes a release requirement and lacks a
  compatible, supportable generated runtime;
- protocol evolution requires a change that the approved ID/capability policy
  cannot express safely; or
- measured compile time, binary size, decode cost, or update burden exceeds a
  later owner-approved budget.

## Owner approval

Pending. Approval must name Option A, B, C, or D and any conditions or changes
to the proposed profile and acceptance tests.
