# ADR-0031: Phase 5 committed domain sink boundary

Status: **Accepted**

Date opened: 2026-08-28

Date approved: 2026-08-28

Decision owner: project owner

Needed by: Phase 5 Slice 5.6

## Decision questions

Which production component owns the persistence, replay, script, and metrics
sink ports; what identical committed domain value does every role receive; at
what point relative to canonical state and latest-publication installation may
delivery occur; how are absence, backpressure, and failure reported without
changing canonical results; and what fan-out, lifetime, retention, retry, and
reentrancy bounds keep an auxiliary consumer from blocking or becoming a
second mutation authority?

These are architecture, ownership, state-scope, ordering, lifetime, and
failure-policy decisions. Production sink interfaces or reducer dispatch must
not land until the project owner approves or amends this ADR. This proposal
does not select a script runtime, database, durable commit point, replay file
format, production queue/thread, target-session projection, audit policy,
canonical state field, authority rule, or gameplay behavior.

## Decision summary

The project owner approved Option A for Decisions 1 through 5 and the proposed
acceptance scenarios on 2026-08-28:

1. server core owns four nominal role-specific sink ports and one explicit
   non-owning bundle with at most one persistence, replay, script, and metrics
   consumer; test support may own bounded fakes, but no backend target lands;
2. every configured role receives the exact same shared immutable
   `CanonicalStatePublication` handle once per publication containing committed
   changes, after that handle is installed as latest; version zero and
   no-commit batches produce no delivery;
3. the reducer performs at most four direct non-blocking `noexcept` attempts in
   fixed persistence, replay, script, metrics order after publication, without
   exposing reducer access or adding registration, locks, workers, or queues;
4. each role reports absent, accepted, backpressured, or failed in a complete
   delivery report; every configured role is attempted, and no result may
   roll back, retry, short-circuit, or relabel the already committed reduction;
   and
5. server core keeps only its existing latest publication and never redelivers.
   A sink may retain the shared handle only behind its own separately approved
   bounded adapter; later role-owning phases decide gap, startup, durability,
   shutdown, and operator policy without gaining mutable canonical access.

## Existing constraints

1. ADR-0003 requires bounded work and explicit overload/failure behavior under
   hostile or failed inputs.
2. ADR-0006 permits only server canonical reducers to commit state. Scripts,
   persistence, replay, metrics, clients, and administrators receive immutable
   results and no bypass mutation API.
3. ADR-0013 requires deterministic ordering, checked counters, and no wall-clock
   or thread-race influence on canonical results.
4. ADR-0014 keeps these domain ports in the engine-independent server-core
   target unless an approved target-topology amendment says otherwise.
5. ADR-0020 defines metrics and structured events as separate best-effort
   observability values. Observability is not persistence, replay, audit, or a
   substitute for the committed domain feed.
6. ADR-0028 makes the writer-confined reducer the sole canonical install path
   and preserves earlier per-command commits if a later command fails.
7. ADR-0029 defines a complete immutable batch publication containing the final
   snapshot and ordered replacement-shaped committed change records. It rejects
   callbacks during individual commits and retains only the latest publication.
8. ADR-0030 adds the complete Phase 5 checksum and idempotency state to that
   publication without adding a protocol or runtime delivery action.
9. Phase 19 owns script language/runtime, API projection, callback limits, and
   script-command scheduling. Phase 20 owns persistence technology, schemas,
   durability acknowledgement, replay format, and the policy for a failed
   required durable consumer.

## Representative scenarios

1. A reducer is constructed at version zero with no committed changes. No sink
   runs during construction.
2. An accepted command commits player and session replacements, installs one
   immutable publication, and then offers that exact shared handle once to each
   configured role.
3. An exact-next rejected command commits only acknowledgement/idempotency state
   and is delivered identically to every role; sinks cannot confuse finalization
   with gameplay success.
4. A batch commits several commands. Every role receives one batch handle whose
   records are contiguous and whose snapshot/checksum describe the final commit.
