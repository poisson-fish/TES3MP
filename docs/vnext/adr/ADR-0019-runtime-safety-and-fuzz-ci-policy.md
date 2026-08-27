# ADR-0019: Runtime-safety and fuzz CI policy

Status: **Accepted**

Date opened: 2026-08-26

Date approved: 2026-08-26

Date amended: 2026-08-27

Decision owner: project owner

Needed by: Phase 3 Slice 3.6

## Decision questions

Which owned targets should the first runtime-safety jobs instrument; which
sanitizer profiles and supported host/compiler combinations should block a
change; how should fuzz targets compose with the independent target graph
before production protocol decoders exist; and which local/CI entry points and
evidence should be durable project interfaces?

This decision is required before Slice 3.6 adds CMake options, sanitizer flags,
fuzz entry points, runners, or workflow jobs. It affects build architecture,
toolchain support, CI cost, and the test surface, but not authority, canonical
state, state scope, protocol semantics, or gameplay behavior.

ADR-0002 already fixes the supported desktop/compiler matrix and requires
repository-owned automation. ADR-0004 already accepts Clang ASan/UBSan/
libFuzzer for the disposable FlatBuffers selection proof. ADR-0005 accepts a
narrow sanitizer profile for the pinned GameNetworkingSockets proof. Neither
decision defines the reusable safety plumbing for project-owned TES3MP targets.

## Decision summary

The project owner approved Option A for Decisions 1 through 5 on 2026-08-26:

1. instrument every project-owned TES3MP target selected by a build, while the
   first fast CI graph builds all five engine-independent libraries and their
   contract/fuzz executables without OpenMW;
2. use separate Linux Clang 18 ASan+UBSan and ThreadSanitizer profiles with
   fail-fast configuration, halt-on-error runtimes, and no suppressions;
3. use one Clang libFuzzer executable per bounded parser, beginning with a
   smoke harness around the existing test-support-only spatial decoder and a
   small checked-in seed corpus;
4. run the dedicated ASan+UBSan/fuzz and ThreadSanitizer jobs with short bounded
   smoke durations and retained evidence—per change through Phase 3, then once
   by manual dispatch for each Phase 4+ phase-completion candidate; and
5. expose the behavior through repository-owned CMake options/presets and a
   runner that writes a versioned JSON evidence record, while leaving normal
   developer presets uninstrumented.

## Required boundaries

1. Sanitizer and fuzz support is opt-in. Existing local and supported baseline
   presets retain their current predictable flags and build behavior.
2. Project-owned functions apply instrumentation only to explicitly registered
   TES3MP targets. They do not silently change OpenMW or third-party dependency
   flags.
3. ASan+UBSan and ThreadSanitizer are separate build directories and profiles;
   they are never combined.
4. Unsupported compiler, platform, or incompatible-option combinations fail at
   configure time with actionable messages. They never degrade to an
   uninstrumented green job.
5. No runtime suppression or source exclusion is accepted for project-owned
   code in this slice. A real finding blocks the job until fixed or returned to
   the owner as an explicit amendment.
6. Fuzz targets live in test plumbing, link only through approved target
   boundaries, accept an arbitrary byte span, and exercise bounded parsers
   without mutating production state.
7. A fuzz crash, timeout, sanitizer report, or ThreadSanitizer report fails the
   job. Reproducer inputs are retained as artifacts and promoted to the seed or
   regression corpus when they expose a defect.
8. This slice does not define production protocol bytes, decoder allocation
   budgets, threading, transport callbacks, canonical mutation, or gameplay.

## Scenarios evaluated

1. A developer uses the ordinary platform preset; no sanitizer or fuzz flags
   appear and existing TES3MP contracts run unchanged.
2. Linux Clang 18 builds all selected engine-independent libraries and contract
   executables with ASan+UBSan, then runs every contract without a report.
3. A separate Linux Clang 18 build runs the same contracts under
   ThreadSanitizer, even though the current facilities are deliberately
   single-threaded, so later concurrency cannot bypass the gate accidentally.
4. The fuzzer receives empty, truncated, exact-size, and arbitrary oversized
   input. The bounded test decoder accepts or rejects without an out-of-bounds
   read, unbounded allocation, state mutation, or crash.
5. A contributor requests fuzzing under MSVC, combines ThreadSanitizer with
   ASan/UBSan, or names an unsupported compiler. Configuration fails clearly.
6. A future production decoder lands. Its owning slice adds a separate fuzz
   executable and seeds through the same helper rather than expanding one
   ambiguous mega-harness.
7. A hosted sanitizer job finds a crash or race. CI retains the toolchain,
   selected profile, commands, test list, logs, and reproducer rather than
   allowing an uninstrumented retry to satisfy the gate.
8. An OpenMW or selected-transport header is introduced into the independent
   graph. Existing fail-closed boundary tests still reject it in sanitizer and
   ordinary builds.

## Decision 1: instrumentation scope and target ownership

### Option A: owned-target registration with a fast independent CI graph (approved)

