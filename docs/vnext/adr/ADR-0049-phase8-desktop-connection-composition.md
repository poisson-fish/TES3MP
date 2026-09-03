# ADR-0049: Phase 8 desktop connection composition

Status: **Accepted**

Date opened: 2026-09-02

Decision owner: project owner

Needed by: Phase 8 Slice 8.3

## Decision

The project owner approved Option A for all decisions on 2026-09-02:

1. multiplayer is explicitly enabled by `tes3mp-enable`, defaulting false;
2. host and port use separate validated `tes3mp-host` and `tes3mp-port` values;
3. optional join material is read once from `tes3mp-password-file`, moved into
   client-session memory, cleared from temporary storage, and never logged;
4. sanitized startup failures stop launch, while runtime failures pass through
   a narrow adapter status provider; and
5. enabled mode requires a validated `tes3mp-timeout-ms` value from 1 through
   60,000 rather than selecting a release default.

During implementation, linking the previously standalone Windows `/MT` network
profile into OpenMW's dependency graph exposed incompatible C runtimes. The
owner approved and confirmed Option C: every Windows TES3MP network build uses
`/MD` (and `/MDd` for Debug). OpenMW's pinned dependency bundle must therefore
be rebuilt for `/MD` before the desktop link can pass. This keeps one
allocator/runtime ABI across desktop, server, and headless processes, requires
the Visual C++ runtime at deployment, and invalidates the earlier Windows
network and OpenMW dependency proofs until rebuilt.

Supplying connection values does not implicitly enable multiplayer. Disabled
mode constructs no transport, runtime, coordinator, or provider. Resume tokens
remain memory-only. The already verified Phase 7 outbound queue profile is
reused without changing queue authority or gameplay behavior.

## Alternatives considered

Endpoint-implied enablement and an always-created dormant runtime were rejected
because they weaken disabled-mode equivalence. A combined `host:port` value and
a fixed development endpoint were rejected in favor of typed validation.
Configuration-file passwords were rejected because reusable secrets would be
retained in ordinary configuration; an interactive prompt is deferred UI work.
Log-only runtime failures are not actionable, while a dedicated multiplayer
screen is larger than this slice. Fixed or independently split timeout defaults
were rejected because no desktop release policy has been approved.

## Acceptance

- Disabled mode creates nothing and preserves baseline startup/frame behavior.
- Enabled mode rejects missing or invalid host, port, timeout, and password
  input before engine execution with sanitized actionable categories.
- Credential bytes are bounded, cleared after transfer, and absent from logs,
  errors, tests, and retained evidence.
- The executable attaches exactly one coordinator only after successful
  transport/runtime startup.
- Version/capability rejection, authentication rejection, timeout, transport
  failure, and disconnect produce closed status categories without secrets.
- Existing frame-order, bounded-work, shutdown-order, target-boundary, patch
  registry, and multiplayer-disabled contracts continue to pass.

## Owner approval

Approved by the project owner on 2026-09-02: Option A for enablement, endpoint,
credential acquisition, actionable errors, and explicit timeout configuration.
