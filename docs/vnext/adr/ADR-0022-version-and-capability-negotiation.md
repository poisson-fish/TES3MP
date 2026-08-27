# ADR-0022: Version and capability negotiation

Status: **Accepted**

Date opened: 2026-08-27

Date approved: 2026-08-27

Decision owner: project owner

Needed by: Phase 4 Slice 4.2

## Decision questions

Which bounded payload schemas represent `ClientHello`, `ServerHello`, and
`SessionRejected`; how do a client and server select a protocol minor and
capability set; how are capability identities and collections represented; what
can safely be returned for incompatible or non-vNext input; and where does
negotiation end before the session state machine begins?

These questions affect wire compatibility, hostile-input work, connection
behavior, and the public protocol API. They must be approved before the Slice
4.2 schemas, generated code, codec, or negotiation behavior land.

## Decision summary

The project owner approved Option A for Decisions 1 through 5 on 2026-08-27:

1. use three distinct size-prefixed FlatBuffer roots with `T3CH`, `T3SH`, and
   `T3RJ` file identifiers and immediate conversion to owned values;
2. use a one-request/one-response negotiation in which the server selects the
   highest overlapping minor and enforces required capabilities in both
   directions;
3. use nonzero stable `u32` capability IDs, with at most 32 sorted optional and
   32 sorted required IDs in each offer and no ID in both sets;
4. use closed rejection reasons and bounded numeric context for valid vNext
   hellos, while non-vNext/TES3MP 0.8 input receives no incompatible wire reply;
   and
5. keep payload decoding and pure negotiation separate from the Phase 4.3
   session state machine and all canonical mutation.

This decision implements ADR-0004, ADR-0013, and ADR-0021. It does not select an
authentication provider, session transition, transport behavior, gameplay
capability, authority, state scope, OpenMW hook, or public release version.

## Existing constraints

1. The current and immediately previous minor within one major must be
   exercisable, with highest-overlap selection and no cross-major fallback.
2. Unknown optional capabilities are ignored and never enabled. Unsupported
   required capabilities reject before authentication or mutation.
3. Every payload is independently size-prefixed, file-identified, verified,
   semantically validated, and copied into owned values before delivery.
4. Capability lists and decode work are bounded before domain allocation.
5. Generated FlatBuffers views remain private to `tes3mp_protocol`.
6. Rejection/error values contain no raw packet bytes, free-form text,
   credentials, addresses, dependency errors, or platform/build identity.
7. TES3MP 0.8 wire and transport compatibility remains explicitly excluded.
8. Negotiated capabilities remain immutable for the connection lifetime; Slice
   4.3 owns the state that retains them.

## Representative scenarios

1. A current server supporting minors 0 through 1 receives a current-minor
   client and selects minor 1.
2. The same server receives a previous-minor-only client and selects minor 0.
3. Two ranges overlap at more than one minor. The highest common minor wins
   without using product, engine, build, or platform metadata.
4. Major versions differ or minor ranges do not overlap. The server returns a
   stable rejection before authentication begins.
5. A client advertises an optional ID unknown to the server. Negotiation
   succeeds, the ID is absent from the negotiated set, and no field presence can
   enable it.
6. A client requires an unsupported server capability, or the server requires
   a capability the client does not support. Negotiation rejects with the
   numerically lowest unsupported required ID, independent of input direction.
7. A capability vector is oversized, zero-bearing, unsorted, duplicated, or
   overlaps the other set. Decode fails before negotiation and returns no
   partial hello.
8. A valid hello payload is truncated, has the wrong file identifier, has an
   invalid size prefix, or carries trailing bytes. Decode returns a closed error
   and no owned value.
9. Input begins with a valid `T3MP` frame preamble but proposes an incompatible
   vNext protocol version. A vNext `SessionRejected` response is safe.
10. Input is TES3MP 0.8 or arbitrary non-`T3MP` data. No shared framing or
    transport exists, so the server records a stable local
    `LegacyOrUnknownProtocol` disposition and sends no wire response.

## Decision 1: payload schema boundary

### Option A: three distinct size-prefixed roots (approved)

Define one FlatBuffer root per already reserved ADR-0021 message kind:

| Kind | File identifier | Owned value |
|---|---|---|
| `ClientHello` | `T3CH` | protocol range plus optional and required capability sets |
| `ServerHello` | `T3SH` | selected protocol version plus negotiated capabilities |
| `SessionRejected` | `T3RJ` | closed reason plus server version range and optional offending capability |

Each table uses explicit consecutive field IDs and the exact ADR-0004 generator
profile. The outer frame kind and inner file identifier must agree before a
value reaches negotiation. Size-prefix length, identifier, structural verifier,
and semantic validation are distinct failure stages.

This is recommended because a schema cannot be decoded as another control
message accidentally, per-kind golden fixtures remain small, and generated
views stay local to one codec implementation.

### Option B: one shared session-control union

Use one root and generated union for all control messages. This centralizes
dispatch but couples unrelated message evolution and weakens the independent
outer-kind/inner-identifier check.

### Option C: custom payload encoding

Use project-owned byte layouts. This would reopen ADR-0004 and duplicate the
already accepted verifier, evolution, generation, and fuzz policy.

## Decision 2: negotiation exchange and version selection

### Option A: client offer and deterministic server selection (approved)

`ClientHello` carries one `u16` major, an inclusive `u16` minor range, and two
capability sets. Server policy uses the same range/set value but is supplied by
the composition layer rather than hard-coded as a public release version in
Slice 4.2.

The server evaluates, in order:

1. equal major;
2. a non-empty inclusive minor overlap;
3. every client-required capability is server-supported; and
4. every server-required capability is client-supported.

Success selects the highest common minor and the sorted intersection of both
supported sets. A required capability implies support. `ServerHello` returns
that version and complete immutable negotiated set.

### Option B: symmetric multi-step offers

Exchange server policy before confirmation. This makes requirements explicit in
both wire directions but adds states, a round trip, timeout edges, and more
pre-authentication work without changing the deterministic result.

### Option C: exact major/minor equality

Require one exact version. This is simpler but contradicts the owner-approved
current-plus-previous-minor policy in ADR-0013.

## Decision 3: capability identity and bounds

### Option A: stable nonzero `u32` IDs and two 32-entry sets (approved)

Capability ID zero is invalid. Optional and required vectors are each limited to
32 entries, strictly ascending, duplicate-free, and disjoint. Their union is
the peer's supported set, so a negotiated intersection contains at most 64 IDs.

The namespace is intentionally larger than the first product needs while the
fixed counts keep each hello and its comparison work small. Slice 4.2 assigns no
gameplay or platform capability ID; later owning slices add named IDs and golden
compatibility coverage.

### Option B: stable `u16` IDs

This saves two bytes per entry but creates needless long-term registry pressure
for a handshake whose approved count is already small.

### Option C: bounded string IDs

Names are readable in captures but add UTF-8, length, canonicalization,
allocation, disclosure, and comparison rules without changing behavior.

### Option D: larger configurable collections

Operator-adjustable or much larger lists add pre-authentication memory and work
and make interoperability configuration-dependent.

## Decision 4: rejection and non-vNext input

### Option A: typed vNext rejection and no legacy wire response (approved)

`SessionRejected` uses these stable reasons:

- `ProtocolMajorMismatch`;
- `NoCompatibleMinor`; and
- `UnsupportedRequiredCapability`.

It also carries the server's supported range and, only for the capability
reason, the lowest unsupported nonzero capability ID. It carries no string.

A complete valid vNext-framed `ClientHello` can receive this response. Four
bytes are enough only to classify the preamble: fewer bytes remain undecided,
`T3MP` proceeds to normal frame decoding, and any other preamble is locally
`LegacyOrUnknownProtocol` with no response. TES3MP 0.8 cannot parse a vNext
FlatBuffer and does not share the selected transport, so implementing an old
packet merely to reject it would violate the clean break.

### Option B: reply with vNext bytes to every sufficiently long input

This is not actionable to a legacy peer and creates unnecessary reflection and
amplification behavior.

### Option C: implement a legacy discriminator and rejection packet

This could improve an old client's message but reintroduces a legacy packet and
transport surface expressly excluded by the program rules.

## Decision 5: API and mutation boundary

### Option A: owned codec plus pure negotiation (approved)

Factories validate locally created version ranges and offers. Payload decoders
return either a fully owned immutable value or a closed enum/numeric error.
`negotiateClientHello` is a pure operation over a validated client hello and
server offer, returning either `ServerHello` or `SessionRejected`.

