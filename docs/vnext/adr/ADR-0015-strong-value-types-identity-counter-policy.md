# ADR-0015: Strong value types and identity/counter policy

Status: **Accepted**

Date opened: 2026-08-26

Date approved: 2026-08-26

Decision owner: project owner

Needed by: Phase 3 Slice 3.2

## Decision questions

Which identity and counter types enter the first engine-independent API; what
are their widths, scopes, and invalid values; which construction, comparison,
hashing, and formatting operations are exposed; and how must monotonic counter
overflow behave?

These choices are required before Slice 3.2 adds public headers. A primitive
layout, generic typedef helper, default constructor, or convenient arithmetic
operator must not silently decide canonical identity or replay behavior.

## Decision summary

The project owner approved Option A for Decisions 1 through 5 on 2026-08-26:

1. use ten scoped unsigned 64-bit semantic identity/counter types;
2. make zero invalid except for the zero-based `ServerTick`, delete public
   default construction, and expose one-based `initial()` where declared;
3. use a project-owned policy template with explicit construction/raw access,
   same-type comparison, hashing, and formatting but no implicit conversions;
4. expose optional-returning checked `next()` only on counter types and never
   wrap, saturate, throw, or provide generic arithmetic; and
5. keep type-qualified debug formatting stable while codecs, allocation,
   persistence, and serialization remain explicit external boundaries.

This approval authorizes the Slice 3.2 semantic scalar API and its tests. It
does not authorize identity allocation, schemas, entity storage, session
replacement behavior, or gameplay-domain state.

## Accepted constraints

Every option must preserve ADR-0003, ADR-0004, ADR-0006, ADR-0013, and ADR-0014:

1. Network-visible identities, ticks, sequences, revisions, command IDs,
   authority epochs, and ingress ordinals are distinct strong types. Primitive
   aliases may not make unrelated values interchangeable.
2. Values have stable declared widths and checked arithmetic. Wraparound never
   restores an old identity, revision, authority epoch, sequence, or order.
3. Invalid input fails before state mutation. Client input receives structural,
   range, scope, sequence/revision/epoch, and basic domain checks without an
   elaborate anti-cheat framework.
4. Protocol, server, client-session, and test-support code share semantic value
   types without OpenMW, FlatBuffers runtime, selected transport, renderer,
   platform, formatting-library, or logging-backend dependencies.
5. The types represent values but do not allocate identities, consult clocks or
   RNGs, serialize themselves through a schema, or mutate domain state.
6. Protocol evolution never reuses a stable identity or capability meaning.

## Scenarios evaluated

1. A function expecting `EntityRevision` is accidentally passed `EntityId`.
2. A decoded zero identity reaches a reducer.
3. Tick zero represents the deterministic initial state before the first tick.
4. A reconnect resumes the same session generation and retries a command ID.
5. A replacement connection starts a new generation while old commands remain
   in a delayed queue.
6. A revision, sequence, epoch, tick, or ingress ordinal reaches `UINT64_MAX`.
7. A structured log must distinguish `EntityId{7}` from `PlayerId{7}`.
8. A protocol codec needs the raw integer without giving the rest of the code
   implicit primitive conversions.
9. An unordered test container hashes values while canonical serialization
   still sorts them explicitly.
10. A future persistence migration reads an invalid or exhausted value.

## Decision 1: initial type catalog, width, and scope

### Option A: scoped unsigned 64-bit catalog (approved)

Add these public semantic types to the `tes3mp_protocol` target:

| Type | Width | Validity/start | Uniqueness or ordering scope |
|---|---:|---|---|
| `EntityId` | `u64` | nonzero | one canonical world identity space |
| `PlayerId` | `u64` | nonzero | one server/world player identity space |
| `SessionId` | `u64` | nonzero | active/replay session identity; not a credential |
| `SessionGeneration` | `u64` | nonzero, starts at 1 | one logical session across replacements/resume |
| `ServerTick` | `u64` | zero valid, starts at 0 | one canonical server timeline |
| `CommandSequence` | `u64` | nonzero, starts at 1 | one session generation |
| `EntityRevision` | `u64` | nonzero, starts at 1 | one entity lifetime |
| `CommandId` | `u64` | nonzero | one session generation; stable across retry/resume |
| `AuthorityEpoch` | `u64` | nonzero, starts at 1 | one entity/authority slot lifetime |
| `IngressOrdinal` | `u64` | nonzero, starts at 1 | one canonical server process/replay stream |

`ConnectionId`, account identity, content identity, cell identity, script-event
identity, database keys, and transport handles remain deferred to their owning
slices. A player-controlled entity holds both a `PlayerId` and `EntityId`; the
types are not aliases and neither is derived by casting the other.

All initial values fit the approved FlatBuffers scalar subset, have practical
headroom, and compare without platform-width ambiguity. Scopes are part of the
contract: a `CommandId` is not claimed globally unique without its session
generation, and `SessionId` is correlation state rather than authentication.

### Option B: use 32-bit values where expected counts are small