Add project-owned CMake helpers that apply the selected runtime-safety profile
to every registered TES3MP library or executable in the configured graph. The
first dedicated CI build configures the five engine-independent libraries,
their contract executables, and fuzz targets through a small standalone
composition project. It does not configure or link OpenMW.

When the app-local adapter or later composition targets are selected by a
normal build with a safety profile, they use the same registration helper.
Dedicated full-product sanitizer coverage remains an incremental requirement
of the slices that introduce those composition roots.

This catches defects in current owned code quickly, preserves the accepted
dependency boundary, and avoids making a complete OpenMW dependency build part
of every short sanitizer smoke.

### Option B: instrument the complete OpenMW baseline immediately

Apply sanitizer flags process-wide and make a full OpenMW build/test job the
Slice 3.6 gate. This gives broader inherited-engine coverage, but greatly
increases CI time and makes third-party/upstream reports and instrumentation
compatibility part of a slice intended to establish the multiplayer scaffold.

### Option C: instrument only test executables

Compile contract/fuzz executables with sanitizers but leave the TES3MP static
libraries uninstrumented. This is cheap, but most implementation code would
not contain sanitizer checks and a green result would overstate coverage.

## Decision 2: sanitizer profiles and support boundary

### Option A: separate Clang 18 ASan+UBSan and ThreadSanitizer profiles (approved)

Use Linux `ubuntu-24.04` with Clang 18 for two strict profiles:

- ASan+UBSan: `address,undefined`, frame pointers retained, recovery disabled,
  leak detection enabled, and runtimes configured to halt on the first report;
- race checking: `thread`, frame pointers retained, and the runtime configured
  to halt on the first report.

The profiles use separate build directories because ThreadSanitizer is not
compatible with AddressSanitizer. No suppressions or project-source exclusions
are permitted. Linux Clang 18 is the initial blocking sanitizer platform;
Windows/MSVC and macOS/AppleClang continue ordinary compile/contract coverage
under ADR-0002 and may gain additional sanitizer profiles only with evidence.

### Option B: ASan+UBSan only

This reuses the already proven compiler family and has lower CI cost, but does
not satisfy Slice 3.6's explicit race-checking requirement or establish a gate
before threads arrive.

### Option C: sanitizer jobs on every supported compiler and operating system

This maximizes tool/runtime diversity, but support differs across MSVC,
AppleClang, and hosted environments and would multiply CI cost before any
multiplayer composition process exists.

## Decision 3: fuzz engine and initial harness

### Option A: one Clang libFuzzer target per bounded parser (approved)

Use compiler-provided libFuzzer with ASan+UBSan. Each target has one semantic
entry point, a bounded checked-in seed corpus, and explicit maximum input/time
arguments. Slice 3.6 starts with one smoke harness around the existing
test-support-only spatial snapshot decoder. That decoder is already a bounded
byte consumer and is explicitly not a production wire format.

Production decoder fuzzers arrive in the slices that add those decoders. A
finding's minimized reproducer becomes a stable regression input where useful.

### Option B: a no-op fuzz executable until Phase 4

This proves only that libFuzzer links and starts. It meets the narrowest reading
of CI plumbing but provides no memory-safety exercise of current code.

### Option C: a custom portable mutation loop

This could run under MSVC and AppleClang without libFuzzer, but would require
the project to own corpus mutation, coverage feedback, minimization, timeout,
and reproducer behavior already provided by the selected Clang toolchain.

## Decision 4: CI cadence and bounded workload

### Option A: dedicated per-change safety workflow (approved through Phase 3)

Add one dedicated vNext workflow with independent Linux Clang 18
ASan+UBSan/fuzz and ThreadSanitizer jobs on push, pull request, and manual
dispatch. Run all current C++ contracts in each sanitizer job; run the initial
fuzzer for 30 seconds in the ASan+UBSan job. Cap each job at 20 minutes and
retain the versioned evidence, logs, and any generated reproducer for 30 days.

Longer scheduled fuzzing and a corpus merge/minimization service remain Phase
22 hardening work unless measured startup/build cost justifies adding a
scheduled extension earlier.

### Option B: fold safety into the Linux baseline matrix

This creates fewer workflow files, but couples a fast independent target gate
to the full OpenMW dependency build and obscures whether failures came from the
baseline or the multiplayer safety profile.

### Option C: manual phase-exit jobs (approved beginning with Phase 4)

Keep the same two bounded jobs and retained evidence, but expose manual dispatch
only and run them once against each phase-completion candidate. Applicable local
contracts and fuzz plumbing still run during slice work. This reduces hosted-CI
burn while preserving a blocking sanitizer/race/fuzz gate before a phase closes.

## Decision 5: local entry points and evidence

### Option A: CMake presets plus a repository-owned runner and JSON evidence (approved)

Add hidden/shared presets and named configure/build presets for the two safety
profiles. A cross-platform Python runner validates the Linux/Clang support
boundary, uses separate build directories, invokes the named presets/targets,
runs bounded fuzzing, and writes a versioned JSON record containing source
commit, platform, compiler/CMake versions, exact profile, executed contracts,
fuzz duration/corpus, and result artifact paths.