5. Unknown-session, old-generation, already-finalized, sequence-gap, empty, and
   fully preflight-rejected batches publish and deliver nothing.
6. Persistence accepts, replay backpressures, script fails, and metrics is
   absent. All configured roles are attempted exactly once, the report preserves
   each outcome, and state/version/publication remain the committed result.
7. A consumer retains an old immutable handle while later batches commit. The
   writer replaces latest publication without waiting, and retained bytes cannot
   be mutated.
8. A consumer misses a publication. The next complete publication exposes the
   version gap; server core neither guesses role policy nor creates unbounded
   replay work.
9. A future script adapter maps the domain record into versioned script events,
   but receives neither packet buffers nor a mutable reducer reference and
   submits commands only through the normal intake path.
10. A future persistence adapter cannot accept work. The Phase 5 report exposes
    failure after the in-memory commit; Phase 20 must fail closed or coordinate
    durability before any external surface claims that commit is durable.

## Decision 1: interface ownership and role topology

### Option A: four server-core-owned nominal ports and a bounded bundle (approved)

Define distinct persistence, replay, script, and canonical-metrics sink
interfaces in `tes3mp_server_core`. Group optional non-owning references in one
explicit bundle, with at most one sink per role. The reducer owns neither sink
nor backend lifetime. `tes3mp_test_support` may add bounded recording fakes for
contract evidence, but Slice 5.6 adds no database, script runtime, replay store,
exporter, new production target, or dependency.

Nominal ports prevent accidental role swapping and leave later phases free to
apply stronger composition policy per role. A fixed four-slot topology gives
the writer a compile-time fan-out bound and avoids arbitrary registration.

### Option B: one generic sink interface and an arbitrary subscriber list

Give every consumer the same callback type and register any number at runtime.
This reduces declarations but loses role identity, permits unbounded fan-out,
and makes required-versus-best-effort policy difficult to review.

### Option C: put each port in its future backend component

Let scripting and persistence own separate interfaces later. This defers the
choice but makes the canonical reducer depend outward on components that do not
yet exist or requires a second adapter boundary after gameplay expands.

### Option D: expose backend libraries directly

Use database, script-runtime, replay-framework, or exporter-native APIs from
server core. This violates the owned engine-independent boundary and selects
Phase 19/20 dependencies prematurely.

## Decision 2: common payload and delivery point

### Option A: one shared immutable installed publication per committed batch (approved)

Use the existing `std::shared_ptr<const CanonicalStatePublication>` as the
delivery value. It already owns the final complete state, state version,
checkpoint tick, checksum, and ordered `CanonicalStateChangeRecord` values.
The reducer first installs that handle as the latest publication, then offers
the exact same handle identity to every configured role. Sinks therefore see
only committed state and can verify that the reducer's latest publication is
the delivered object.

Deliver once for a batch containing one or more committed records. Do not
deliver the constructor's version-zero publication, an empty batch, or a batch
with no canonical commit. A partial batch that publishes earlier successful
commits before returning/throwing still delivers that committed publication.

### Option B: invoke sinks for each individual change during reduction

This minimizes latency but reintroduces callbacks inside the per-command
mutation sequence, exposes a partially completed batch, and conflicts with
ADR-0029's atomic reader-visible batch publication.

### Option C: give each role a separately projected payload

This may reduce data per consumer, but four projections can disagree about the
same commit and make the reducer choose future script, database, replay, and
metric schemas. Later adapters should project from one domain value.

### Option D: offer a candidate before installation and allow veto

This can coordinate durability before memory commit, but turns auxiliary sinks
into mutation authority and imports Phase 20 durability policy into Phase 5.

## Decision 3: invocation, ordering, and reentrancy boundary

### Option A: fixed post-publication bounded `try` fan-out (approved)

After installing a non-empty publication, make one direct `noexcept`
non-blocking attempt for each configured role in fixed persistence, replay,
script, metrics order. The maximum work initiated by server core is four calls
per committed batch. Sink interfaces receive only the const shared publication,
not a reducer, command intake, callback registrar, mutable state, or completion
continuation.