Use 32-bit revisions, sequences, epochs, ticks, and local identities, retaining
64-bit values only for global entities or commands. This reduces state and wire
size but creates earlier exhaustion and more width-specific templates and
migrations without a demonstrated need.

### Option C: 128-bit opaque identities and 64-bit counters

Use random or time-independent 128-bit values for entity, player, session, and
command identity. This reduces coordination between allocators but doubles
common identity storage/wire width, complicates formatting and FlatBuffers
mapping, and does not help monotonic counters.

### Option D: reuse OpenMW and transport identity types

Use `ESM::RefId`, actor IDs, Steam connection handles, or platform identity in
shared APIs. This couples canonical/product identity to engine content or a
selected adapter and violates the independent target boundary.

## Decision 2: invalid values and construction

### Option A: unrepresentable invalid state except explicitly valid tick zero (approved)

Delete public default construction. Nonzero types use a named `fromValue(raw)`
factory returning `std::optional<T>` and an `initial()` factory where the
contract starts at one. `ServerTick::fromValue(0)` and `ServerTick::initial()`
are valid and represent the initial state.

Construction from another semantic type is impossible. Raw construction and
raw access are explicit. Optionality is represented by `std::optional<T>` at
the owning API boundary, not by smuggling zero through a valid strong value.
The protocol decoder can classify an empty result as a stable invalid-value
rejection before mutation.

### Option B: default-construct every type into an invalid zero state

Allow zero-valued objects and require `isValid()` checks. This simplifies
containers and deserialization but permits invalid canonical values to exist
and makes forgotten checks ordinary.

### Option C: make zero valid for every type

Treat the full `u64` range as valid and use external optionals when absence is
needed. This avoids factories but removes a cheap malformed/uninitialized input
check and makes counter start/exhaustion rules less obvious.

### Option D: reserve `UINT64_MAX` as invalid

Allow zero but reject the maximum. This preserves default construction but
reduces the terminal counter value and makes checked-next/exhaustion semantics
more surprising.

## Decision 3: type implementation and ordinary operations

### Option A: project-owned policy template with explicit public aliases (approved)

Implement one small project-owned internal template in the `tes3mp_protocol`
public headers, parameterized by a unique tag, zero-validity policy, and counter
capability. Export the ten semantic names as distinct aliases. Do not reuse
OpenMW's `Misc::StrongTypedef`: it default-constructs an uninitialized value,
implicitly converts to/from the primitive through references, and increments
without overflow checks.

Each public type provides only:

- explicit named construction and `value()` access;
- same-type equality and total ordering;
- `std::hash` based on the raw value for non-canonical lookup containers;
- stable type-qualified debug text and `operator<<`; and
- checked counter advancement only when its policy permits it.

There are no mixed-type comparisons, implicit primitive conversions, generic
arithmetic, boolean conversion, or inheritance. The template and tags remain
implementation details; callers program against semantic names.

### Option B: reuse `Misc::StrongTypedef`

This minimizes new code and follows one OpenMW component precedent. Its implicit
primitive conversions, uninitialized default construction, and unchecked `++`
are specifically incompatible with the accepted invalid/overflow policy.

### Option C: hand-write ten unrelated classes

This makes every operation explicit and avoids template diagnostics, but
duplicates construction, comparison, hash, formatting, and overflow logic and
invites subtle behavioral drift between types.

### Option D: enum classes over `u64`

Scoped enums are distinct and compact, but arbitrary runtime construction,
checked advancement, hashing/formatting, and zero policies become awkward and
misrepresent values as closed enumerations.

## Decision 4: monotonic advancement and exhaustion

### Option A: checked named advancement returning optional (approved)

Counter-capable types expose `next()` returning `std::optional<T>`. At
`UINT64_MAX`, it returns empty and leaves the source unchanged. No prefix/
postfix increment, addition, subtraction, saturation, or modular comparison is
provided. Reducers and schedulers must convert exhaustion into an explicit
terminal operational error before mutation.

Identity types have no advancement API; their owning allocator arrives in a
later slice and must apply the same no-wrap rule. Comparison is normal unsigned
total ordering, never serial-number arithmetic.

### Option B: checked advancement throws

Throw `std::overflow_error` at exhaustion. This is explicit but introduces
exceptions into deterministic reducer control flow where an ordinary checked
result is easier to test and route into an operational error.

### Option C: saturate at the maximum

Return the maximum forever. This avoids wraparound but silently reuses a
revision, sequence, tick, epoch, or ordinal and destroys progress guarantees.

### Option D: wrap modulo `2^64`

Use native unsigned arithmetic and serial-number comparisons. This can work for
bounded network windows but would eventually restore stale canonical identities
and directly contradict ADR-0013.

## Decision 5: formatting, serialization, and allocation separation

### Option A: type-qualified debug text and explicit raw codec boundary (approved)

