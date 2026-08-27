# ADR-0021: Bounded protocol framing and decode boundary

Status: **Accepted**

Date opened: 2026-08-27

Date approved: 2026-08-27

Decision owner: project owner

Needed by: Phase 4 Slice 4.1

## Decision questions

How does an untrusted byte message identify vNext framing, declare its payload
size, class, and stable kind before a payload decoder runs; what fixed byte
budgets apply to each delivery class; and what owned success and structured
failure values may cross the protocol decode boundary?

This decision defines framing and bounded dispatch only. It does not define
capability negotiation fields, authentication claims or providers, session
state transitions, reliable-operation contents, snapshot contents, transport
channels, authority, state scope, or gameplay behavior.

## Existing constraints

The choice must preserve ADR-0003, ADR-0004, ADR-0013, ADR-0014, and the Phase 4
plan:

1. Oversized input is rejected before payload decoding or allocation.
2. The received byte count must exactly match the declared frame size; trailing
   and concatenated bytes are not accepted as one frame.
3. FlatBuffers verification and semantic conversion remain separate later
   payload-decoder stages. Generated views never cross the codec boundary.
4. Reliable apply-once operations and latest-wins sampled state remain distinct
   message classes.
5. Unknown kinds, unsupported framing formats, and class/kind mismatches fail
   clearly instead of being inferred from payload contents.
6. Stable framing and kind identifiers are never reused for another meaning.
7. Decode errors contain no packet bytes, credentials, addresses, dependency
   diagnostics, or user-provided text.
8. Protocol remains independent of OpenMW, transport libraries, operating-
   system APIs, rendering, and test support.

## Scenarios evaluated

1. A peer sends every truncation of a valid frame, a mismatched length, an extra
   trailing byte, or two concatenated frames.
2. A control message declares a large payload intended for a snapshot so that
   the control path would allocate or verify disproportionate input.
3. A peer uses a known kind with the wrong delivery class or sends an unknown
   kind whose payload resembles a known FlatBuffer.
4. A transport recycles or overwrites its receive buffer immediately after
   decode while the accepted frame remains queued for a payload decoder.
5. A malformed authentication-bearing message causes a diagnostic. The error
   can identify the safe category and numeric bounds without retaining secret
   bytes or free-form text.
6. A later protocol release adds a message kind or changes its payload schema
   without changing the outer frame layout.
7. A future outer framing change cannot be parsed safely under format version
   one and therefore needs an explicit new framing-format version.

## Decision 1: outer frame

### Option A: fixed owned header plus FlatBuffers payload (approved)

Use a 12-byte project-owned, little-endian header:

| Offset | Width | Field |
|---:|---:|---|
| 0 | 4 | ASCII magic `T3MP` |
| 4 | 1 | framing-format version, initially `1` |
| 5 | 1 | closed message class |
| 6 | 2 | globally stable message-kind identifier |
| 8 | 4 | payload byte length |

The payload length excludes the header. A decoder first validates the complete
header, known class/kind mapping, non-zero payload length, class budget, checked
total-size arithmetic, and exact received length. Only then may it copy the
payload or invoke a later payload decoder.

Beginning in Slice 4.2, each payload is a size-prefixed FlatBuffer with its own
file identifier. The inner size prefix permits the FlatBuffers decoder to reject
trailing bytes inside the declared payload independently of the outer frame.
The outer framing-format version is not the negotiated vNext protocol version.
Format version `1` adds no checksum, compression flag, transport channel, or
platform identity; integrity and transport behavior remain later adapter work.

### Option B: FlatBuffers envelope union

Use one size-prefixed FlatBuffers root for class, kind, and a union payload.
This removes the fixed header but requires generic verification before the
message class can select a budget and makes every kind part of one central union.

### Option C: length prefix only

Prefix an otherwise self-identifying payload with only its size. This is small,
but cannot reject an incompatible format, unknown kind, or abusive class before
payload inspection and gives legacy/random input no recognizable vNext boundary.

## Decision 2: message classification and identifiers

### Option A: three closed classes and globally stable kind IDs (approved)

Define `SessionControl`, `ReliableOperation`, and `LatestWinsSnapshot` as closed
classes. A compile-time descriptor maps every known kind to exactly one class
and class budget. Field presence never changes the class.

Reserve these initial identifiers:

| Identifier | Kind | Class | Payload owned by |
|---:|---|---|---|
| `0x0001` | `ClientHello` | `SessionControl` | Slice 4.2 |
| `0x0002` | `ServerHello` | `SessionControl` | Slice 4.2 |
| `0x0003` | `SessionRejected` | `SessionControl` | Slice 4.2 |
| `0x0100` | `ReliableOperation` | `ReliableOperation` | Slice 4.4 |
| `0x0200` | `LatestWinsSnapshot` | `LatestWinsSnapshot` | Slice 4.4 |

Slice 4.1 establishes only identifiers, classification, and framing. It does
not add placeholder payload schemas or settle the behavior of the later kinds.
Unknown kind values and known kinds paired with the wrong class are rejected.
New IDs land with their owning slice and are never reused.

### Option B: infer class from identifier ranges

Encode the delivery class implicitly in numeric ranges. This saves one header
byte but permanently couples scheduling semantics to identifier allocation and
makes accidental reclassification harder to detect.

### Option C: generic reliable/unreliable flags

Use transport-like flags instead of semantic classes. This cannot strongly
distinguish reliable apply-once operations from replaceable latest-wins samples
and lets transport policy leak into protocol meaning.