Workflow YAML only provisions Clang 18 and invokes that runner. Unit tests
assert command construction, invalid-option rejection, workflow pins, evidence
shape, and fail-closed behavior without needing sanitizers on the local host.

### Option B: workflow-only commands

This is smaller, but developers cannot reproduce the exact gate through one
owned entry point and command/evidence drift is harder to test.

### Option C: CMake targets without retained evidence

This is locally convenient but loses the exact profile/toolchain record needed
to compare cross-run findings and prove the completion gate.

## Proposed acceptance tests and demo

The approved implementation must demonstrate:

1. ordinary presets contain no TES3MP sanitizer/fuzzer enablement;
2. ASan+UBSan and ThreadSanitizer profiles are mutually exclusive and reject
   unsupported hosts/compilers at configure or runner validation;
3. all selected TES3MP libraries, not only the test executables, compile with
   the requested profile and every required executable links its runtime;
4. the independent graph and all existing C++ contracts pass under Linux Clang
   18 ASan+UBSan;
5. the same contracts pass in a separate Linux Clang 18 ThreadSanitizer build;
6. the libFuzzer target builds only when requested and rejects empty, malformed,
   exact, and oversized spatial test encodings during a bounded 30-second run
   without a sanitizer finding;
7. checked-in seeds and future retained reproducers have explicit size/count
   bounds and deterministic repository paths;
8. existing dependency/include boundary failures remain fail closed;
9. repository unit tests verify runner commands, profile exclusivity, workflow
   pins/cadence/timeouts, no suppressions, and evidence schema; and
10. CI retains separate ASan+UBSan/fuzz and ThreadSanitizer evidence artifacts.

The implementation demo must show a normal uninstrumented configure, both
instrumented configurations, the full current contract list, the bounded
fuzzer smoke, one intentional unsupported/incompatible configuration failure,
and the retained evidence fields. Slice 3.6 remains **In Progress** until the
hosted jobs pass and the owner accepts that demo.

## Consequences

- Current project-owned code receives memory, undefined-behavior, and race
  checks without coupling the fast gate to the complete OpenMW build.
- Linux Clang 18 becomes the initial sanitizer reference environment while the
  supported release compiler matrix remains unchanged.
- Per-parser fuzz targets make ownership and corpus budgets explicit, but each
  later production decoder slice must register its own harness.
- Phase 4+ safety runs add two short Linux jobs once per phase-completion
  candidate. Longer fuzzing remains a later measurable hardening decision.
- ThreadSanitizer is initially a plumbing smoke because current deterministic
  facilities intentionally own no threads; keeping it active prevents later
  concurrency from arriving without a gate.

## Failure modes and mitigations

- **Only executables are instrumented:** inspect generated compile commands and
  assert flags on every selected owned target.
- **Sanitizer profiles are accidentally combined:** fail configure and runner
  validation before generation.
- **Unsupported toolchain silently runs clean:** require exact Linux/Clang
  profile validation and retain the resolved compiler identity.
- **Suppressions hide a project defect:** reject suppression options/files in
  the Slice 3.6 policy tests and require an owner-approved ADR amendment.
- **Fuzzer allocates from arbitrary length:** cap CI artifact inputs and have the
  target reject over-limit data before parser work.
- **One mega-harness obscures ownership:** use one executable/corpus per bounded
  parser and name it in its owning slice.
- **CI-only behavior drifts:** keep profile construction and commands in CMake
  presets and the repository runner, with workflow-policy tests.
- **Short fuzzing is mistaken for release assurance:** call it a smoke gate;
  Phase 22 owns sustained fuzz budgets and corpus health.

## Review and replacement triggers

Reopen this ADR if:

- project-owned targets need a sanitizer exclusion or runtime suppression;
- Clang 18 or `ubuntu-24.04` is retired or cannot run one of the profiles;
- a supported compiler exposes a materially valuable sanitizer configuration
  that should become blocking;
- the standalone graph cannot represent an approved owned-target boundary;
- phase-exit cadence allows safety regressions to accumulate or makes failures
  too costly to localize;
- a production decoder cannot use one bounded byte-span harness;
- ThreadSanitizer cannot execute once real concurrency lands; or
- Phase 22 evidence requires continuous/coverage-guided fuzz infrastructure.

## Owner approval

Approved by the project owner in the 2026-08-26 working session: Option A for
Decisions 1 through 5.

Approval fixes owned-target registration with a fast independent safety graph,
separate Linux Clang 18 ASan+UBSan and ThreadSanitizer profiles, per-parser
libFuzzer targets beginning at the test-only spatial decoder, two bounded
per-change jobs, and repository-owned presets/runner/JSON evidence. It does not
approve a production wire decoder, full-OpenMW sanitizer gate, runtime
suppression, threading model, protocol behavior, authority, state scope, or
gameplay behavior.

The project owner amended Decision 4 in the 2026-08-27 working session. Option
A remains the historical Phase 3 implementation evidence; Option C is approved
for Phase 4 and later. The same profiles, bounds, failure policy, and retained
evidence remain mandatory, but the workflow is manually dispatched once at
phase exit instead of running on every push or pull request.
