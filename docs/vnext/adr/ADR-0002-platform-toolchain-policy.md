# ADR-0002: Supported platforms, toolchains, CI, and dependency policy

Status: **Accepted**

Date opened: 2026-08-25

Date approved: 2026-08-25

Decision owner: project owner

Needed by: Phase 1

## Decision summary

The project owner approved Option A for Decisions 1 through 4 on 2026-08-25.
The approved policy is to:

- support Windows x86-64, Linux x86-64, macOS arm64, and macOS x86-64 desktop
  clients and servers, with macOS x86-64 tested on a scheduled and release-gate
  cadence rather than on every change;
- make Windows x86-64 the required initial PC VR platform, retain Linux x86-64
  PC VR as best-effort research until Phase 9 evidence supports promotion, and
  leave macOS PC VR outside the initial release scope because it is currently
  nonfunctional; this does not reduce macOS desktop support;
- defer Quest 3 production and Android CI to Phases 23 and 24 while requiring
  new core dependencies to document Android ARM64 feasibility;
- use GitHub Actions with versioned CMake presets and repository-owned scripts,
  explicit hosted-runner labels, C++20, Ninja, and upstream-aligned compilers;
  and
- retain OpenMW 0.51's platform-native dependency flows for the baseline, while
  pinning and recording provenance and licenses for every added dependency.

## Why this decision is needed now

Phase 1 must turn a pinned OpenMW 0.51 tree into a reproducible baseline on all
supported desktop platforms. The supported architectures, blocking CI jobs,
compiler families, and dependency source determine what “reproducible” and
“supported” mean. Leaving those implicit would allow whichever runner happens
to pass first to settle product support and dependency policy accidentally.

The player-visible consequence is whether a release is actually tested on a
player's operating system and CPU architecture. The engineering consequence is
the number of build, packaging, dependency, hardware, and regression matrices
that every later multiplayer slice must maintain.

## Evidence available at kickoff

- The pinned `openmw-0.51.0` tag resolves to
  `f4bec41444214a7903bebd178389ca22ca13f646`.
- OpenMW 0.51 declares CMake 3.16.0, C++20, and no compiler extensions.
- Its tagged CI uses Ubuntu 24.04 for GCC and Clang jobs, MSVC 2022 x86-64 for
  Windows jobs, and macOS 15/Xcode 16 for arm64 and x86-64 jobs. The arm64 macOS
  merge-request job is blocking while the x86-64 merge-request job is manual
  and non-blocking.
- The tagged OpenMW dependency scripts use an explicit vcpkg tag for macOS and
  Windows (`2026-02-24`) and Qt 6.6.3 on Windows. Linux dependencies come from
  the Ubuntu 24.04 repositories plus named OpenMW PPAs.
- As observed on 2026-08-25, GitHub's hosted-runner catalog offers explicit
  `ubuntu-24.04`, `windows-2022`, `macos-15` arm64, and `macos-15-intel` labels.
  Hosted images update regularly, so each job must archive the resolved image,
  compiler, CMake, SDK, and dependency versions rather than treating a label as
  a complete lockfile.
- The OpenMW-VR versioning document says the fork has no independent release
  schedule and currently identifies its OpenMW base as 0.49 RC7. This is
  evidence for keeping the fork behind the separate Phase 9 maintenance gate,
  not for weakening the requirement to ship PC VR.

Primary evidence:

- [OpenMW 0.51 CMake configuration](https://gitlab.com/OpenMW/openmw/-/blob/openmw-0.51.0/CMakeLists.txt)
- [OpenMW 0.51 CI configuration](https://gitlab.com/OpenMW/openmw/-/blob/openmw-0.51.0/.gitlab-ci.yml)
- [OpenMW 0.51 Windows dependency versions](https://gitlab.com/OpenMW/openmw/-/blob/openmw-0.51.0/CI/deps_versions.msvc.sh)
- [OpenMW 0.51 macOS dependency versions](https://gitlab.com/OpenMW/openmw/-/blob/openmw-0.51.0/CI/macos/deps_versions.sh)
- [OpenMW-VR versioning policy](https://gitlab.com/madsbuvi/openmw/-/blob/openmw-vr/docs/source/manuals/openmw-vr/versioning.rst)
- [GitHub-hosted runner image catalog](https://github.com/actions/runner-images#available-images)

## Scenarios the policy must cover

1. A desktop change passes locally but breaks C++20 compilation or linkage on
   one supported operating system or CPU architecture. Required CI must block
   the change or the release according to the declared cadence.
2. A hosted runner updates its compiler or SDK without changing its broad image
   label. The job records the resolved environment, a failure is reproducible,
   and the project can pin or deliberately migrate the affected tool.
3. A new protocol or transport dependency works on desktop but has no Android
   ARM64 path. The dependency review reports that limitation before adoption;
   Quest work is not silently made impossible even though Android is not yet a
   release gate.
4. An Intel macOS-only defect is missed by per-change arm64 CI. The scheduled
   job detects it, and a release cannot proceed until the x86-64 gate passes.
5. A PC VR patch works on Windows but not Linux. The initial Windows PC VR
   release gate remains actionable; Linux support is not advertised until the
   Phase 9 evidence and owner review promote it.
6. An upstream OpenMW dependency pin changes. Baseline provenance distinguishes
   the intentional update from a floating package-manager or runner-image
   change and retains the relevant license record.

## Decision 1: desktop operating systems and architectures

### Option A — balanced supported matrix (recommended)

- Windows x86-64 and Linux x86-64 are required on every change.
- macOS arm64 is required on every change.
- macOS x86-64 remains a supported release architecture, with scheduled and
  release-gate CI rather than a required job on every change.
- Windows ARM64 and Linux ARM64 are not initially supported release targets.

This follows OpenMW 0.51's strongest tested paths, covers modern Apple hardware,
retains Intel Mac users without imposing its higher-cost runner on every change,
and bounds the first release matrix.

### Option B — add Windows and Linux ARM64 now

This expands accessibility and gives stronger early portability evidence. It
also adds dependency, packaging, installer, runtime, and performance gates before
the multiplayer core exists; Windows ARM64 also lacks the same OpenMW 0.51
upstream proof as the recommended matrix.

### Option C — x86-64 desktop only

This is the smallest initial matrix but excludes current Apple Silicon as a
native supported target and diverges from the pinned upstream tag's primary
macOS CI path.

## Decision 2: PC VR platform scope

### Option A — Windows required, Linux evidence-driven (recommended)

Windows x86-64 is the required PC VR release platform. Linux x86-64 remains a
best-effort build/research target until Phase 9 produces fork, OpenXR runtime,
hardware, and interoperability evidence sufficient for explicit owner
promotion. macOS PC VR is outside the initial release scope.

This satisfies the required PC VR product target with one concrete hardware and
runtime matrix while respecting the separate fork's maintenance risk.

### Option B — Windows and Linux required

This provides a broader open-platform release but makes two VR runtime,
packaging, hardware, and support matrices block Phase 9 and every release.

### Option C — Windows, Linux, and macOS required

This offers the widest nominal coverage but has the weakest current fork and
runtime evidence and the highest continuing hardware-test cost.

## Decision 3: Quest and Android work before Phase 23

### Option A — portability constraint only (recommended)

Do not add Android or Quest production jobs before Phases 23 and 24. Every new
engine-independent dependency ADR must still record Android ARM64 build support,
license implications, and platform leakage risk. Core interfaces remain free of
Android, OpenXR, and headset types.

### Option B — early non-blocking Android compile probe

Add a compile-only job for engine-independent targets after Phase 3. This gives
earlier signal but creates an additional toolchain and dependency job long before
the on-device feasibility phase can validate its usefulness.

### Option C — make Quest a release target now

This contradicts the accepted program order and would let an unresolved stretch
target block the desktop and PC VR release.

## Decision 4: CI, compiler, build, and dependency policy

### Option A — upstream-aligned, repository-owned automation (recommended)

- Use GitHub Actions because the shared repository and review workflow are on
  GitHub. Use explicit runner labels, never `*-latest` aliases:
  `ubuntu-24.04`, `windows-2022`, `macos-15`, and scheduled/release-only
  `macos-15-intel`.
- Keep configure/build/test behavior in versioned CMake presets and
  repository-owned scripts that run outside CI. Workflow YAML composes those
  commands but is not their only definition.
- Retain OpenMW 0.51's CMake 3.16 source minimum and C++20 language level during
  baseline cutover. Use Ninja for the documented local and CI path.
- Use GCC 13 as the primary Linux compiler and Clang 18 as a second compiler
  gate; MSVC 2022 with the v143 toolset on Windows; and AppleClang from Xcode 16
  on macOS. Record exact patch versions emitted by each job.
- Begin with OpenMW 0.51's platform-native dependency acquisition and pins to
  minimize the baseline diff. A Phase 1 provenance manifest records exact
  packages, repositories, vcpkg baseline/tag, integrity data where available,
  and licenses.
- Every new multiplayer dependency must use an exact released version or commit,
  include integrity verification when artifacts are fetched, record license and
  supported platforms, name an update owner/review cadence, and avoid floating
  branches or unbounded package-manager resolution.

### Option B — one new vcpkg manifest for every platform

This gives a more uniform dependency declaration and can improve binary-cache
reuse. Converting Linux and the whole OpenMW baseline at cutover creates a large
intentional difference, may mask upstream packaging assumptions, and makes
vcpkg overlay maintenance part of Phase 1.

### Option C — vendor dependencies or use submodules broadly

This gives strong source pinning and offline control. It also transfers security
updates, patch carrying, license inventory, source storage, and cross-platform
build ownership to this project.

## Proposed acceptance tests and evidence

The selected policy will be considered implemented only when later slices
produce all evidence applicable to their phase:

- a clean checkout configures, builds, installs, and runs upstream tests on each
  required desktop runner;
- CI archives the runner image/version, OS, CPU architecture, compiler, CMake,
  SDK, generator, package/dependency versions, and relevant lock or manifest
  hashes;
- the same named CMake preset and repository script used in CI runs locally;
- macOS x86-64 scheduled and release-gate jobs pass before a supported release;
- unsupported architectures and missing tools fail with bounded, actionable
  configure errors rather than partially building an unsupported artifact;
- a machine check rejects floating refs or unrecorded new dependencies;
- each selected multiplayer library's ADR includes Linux, Windows, macOS,
  license, maintenance, security, and Android ARM64 evidence; and
- PC VR or Quest support is promoted only through the Phase 9 or Phase 23 owner
  gate, respectively.

## Consequences of the approved decision

- Desktop release support is concrete without multiplying every CPU/OS
  combination at project start.
- Intel macOS regressions may be detected later than arm64 regressions, but they
  remain release blockers and have a scheduled detection path.
- Initial PC VR support is narrower than desktop support and must be described
  accurately in release documentation.
- Runner labels do not fully pin hosted images. Environment manifests and
  repository-owned commands are therefore required evidence, and tool updates
  remain explicit maintenance events.
- Dependency management remains somewhat platform-specific, matching upstream
  rather than creating a second cross-platform packaging project during the
  baseline cutover.

## Failure modes and mitigations

- **Hosted image drift:** archive resolved tool versions; pin explicit OS labels
  and toolchain majors; move images only in reviewed changes.
- **Dependency repository drift or disappearance:** record source URLs, versions,
  hashes, licenses, and cache provenance; fail closed when integrity checks fail.
- **Non-reproducible local/CI behavior:** keep commands in presets/scripts and
  exercise those exact entry points in CI.
- **Secondary platform bit rot:** make scheduled jobs visible and release
  blocking; promote them to per-change if failures become frequent or costly.
- **VR fork lag:** isolate PC VR in the Phase 9 worktree/patch policy and require
  desktop regressions plus interoperability evidence.
- **Quest accidentally foreclosed:** require Android ARM64 evidence for core
  dependency choices even while on-device and production work remains deferred.

## Review and replacement triggers

Review this ADR when any of the following occurs:

- an upstream OpenMW baseline update changes its C++ standard, CMake floor,
  primary compilers, supported architectures, Qt major, or dependency flow;
- GitHub deprecates a selected runner label or the chosen runner cannot meet CI
  time/resource needs;
- macOS x86-64 usage or runner availability no longer justifies supported status;
- Phase 9 provides enough Linux PC VR evidence to consider promotion;
- Phase 23 begins, or an earlier dependency choice materially threatens Android
  ARM64 feasibility; or
- a security, license, availability, or reproducibility issue requires replacing
  a dependency source or package manager.

Any support expansion or removal is a project-owner decision. A change that
affects shipped compatibility or release support reopens this ADR and the
affected phase.

## Owner approval

Approved by the project owner in the 2026-08-25 working session: Option A for
Decisions 1, 2, 3, and 4.

The owner clarified that VR is expected to be nonfunctional on macOS while
macOS itself must remain a supported desktop platform. Accordingly, this
approval does not advertise or gate the initial release on macOS PC VR. It does
require the approved macOS arm64 and x86-64 desktop coverage. Any future macOS
PC VR support expansion must reopen this ADR with implementation and hardware
evidence.