The API does not authenticate, retain connection state, log, send, allocate a
session, or mutate canonical state. Slice 4.3 owns legal transitions, timeout,
cancellation, redaction, and lifetime retention of the negotiated result.

### Option B: decode directly into a session handler

This reduces calls but makes malformed input, negotiation, and transition
atomicity harder to prove independently.

### Option C: exceptions or free-form diagnostics

This would make stable categorization, bounded failure behavior, and redaction
dependent on call-site discipline.

## Approved acceptance tests and demo

1. Exact golden payload bytes and owned encode/decode round trips pass for all
   three schemas.
2. Current and previous minor offers select the highest common minor.
3. Major mismatch and no minor overlap reject before authentication.
4. Unknown optional IDs are ignored and absent from the negotiated set.
5. Unsupported requirements in either direction reject with the lowest missing
   stable ID.
6. Zero, unsorted, duplicate, overlapping, and over-limit vectors reject before
   negotiation.
7. Every truncation, wrong file identifier, length mismatch, trailing byte, and
   representative semantic mutation returns no partial owned value.
8. The negotiated set is sorted, immutable, bounded, and independent of engine,
   build, and platform identity.
9. Short, valid-vNext, and legacy/unknown preambles produce the three approved
   classifications; non-vNext input produces no wire payload.
10. Production schemas and committed generated headers reproduce exactly with
    pinned FlatBuffers `25.12.19` and the restricted generator arguments.
11. The handshake decoder has a bounded corpus-backed libFuzzer target in the
    existing ASan+UBSan evidence path.
12. Protocol public headers expose no FlatBuffers-generated, OpenMW, transport,
    platform, or test-support type.

The implementation demo presents the three golden payloads, current/previous
minor success, ignored optional capability, both required-capability directions,
version rejections, malformed-vector rejection, source-buffer overwrite, and
legacy/no-reply classification. Owner acceptance remains required before Slice
4.2 becomes **Implemented**.

## Consequences

- The protocol now has a small stable capability-ID namespace and three initial
  FlatBuffer schemas, but no capability meaning is assigned yet.
- A connection's version/capability result is complete before authentication or
  session creation and can be retained immutably by Slice 4.3.
- Rollout support remains bounded to one major and two adjacent minors.
- Operators cannot expand handshake work or wire compatibility with a setting.
- Old TES3MP clients do not receive a special old-wire message; clean-break
  incompatibility remains operationally explicit without reintroducing legacy
  code.

## Failure modes and mitigations

- **FlatBuffers view escapes:** generated headers and runtime include paths are
  private; public APIs contain only project-owned values.
- **Capability smuggling:** only the negotiated intersection enables behavior;
  field presence and unknown optional IDs do not.
- **Requirement checked one way:** named tests cover client-required and
  server-required failures and choose the lowest missing ID across both.
- **Unbounded list work:** fixed counts are checked before owned vector copy;
  vectors must already be sorted and disjoint.
- **Ambiguous rejection context:** reason-specific semantic validation requires
  an offending ID exactly when the capability reason is used.
- **Schema/generator drift:** the existing pinned proof runner now regenerates
  production schemas and compares every committed generated byte.
- **Legacy response becomes a compatibility shim:** the preamble classifier
  returns only a local disposition and never encodes an old packet.
- **Release version chosen accidentally:** the codec accepts validated policy
  values; a composition/release slice must deliberately select the advertised
  current range.

## Review and replacement triggers

Reopen this ADR if:

- live capability renegotiation is required;
- more than 32 optional or required capabilities are genuinely needed;
- capability IDs require namespacing, allocation authorities, or a width change;
- more than current-plus-previous minor or cross-major interoperability is
  required;
- a safe, supportable legacy rejection path becomes possible without importing
  legacy packets or transport;
- authentication must influence version/capability selection;
- the one-request/one-response exchange cannot express an approved feature; or
- the restricted FlatBuffers profile cannot enforce these bounds.

## Owner approval

Approved by the project owner in the 2026-08-27 working session: Option A for
Decisions 1 through 5 without amendment.

Implementation-demo acceptance remains separate. This approval does not cover
authentication, session transitions, transport behavior, gameplay capability
semantics, authority, state scope, or a public protocol release version.