## Decision 3: byte budgets

### Option A: fixed per-class payload budgets (approved)

Use these protocol constants:

| Class | Maximum payload bytes |
|---|---:|
| `SessionControl` | 4 KiB |
| `ReliableOperation` | 16 KiB |
| `LatestWinsSnapshot` | 64 KiB |

The header is excluded. Exact-limit payloads are accepted by framing;
limit-plus-one is rejected before copying or payload verification. A later
message descriptor or schema may impose a smaller limit. Messages larger than
their fixed budget must use an explicitly designed bounded chunking protocol;
operators cannot raise these wire limits through configuration.

### Option B: one 64 KiB budget

Use the largest initial budget for every kind. This is simple, but allows a
handshake/control peer to consume snapshot-sized framing work and memory.

### Option C: server-configurable budgets

Let each operator tune maximum frame sizes. This is flexible, but turns wire
interoperability and hostile-input resistance into configuration-dependent
behavior and complicates clients, tests, and operational support.

## Decision 4: result ownership and structured errors

### Option A: owned payload and closed error values (approved)

A successful decode returns an owned `DecodedFrame` containing the validated
class, kind, and one bounded payload byte vector. The caller's input storage may
be discarded immediately. A failed decode returns no partial frame and performs
no payload allocation.

Errors use closed stage and code enums plus bounded numeric context: observed
format/class/kind values, observed bytes, and the applicable limit. They contain
no dynamic string, raw byte span, exception text, credential, address, pointer,
or user-provided field. The API uses an explicit value result rather than
exceptions; diagnostics do not decide session or canonical behavior.

### Option B: borrowed payload views

Return a span into transport-owned input. This avoids a copy, but permits packet
buffer lifetime to escape into dispatch/session code and makes asynchronous
retention unsafe.

### Option C: exceptions or diagnostic strings

Report failures through thrown exceptions or formatted text. This is familiar,
but allocation, redaction, stable categorization, and fuzz assertions depend on
call-site discipline.

## Acceptance tests and demo

Slice 4.1 must demonstrate:

1. exact framing-format-one golden bytes and encode/decode round trips for all
   five initial known kinds;
2. rejection of every header truncation, bad magic, unsupported version,
   unknown class/kind, class/kind mismatch, zero payload, length mismatch,
   trailing byte, concatenated frame, and checked-size failure;
3. exact-limit acceptance and limit-plus-one rejection for every class;
4. no payload allocation and no partial result on every failure path;
5. accepted payload ownership after the input buffer is overwritten;
6. closed structured error stage/code/context values with no public text or raw
   byte field;
7. bounded mutation/property cases and a corpus-backed frame-decoder fuzz target
   under the existing ASan+UBSan profile;
8. protocol public headers and implementation build without OpenMW,
   GameNetworkingSockets, platform, FlatBuffers-generated, or test-support
   headers; and
9. applicable standalone contracts, repository policy tests, baseline
   provenance, legacy exclusion, and staged-diff checks remain green.

The implementation demo presents a golden frame, successful owned round trip,
source-buffer overwrite, representative structured failures, and the three
class boundary checks. Owner acceptance remains required before Slice 4.1 is
marked **Implemented**.

## Consequences

- Twelve bytes of explicit framing and a second length inside later FlatBuffers
  payloads buy two independently testable hostile-input boundaries.
- Early classification permits class-specific work and memory rejection without
  invoking generated code.
- A successful decode copies at most 64 KiB once. Avoiding that copy would need
  a separately approved lifetime design and measured need.
- Fixed limits make abuse behavior and interoperability deterministic, but
  future large state transfers need bounded chunking rather than a config knob.
- The stable kind table begins before payload schemas; each later slice still
  owns and gates its message fields and behavior.

## Failure modes and mitigations

- **Integer overflow while computing total bytes:** subtract/compare against the
  received span and class budget before addition; cover hostile values directly.
- **Class smuggling:** compare every known kind with its compiled class and fail
  before copying the payload.
- **Trailing payload bytes hidden inside a valid outer frame:** require the later
  size-prefixed FlatBuffer length to equal the declared payload length exactly.
- **Error values become a logging side channel:** keep error shapes enum/numeric
  only and add compile-time/public-API canary assertions.
- **A 64 KiB snapshot becomes an unbounded collection:** framing is only the
  outer limit; each later schema still needs depth, table, collection, string,
  allocation, and semantic limits.
- **Stable IDs are reassigned:** retain golden framing fixtures and extend a
  repository registry/policy check with each new kind.
- **Framing version is confused with protocol version:** name and test them as
  separate values; version negotiation remains inside Slice 4.2 messages.

## Review and replacement triggers

Reopen this ADR if:

- a supported transport cannot preserve one complete bounded frame per received
  message;
- measurements justify borrowed buffer ownership or a larger class limit;
- chunking, compression, or integrity must become an outer protocol concern;
- more delivery classes or live class changes become necessary;
- a payload format cannot provide its own exact-length and identifier check; or
- framing format `1` cannot safely express a required outer change.

## Owner approval

Approved by the project owner in the 2026-08-27 working session: Option A for
Decisions 1 through 4 and the proposed Phase 4 slice boundaries.

Approval fixes framing, classification, byte budgets, and owned structured
decode results only. It does not approve capability/authentication fields,
session transitions, transport behavior, authority, state scope, gameplay,
operation contents, or snapshot contents.