The core owns no mutex across a call, worker, condition variable, task,
registration list, or queue. The contract prohibits blocking, reentrant reducer
entry, and exceptions. Later adapters that need asynchronous work must perform
only bounded enqueue/copy work inside the attempt and prove their isolation in
their owning phase.

### Option B: a core-owned asynchronous dispatcher

Add workers and queues now. This could isolate backend latency, but prematurely
fixes capacity, allocation, shutdown, ordering, and thread-failure behavior
without a production composition root.

### Option C: pull-only consumers of `latestPublication()`

Keep the reducer unchanged and let all roles poll. This inherently isolates the
writer, but cannot prove that every configured role was offered each committed
record and makes loss invisible until each role implements its own polling.

### Option D: caller-managed fan-out after `apply()`

Return a publication and rely on the composition root to notify sinks. This
keeps dispatch outside the reducer but permits omission, delivery of a stale
handle, or delivery before latest-publication installation.

## Decision 4: result and failure policy

### Option A: complete per-role report with no canonical control effect (approved)

Define closed per-role results: `NotConfigured`, `Accepted`, `Backpressured`,
and `Failed`. Return a complete delivery report with the batch reduction result
without changing its canonical success/error meaning. Attempt all configured
roles even when an earlier role backpressures or fails. Never roll back an
installed commit, retry, wait, terminate, skip a later role, or relabel a
command disposition because of sink delivery.

Add closed low-cardinality ADR-0020 observation categories for role and result;
observation acceptance or loss cannot alter the report, fan-out, or state.
`NotConfigured` is explicit in the report and is not emitted as a backend
failure. This makes Phase 5 failure visible while leaving Phase 19/20 free to
decide whether the runtime disables a role, pauses intake, fails closed, or
delays an external durability acknowledgement.

### Option B: silently ignore every sink result

This resembles best-effort metrics but cannot meet the explicit failure-policy
gate for persistence, replay, or scripts and could claim healthy operation
after losing required work.

### Option C: roll back or reject the canonical commit on sink failure

The sink is called only after immutable publication installation, so rollback
would require a second mutation path and make results depend on auxiliary
availability.

### Option D: stop fan-out at the first failure

This bounds work slightly further but couples independent roles by order and
silently deprives later sinks of an already committed record.

## Decision 5: lifetime, retention, retry, and later-role policy

### Option A: existing latest retention with no core retry or redelivery (approved)

Server core retains only ADR-0029's latest immutable publication. A sink may
copy the shared handle during its bounded attempt, but its adapter must impose
and test a separately approved queue/retention limit. Server core never waits
for release, tracks per-sink acknowledgement, keeps a retry ring, or redelivers.

A later offered publication is self-contained and exposes any state-version
gap. Whether a role may recover from the included snapshot or must instead
pause/fail closed is deliberately owned by that role's phase. In particular,
Phase 20 must not treat a full snapshot as proof that missing replay records
were durably recorded, and Phase 19 must define deterministic script failure
and reload behavior. Startup/bootstrap delivery and shutdown draining also
remain in those phases.

### Option B: one core-owned bounded queue per role

This makes retry concrete, but four queue capacities, overflow policies,
workers, shutdown rules, and loss guarantees would be selected before the
backends or their required semantics exist.

### Option C: retain publications until every sink acknowledges

This can provide lossless delivery but makes memory and writer progress depend
on the slowest or failed auxiliary consumer.

### Option D: silently latest-win for every role

This is bounded and useful for metrics, but persistence/replay can lose causal
records and scripts can skip behavior without an explicit role policy.

## Acceptance tests and demo

The approved Slice 5.6 implementation must add named tests for:

1. server-core sink headers compile without OpenMW, transport-library,
   script-runtime, database, exporter, platform, or test-support headers;
2. the four nominal ports and bounded bundle permit at most one sink per role
   and introduce no new production target dependency;
3. construction/version zero, empty batches, and no-commit batches deliver
   nothing;
4. an applied commit installs latest publication before all four roles receive
   the exact same shared handle once;
