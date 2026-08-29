# ADR-0014: Phase 3 target topology and boundary enforcement

Status: **Accepted**

Date opened: 2026-08-26

Date approved: 2026-08-26

Date amended: 2026-08-28

Decision owner: project owner

Needed by: Phase 3 Slice 3.1

## Decision question

How should the first production multiplayer targets be divided, named, placed,
and mechanically prevented from acquiring forbidden OpenMW or transport-library
dependencies?

This decision fixes build topology only. It does not define protocol fields,
canonical state, gameplay behavior, OpenMW hook call sites, transport behavior,
or a server process composition root.

The owner-approved Phase 6 refinement in
[`ADR-0034`](ADR-0034-phase6-credential-and-resumption-boundary.md) keeps this
exact project-target graph. It permits `tes3mp_server_core` to use the already
verified OpenSSL crypto library privately in the real-network profile; no
OpenSSL type may enter a public header, and canonical reducer APIs/tests remain
independent of that implementation.

## Accepted constraints

The topology must preserve the vNext README, ADR-0004, ADR-0005, ADR-0007, and
ADR-0013:

1. Protocol, transport abstraction, server core, client session, and reusable
   test support remain independent of OpenMW, rendering, VR, operating-system,
   and selected transport-library types.
2. The OpenMW adapter is the only initial multiplayer target allowed to depend
   on `openmw-lib` or application-local OpenMW headers.
3. GameNetworkingSockets integration remains a Phase 6 adapter concern; the
   Phase 3 transport target contains only project-owned abstractions.
4. Target and runtime names do not use `vnext` as permanent product identity.
5. Dependency violations fail during configuration or a focused boundary test,
   rather than relying on reviewer memory.
6. Slice 3.1 adds empty buildable seams only. Later slices own public value
   types, canonical primitives, deterministic facilities, faults, and telemetry.

## Scenarios evaluated

1. A protocol source accidentally includes an OpenMW world header.
2. Server-core code directly includes GameNetworkingSockets.
3. Client-session code links the OpenMW adapter to reuse a convenience helper.
4. Test support needs to compose fake protocol, transport, server, and client
   facilities without becoming a production dependency.
5. OpenMW is disabled while engine-independent targets are still configured and
   buildable.
6. A future target is added without declaring its direct dependency allowance.
7. An OpenMW baseline update changes the monolithic `openmw-lib` target.

## Options considered

### Option A: separate TES3MP libraries plus one app-local adapter (approved)

Create these concrete CMake targets:

| Target | Location | Allowed direct project dependencies |
|---|---|---|
| `tes3mp_protocol` | `components/tes3mp/protocol` | none |
| `tes3mp_transport` | `components/tes3mp/transport` | `tes3mp_protocol` |
| `tes3mp_server_core` | `components/tes3mp/server_core` | `tes3mp_protocol` |
| `tes3mp_client_session` | `components/tes3mp/client_session` | `tes3mp_protocol`, `tes3mp_transport` |
| `tes3mp_test_support` | `components/tes3mp/test_support` | the four engine-independent production targets |
| `openmw_tes3mp_adapter` | `apps/openmw/tes3mp` | `tes3mp_client_session`, `openmw-lib` |

Expose stable `TES3MP::Protocol`, `TES3MP::Transport`,
`TES3MP::ServerCore`, `TES3MP::ClientSession`, `TES3MP::TestSupport`, and
`TES3MP::OpenMWAdapter` build aliases. Aliases are build vocabulary, not C++
API namespaces or protocol identity.

Each target begins as a real C++20 static library with a private translation-
unit anchor. This proves compiler and linker participation while leaving public
APIs to their owning slices. Engine-independent targets are configured outside
OpenMW's existing monolithic `components` library and exist even when OpenMW is
disabled. The adapter is configured only with the OpenMW application target.

A project-owned CMake verifier checks every target's direct link dependencies
against its declared allowlist and scans engine-independent sources for known
forbidden include families. Focused tests construct allowed and deliberately
invalid miniature target graphs so both checks are demonstrated to fail closed.
Later Phase 3 slices add compile-time public-header isolation checks as concrete
headers appear.