Format values as stable ASCII `TypeName{decimal}` through `toString()` and
`operator<<`, using project/standard facilities only. No global `fmt` or
`std::format` customization is required in the base API. This makes diagnostics
unambiguous and testable across supported standard libraries.

FlatBuffers codecs explicitly read/write `value()` and call `fromValue()`;
strong types do not include generated schema headers or serialize themselves.
Identity allocation, seed/random selection, persistence mapping, and transport
correlation remain separate injected services owned by later slices. Hashing is
for lookup only and never defines canonical iteration or checksums.

### Option B: format raw decimal values only

Print `42` for every type. This is compact but makes logs and assertion failures
ambiguous when several identity/counter kinds share a field list.

### Option C: let strong types own FlatBuffers conversion

Add schema conversion methods to each type. This reduces codec boilerplate but
couples the lowest semantic API to generated code and lets schema layout drive
domain construction.

### Option D: allocate identities inside constructors

Default constructors draw from a global counter or RNG. This is ergonomic but
introduces ambient mutable state, nondeterminism, hidden scope, and difficult
replay/persistence behavior.

## Approved policy summary

This table records the approved result for all five decisions.

| Area | Recommended contract |
|---|---|
| Catalog | Ten distinct `u64` semantic types with documented world/entity/session/timeline scopes |
| Invalid state | Zero rejected for identities and one-based counters; tick zero valid; no public default construction |
| API | Project-owned policy template, explicit factory/raw access, same-type order/hash/debug formatting, no implicit conversions |
| Overflow | Counter `next()` returns empty at `UINT64_MAX`; no wrapping, saturation, or arithmetic operators |
| Boundaries | `TypeName{decimal}` debug text; codecs convert raw values explicitly; allocation and persistence stay outside the types |

## Approved acceptance tests

Slice 3.2 must add tests named for these contracts:

1. `semantic_types_are_not_convertible_or_comparable_to_each_other`
2. `semantic_types_are_not_implicitly_convertible_to_or_from_u64`
3. `nonzero_types_reject_zero_and_have_no_default_constructor`
4. `server_tick_accepts_zero_as_its_initial_value`
5. `one_based_counters_expose_value_one_as_initial`
6. `same_type_equality_and_total_order_use_unsigned_value_order`
7. `hash_matches_equal_values_without_defining_canonical_order`
8. `debug_text_is_stable_type_qualified_ascii_decimal`
9. `counter_next_advances_without_mutating_source`
10. `counter_next_at_u64_max_returns_empty_without_wrap`
11. `identity_types_expose_no_increment_or_arithmetic_operators`
12. `types_compile_without_openmw_flatbuffers_transport_fmt_or_platform_headers`
13. `test_support_can_construct_boundary_and_exhaustion_values_explicitly`

The implementation demo must show a compile-time cross-type rejection, zero and
maximum boundary behavior, stable formatting, and an independent target build.

## Consequences of the approved decision

- Common scalar handling is implemented once while public APIs remain semantic.
- Invalid identities cannot exist as constructed values; optional absence is
  visible in function signatures.
- Callers write explicit codec/allocation code instead of relying on convenient
  implicit conversions.
- Ten 64-bit values are simple and roomy, but the documented scope is required
  to interpret uniqueness correctly.
- `std::hash` supports ordinary lookup; deterministic reducers still sort by
  stable semantic keys before encoding or checksumming.
- Exhaustion paths are improbable but fully testable and fail before mutation.

## Failure modes and mitigations

- **Template leaks generic operations:** keep the public surface allowlisted and
  add compile-time negative assertions for conversion and arithmetic.
- **Zero becomes a hidden optional:** reject it in nonzero factories and require
  `std::optional` in APIs that represent absence.
- **Command ID is treated globally:** include session generation in command-key
  contexts and replay evidence; never document bare `CommandId` as global.
- **Session ID is treated as authentication:** keep credentials/resume tokens in
  ADR-0005-owned security interfaces and label session IDs non-secret.
- **Hash order becomes canonical:** tests permute insertion order and later
  canonical encoders sort explicitly.
- **Counter exhaustion is ignored:** make `next()` return a checked result and
  require owning loops/reducers to handle empty before mutation.
- **Formatting pulls a large dependency inward:** implement stable decimal text
  with standard/project code and keep logging backends outside the target.
- **Future type needs different width:** add a new semantic type/versioned field;
  do not widen or reinterpret a released type silently.

## Review and replacement triggers

Reopen this ADR if:

- representative scale or protocol evidence requires a different width;
- a listed uniqueness/order scope changes;
- a type must survive outside its documented world/session/entity lifetime;
- an approved schema cannot represent the catalog without lossy conversion;
- exceptions become the project-wide deterministic error model;
- a public formatting or serialization contract requires an external runtime;
  or
- a new identity family cannot be added without weakening type separation.

## Owner approval

Approved by the project owner in the 2026-08-26 working session: Option A for
Decisions 1 through 5.

Approval fixes the semantic scalar contracts only. It does not authorize
identity allocation, schemas, entity storage, session replacement behavior, or
gameplay-domain state.