5. acknowledgement-only duplicate/rejection commits use the same delivery path
   and retain their exact final disposition;
6. multiple commits produce one delivery containing contiguous ordered records
   and the exact final snapshot/version/tick/checksum;
7. fixed persistence/replay/script/metrics order is deterministic;
8. accepted, backpressured, failed, and absent roles produce the exact complete
   report while every configured role is attempted;
9. sink results and sink-observation acceptance/drop do not change canonical
   state, version, checksum, publication, dispositions, or later-role delivery;
10. a retained old handle remains immutable and cannot delay replacement of the
    reducer's latest publication;
11. a missed publication is visible as a version gap without core retry,
    acknowledgement, or unbounded retention; and
12. public APIs expose no mutable reducer/state access, callback registration,
    arbitrary subscriber collection, backend type, queue, worker, wall clock,
    wire payload, or free-form metric label.

The owner demo should show one committed multi-change batch delivered as the
same installed immutable object to four role-specific recorders; exact role
order and mixed accepted/backpressured/failed/absent results; an
acknowledgement-only record; no delivery for no-commit work; unchanged canonical
output across delivery outcomes; and a retained old handle that cannot block a
later commit.

## Consequences

- One server-core domain value remains the source for future database, replay,
  script-event, and metric adapters, avoiding four mutation-time projections.
- Nominal roles and fixed fan-out make dependency, ordering, and work bounds
  mechanically testable, at the cost of four small interfaces.
- Post-publication attempts cannot prevent or undo an in-memory canonical
  commit. Later durability acknowledgement and fail-closed runtime behavior
  remain explicit Phase 20 work.
- Direct attempts avoid premature threads and queues. Production adapters must
  still prove bounded non-blocking behavior and shutdown policy.
- Full publications cost shared ownership rather than four deep copies. A sink
  that retains them owns its own bounded memory policy.

## Failure modes and mitigations

- **A sink sees candidate or partial state:** install one complete immutable
  publication before fan-out and compare handle identity in tests.
- **One role receives a different projection:** accept only the shared canonical
  publication in all four nominal interfaces.
- **A consumer becomes mutation authority:** expose no reducer or mutable state;
  script/admin changes return through normal command intake in later phases.
- **Failure of one role hides later delivery:** attempt all four fixed slots and
  return every result.
- **A sink blocks or reenters:** require direct bounded non-blocking `noexcept`
  attempts, hold no core lock, expose no reentry capability, and require adapter
  evidence before production composition.
- **Retained work grows with a failed consumer:** keep no core queue or
  acknowledgement state and require bounded adapter retention.
- **Persistence loss is mistaken for durability:** expose failure and reserve
  durability claims, coordination, and fail-closed policy for ADR-0010/Phase 20.
- **Metrics become authority or high cardinality:** keep domain metrics adapter
  input separate from ADR-0020 output and permit only closed role/result
  dimensions.

## Review and replacement triggers

Reopen this ADR if:

- a role requires multiple independently configured consumers;
- measured bounded direct attempts cannot meet the writer budget;
- a required sink cannot avoid reentrant reducer entry;
- Phase 19 requires transactional script event delivery with the canonical
  commit rather than post-commit bounded notification;
- Phase 20 requires a pre-install durability veto or a core-owned journal to
  satisfy its approved commit policy;
- persistence/replay needs a different immutable domain payload;
- audit requirements turn a current auxiliary role into a synchronous durable
  authority; or
- a future canonical commit can occur without an immutable publication.

## Owner approval

Approved by the project owner in the 2026-08-28 working session: Option A for
Decisions 1 through 5 and all 12 proposed acceptance-test contracts.

This approval authorizes only the server-core role-specific sink ports, shared
immutable installed-publication delivery, bounded fixed-order post-commit
fan-out, explicit per-role delivery reporting, closed observability additions,
and focused test-support/test evidence described here. It does not approve a
backend, queue, worker, database, script runtime/API, durability acknowledgement,
replay format, audit policy, target projection, authority change, canonical
state addition, or gameplay behavior.