### Option B: one combined TES3MP core, split later

Place protocol, session, server, and test code in one library until more code
exists. This reduces initial CMake declarations but permits accidental reverse
dependencies and makes a later split a migration rather than an enforced
starting property.

### Option C: use OpenMW's `components` and `openmw-lib` targets

Add engine-neutral multiplayer files to the existing OpenMW libraries and keep
only logical namespaces separate. This follows the current build style but the
result transitively inherits OpenMW, renderer, SDL, OSG, Lua, and platform
dependencies, contradicting the accepted independent-core boundary.

### Option D: separately configured nested CMake project

Build all TES3MP libraries as a standalone nested project and import them into
OpenMW. This provides strong isolation, but creates unnecessary configure,
toolchain, test-discovery, packaging, and IDE coordination before the existing
top-level build has demonstrated a limitation.

## Decision

The project owner approved Option A on 2026-08-26 together with the Phase 2 exit
review. Slice 3.1 implements the exact target set, directories, dependency
allowlist, negative boundary checks, and build aliases above.

## Acceptance tests and evidence

Slice 3.1 must demonstrate:

1. all five engine-independent targets configure and build with `BUILD_OPENMW=OFF`;
2. the OpenMW adapter target configures in the normal OpenMW build graph;
3. the recorded direct dependency graph matches the allowlist;
4. adding an undeclared direct target dependency fails a focused configuration;
5. adding a known forbidden OpenMW/renderer/transport include to an engine-
   independent source fails a focused configuration;
6. the test-support target is never a dependency of a production target;
7. no target name uses `vnext`; and
8. indexed baseline provenance and repository-owned tests remain green.

Phase 3's exit gate still requires stronger compile-time forbidden-header
evidence once public headers exist, deterministic trace/checksum behavior, and
the remaining Slice 3.2 through 3.7 deliverables.

## Consequences

- More small targets and explicit links exist from the first production slice.
- OpenMW's large `components` library cannot become a convenience dependency of
  engine-independent multiplayer code.
- Test support can compose all independent layers but cannot leak into them.
- The adapter is an explicit leaf integration target and does not yet alter the
  OpenMW executable or runtime lifecycle.
- Empty anchor translation units are temporary proof artifacts and may be
  removed when a target gains real implementation files.
- The allowlist describes direct build dependencies. Public API include
  hygiene, symbol ownership, and runtime direction receive additional checks as
  those APIs appear.

## Failure modes and mitigations

- **A dependency is hidden in a source include:** scan known forbidden include
  families now and add compile-time poisoned-header checks with public APIs.
- **A link is hidden in a generator expression:** reject unsupported direct-link
  expressions in the verifier until they have an explicit reviewed policy.
- **Test support becomes production state:** prohibit it in every production
  allowlist and test the reverse dependency condition.
- **The adapter becomes a second engine:** keep it empty in Slice 3.1 and require
  the Phase 8 exact hook inventory before lifecycle or mutation code lands.
- **Target names churn:** use permanent TES3MP names now; changing them reopens
  this ADR and updates build, CI, packaging, and downstream documentation.
- **OpenMW changes `openmw-lib`:** revisit only the adapter edge; independent
  targets remain unaffected.

## Review and replacement triggers

Reopen this ADR if:

- an engine-independent target needs an OpenMW, renderer, platform, scripting,
  persistence, or selected transport-library dependency;
- the direct dependency direction must change;
- the adapter cannot remain an app-local leaf target;
- a separately configured project becomes necessary for supported toolchains or
  packaging; or
- target naming or installation/export becomes a public downstream contract.

## Owner approval

Approved by the project owner in the 2026-08-26 working session: Option A.

The same review explicitly approved the Phase 2 exit and movement into Phase 3.
Implementation-demo acceptance remains separately required before Slice 3.1 is
marked **Implemented**.
