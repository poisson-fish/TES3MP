# TES3MP vNext implementation notes

Document type: chronological implementation history and working notes

Updated: 2026-09-02

Status source: [`IMPLEMENTATION_PLAN.md`](IMPLEMENTATION_PLAN.md)

## Purpose

This companion file retains phase-specific implementation notes, slice logs,
verification details, owner-review history, and follow-ups. The implementation
plan remains the concise source of truth for current phase and slice status,
ordered deliverables, decision gates, and exit criteria.

Read this file when investigating why a decision or status changed. Routine
planning and continuation should begin with the implementation plan and open
only the relevant phase section here.

## Phase 0 — Archive and cutover preparation

[Back to the phase tracker](IMPLEMENTATION_PLAN.md#phase-0--archive-and-cutover-preparation)

- The direction document, permanent legacy archive tag, and reference-only
  legacy gameplay inventory are the completed slices.
- The existing `tes3mp-0.8.1` tag is a lightweight tag at `68954091c` and must
  not be moved. ADR-0001 must choose a new unambiguous archive-tag name.
- Branch `0.8.1` currently provides the intended `49be5b640` source reference,
  but a branch alone is not the permanent archive artifact required by the plan.
- No cutover command should be run until the dry-run result preserves the vNext
  documentation and produces an OpenMW 0.51 tree without a textual merge.
- 2026-08-25 — Slice 0.2 — Implemented
  - Change: created and pushed the permanent annotated tag
    `tes3mp-0.8.1-archive`; tag object
    `1f3bc4c651573a60b4326b5d4703b6fad4b7fccf` archives commit
    `49be5b6405d6ab427e06ed350cf76c715a1f3bdd`.
  - Decisions: project owner approved the recommended
    `tes3mp-0.8.1-archive` name on 2026-08-25. This approval does not select or
    authorize the baseline-cutover mechanics governed by ADR-0001.
  - Verification: `git cat-file -t tes3mp-0.8.1-archive` returned `tag`;
    `git rev-parse tes3mp-0.8.1-archive^{}` returned `49be5b6405d6ab427e06ed350cf76c715a1f3bdd`;
    and `git ls-remote --tags origin 'refs/tags/tes3mp-0.8.1-archive' 'refs/tags/tes3mp-0.8.1-archive^{}'`
    returned the tag object and the same peeled commit. `tes3mp-0.8.1` remains a
    lightweight tag at `68954091c54d0596037c4fb54d2812313b7582a1`.
  - Owner review: explicit tag-name approval received in the 2026-08-25 working
    session; published name and target match the approved option.
  - Follow-ups: Slice 0.5 still requires the complete ADR-0001 decision packet,
    owner approval, and disposable cutover rehearsal before any cutover command.
- 2026-08-25 — Slice 0.3 — Implemented
  - Change: this commit adds
    `docs/vnext/LEGACY_GAMEPLAY_FEATURE_INVENTORY.md`, a reference-only inventory
    grouped by user-visible gameplay and operational domains.
  - Decisions: none; the inventory explicitly rejects compatibility, authority,
    state-scope, architecture, and gameplay-semantics implications.
  - Verification: `git diff --check`; `test -e` over every source named in the
    inventory's repository-evidence section; and `rg -q` checks for all required
    inventory sections. No production, build, or test target changed.
  - Owner review: not required for completion because this historical
    documentation slice approves no architecture or behavior; included in the
    current working-tree handoff for review.
  - Follow-ups: missing behavior defined only by external CoreScripts, wiki, or
    OpenMW-VR sources must be researched in the relevant future ADR/GDR slice.
- 2026-08-25 — Slice 0.4 — In Progress
  - Change: added proposed
    [`ADR-0002`](adr/ADR-0002-platform-toolchain-policy.md) with desktop, PC VR,
    Quest, CI/toolchain, and dependency-policy options, scenarios, tradeoffs,
    recommendations, failure modes, review triggers, and acceptance evidence.
  - Decisions: none accepted. The decision packet recommends a bounded desktop
    matrix, Windows-first PC VR, deferred Quest production, and upstream-aligned
    GitHub Actions/toolchain/dependency policy; every recommendation remains
    subject to explicit project-owner approval.
  - Verification: compared the repository baseline with the pinned OpenMW 0.51
    CMake, CI, and dependency-version files; verified the official tag with
    `git ls-remote`; reviewed the OpenMW-VR versioning policy and the current
    GitHub-hosted runner catalog; then ran `git diff --check` and document-link
    checks recorded in the working-session handoff.
  - Owner review: decision packet presented on 2026-08-25; approval pending.
    Slice 0.4 remains **In Progress** and no dependent production work is
    authorized.
  - Follow-ups: record the owner's selected options and conditions in ADR-0002,
    then mark the ADR and slice **Implemented**. Phase 1 owns the desktop proof
    matrix, Phase 9 owns PC VR proof, and Phase 23 owns Quest feasibility
    evidence under the approved policy.
- 2026-08-25 — Slice 0.4 — Implemented
  - Change: accepted
    [`ADR-0002`](adr/ADR-0002-platform-toolchain-policy.md) after owner review
    and recorded the selected desktop, PC VR, Quest, CI/toolchain, and dependency
    policies.
  - Decisions: owner approved Option A for Decisions 1 through 4. macOS arm64
    and x86-64 desktop remain supported; Windows x86-64 is the required initial
    PC VR platform; Linux PC VR remains evidence-driven best effort; macOS PC VR
    is outside the initial release scope because it is currently nonfunctional;
    Quest production remains deferred; and the upstream-aligned GitHub Actions
    policy is accepted.
  - Verification: `git diff --check`; required ADR section/status checks; local
    link checks; and comparison of the accepted text with the four presented
    options and the owner's clarification.
  - Owner review: explicit approval received in the 2026-08-25 working session:
    “all A for all four,” with the macOS VR clarification recorded above.
  - Follow-ups: Phase 1 owns desktop CI/build proof, Phase 9 owns PC VR support
    evidence and any Linux promotion, and a future macOS PC VR expansion must
    reopen ADR-0002. Phase 23 owns Quest feasibility.
- 2026-08-25 — Slice 0.5 — In Progress
  - Change: added proposed
    [`ADR-0001`](adr/ADR-0001-baseline-cutover-git-mechanics.md) with cutover
    graph/tree options, recommended exact-tree mechanics, preserved-path scope,
    failure handling, verification, publication, and rollback policy.
  - Decisions: none accepted. The packet recommends a two-parent cutover whose
    tree is exact OpenMW 0.51 plus `docs/vnext/**`. ADR approval would authorize
    only the disposable production-form rehearsal, not the real cutover.
  - Verification: fetched the official tag into a disposable local clone and
    verified `f4bec41444214a7903bebd178389ca22ca13f646`; constructed synthetic
    one- and two-parent candidates without updating a shared ref; verified the
    recommended candidate's parent order, both-parent ancestry, OpenMW-only tree
    outside the three then-committed `docs/vnext` files, byte-identical preserved
    docs, absence of checked legacy paths, and exact-tree new-commit rollback.
  - Owner review: decision packet presented on 2026-08-25; approval pending.
    No production-form rehearsal or active-branch cutover is authorized.
  - Follow-ups: after approval, run and record the production-form
    merge/read-tree/restore rehearsal from the final committed inputs. Slice 0.6
    and Phase 0 exit-gate approval remain required before the real cutover.
- 2026-08-25 — Slice 0.5 — Implemented
  - Change: accepted
    [`ADR-0001`](adr/ADR-0001-baseline-cutover-git-mechanics.md) after owner
    approval and completed the approved merge/read-tree/restore rehearsal in an
    isolated clone and worktrees without changing the active repository.
  - Decisions: owner approved Option 1: a two-parent cutover with the final
    pre-cutover `vnext` commit first, OpenMW
    `f4bec41444214a7903bebd178389ca22ca13f646` second, and a tree equal to OpenMW
    plus only `docs/vnext/**`. Publication is fast-forward-only; published
    rollback is a new commit, never rewritten history.
  - Verification: disposable input `45fa537004aa19bef4b35b9c556351f8327bf75a`;
    cutover `e042db240fe2f109c47bedf0640e4724b7f6ea63`; tree
    `85d3390ca15a169e04e88d746b1a1d67de4c7a1b`; rollback
    `addf2c63ff64af3a12f09f71863ff1973f8601ac`. Exact parent order and ancestry,
    upstream identity outside five `docs/vnext` files, preserved-doc identity,
    legacy-path absence, clean worktrees, `git diff --check`, `git fsck
    --no-dangling`, merge-abort restoration, and exact-tree new-commit rollback
    all passed.
  - Owner review: Option 1 and its mechanics explicitly approved in the
    2026-08-25 working session. This approval covered the rehearsal only; it did
    not authorize the real cutover.
  - Follow-ups: Slice 0.6 must capture final pre-cutover provenance. Then repeat
    the approved input/tree checks against the clean published pre-cutover commit
    and hold the Phase 0 exit-gate review before any real cutover.
- 2026-08-25 — Slice 0.6 — Implemented
  - Change: commit `1dc1d5bd00519efa5b92a917c46f9407e9e28257`
    added and published
    [`PRE_CUTOVER_PROVENANCE.md`](PRE_CUTOVER_PROVENANCE.md), recording the clean
    published capture base, local and shared branch/tag refs, remote
    configuration, submodule declaration and initialization state, commit/tree
    IDs, vNext first-parent lineage, legacy-tree classification, and the final
    pre-cutover preflight.
  - Decisions: none. This is a read-only capture of existing repository state and
    does not authorize the real cutover, configure `openmw-upstream`, or mutate
    any branch, tag, submodule, or shared ref. The capture also records that the
    earlier lightweight `tes3mp-0.8.1` tag was no longer advertised locally or
    by `origin`; the required annotated `tes3mp-0.8.1-archive` tag remains intact.
  - Verification: before editing, `git status --porcelain=v1
    --untracked-files=all` and `git diff --check` produced no output;
    `vnext` and `origin/vnext` both resolved to
    `beb2f86cdf7d810f8eebf0207e266fb62c6e8fda`; `git fsck --no-dangling`
    passed; `git cat-file -t tes3mp-0.8.1-archive` returned `tag`; local and
    remote peel checks returned
    `49be5b6405d6ab427e06ed350cf76c715a1f3bdd`; `git submodule status` and
    `git ls-tree HEAD extern/breakpad` agreed on
    `e6d1c032baa222d8a8dc87813e9067199ec0266d`; and a read-only
    `git ls-remote https://gitlab.com/OpenMW/openmw.git` query resolved
    `openmw-0.51.0` to `f4bec41444214a7903bebd178389ca22ca13f646`.
    After editing, `git diff --check` passed; all local Markdown links under
    `docs/vnext` resolved; required provenance headings and every recorded local
    commit/tree/tag object ID were checked; and the changed-path check found only
    `docs/vnext/IMPLEMENTATION_PLAN.md` and
    `docs/vnext/PRE_CUTOVER_PROVENANCE.md`.
    The post-push preflight found a clean worktree; `HEAD`, local `vnext`,
    `origin/vnext`, and the direct remote query all resolved to the published
    commit; its tree was `994268dd4e8917a8337d1045aea57efa6728c0ef`;
    archive-tag and submodule checks remained unchanged; and `git diff --check`
    plus `git fsck --no-dangling` passed.
  - Owner review: no architecture or behavior decision is introduced, so no
    decision approval was required for the capture. The separate Phase 0
    exit-gate review remains pending.
  - Follow-ups: after this status-only documentation update is published, repeat
    the final clean/local/tracking/remote identity check, acknowledge the observed
    absence of the old lightweight tag during the exit-gate review, and obtain
    explicit project-owner approval before Phase 1 or any real cutover action.
- 2026-08-25 — Phase 0 exit gate — Implemented
  - Change: the project owner reviewed the completed Slice 0.1–0.6 evidence and
    approved advancing to Phase 1, including authorization to execute and
    publish the real ADR-0001 cutover.
  - Decisions: no ADR was changed. The review accepted the documented absence
    of the old lightweight `tes3mp-0.8.1` tag because the required permanent
    annotated `tes3mp-0.8.1-archive` tag remains published and correct.
  - Verification: before the review, `HEAD`, local `vnext`, `origin/vnext`, and
    a direct `git ls-remote --heads origin refs/heads/vnext` query all resolved
    to `e68728ab6a03559e26564f683fbb4007c0e9c727`; the worktree was clean;
    `git diff --check` and `git fsck --no-dangling` produced no output; the
    archive tag was an annotated tag locally and remotely and peeled to
    `49be5b6405d6ab427e06ed350cf76c715a1f3bdd`; and the official OpenMW tag
    query resolved to `f4bec41444214a7903bebd178389ca22ca13f646`.
  - Owner review: explicit Phase 0 exit-gate approval and real-cutover
    authorization received in the 2026-08-25 working session.
  - Follow-ups: execute Phase 1 Slices 1.1 and 1.2 exactly as ADR-0001 specifies.

## Phase 1 — Clean OpenMW 0.51 baseline

[Back to the phase tracker](IMPLEMENTATION_PLAN.md#phase-1--clean-openmw-051-baseline)

- Preserve `docs/vnext/` and the minimum required repository metadata during the
  cutover as explicit vNext-owned differences.
- Baseline fixes required merely to build on supported runners should be isolated
  and documented; they must not become a place to reintroduce legacy multiplayer
  changes.
- Record CI images, compilers, CMake version, dependency source, and cache keys in
  ADR-0002 so a green baseline can be reproduced outside CI.
- 2026-08-25 — Slice 1.1 — Implemented
  - Change: configured `openmw-upstream` with
    `https://gitlab.com/OpenMW/openmw.git` and fetched the official
    `openmw-0.51.0` tag and its reachable history.
  - Decisions: none; the remote URL and pinned commit were already accepted by
    ADR-0001.
  - Verification: `git config --get remote.openmw-upstream.url` returned the
    official URL; `git rev-parse openmw-0.51.0` returned
    `f4bec41444214a7903bebd178389ca22ca13f646`; and `git cat-file -t
    openmw-0.51.0` returned `commit`.
  - Owner review: Phase 0 exit-gate approval authorized this Phase 1 slice in the
    2026-08-25 working session; no new architecture or behavior decision arose.
  - Follow-ups: Slice 1.2 performs the approved cutover from the final published
    pre-cutover documentation commit.
- 2026-08-25 — Slice 1.2 — In Progress
  - Change: began the production cutover using the accepted ADR-0001 exact-tree,
    two-parent procedure; no cutover ref has been created or published yet.
  - Decisions: none; the owner explicitly authorized the real cutover during the
    Phase 0 exit-gate review.
  - Verification: pending the final documentation commit, repeated preflight,
    disposable-worktree tree/ancestry checks, and publication verification.
  - Owner review: real-cutover authorization received in the 2026-08-25 working
    session.
  - Follow-ups: publish this final pre-cutover documentation state, repeat all
    ADR-0001 preconditions, construct and verify the cutover, then record its
    exact commit/tree evidence before marking Slice 1.2 **Implemented**.
- 2026-08-25 — Slice 1.2 — Implemented
  - Change: published the ADR-0001 two-parent exact-tree cutover as
    `6cdaddda60e9e1f02cc6b5029dd2b589a9f9d11b`, with tree
    `0824acb059d88ef65be3a7277f6e48a3f9eab04d`. Its first parent is the final
    published pre-cutover documentation commit
    `85d92afed0e454c2599cf9b4c0b11788af3e9007`, and its second parent is OpenMW
    `f4bec41444214a7903bebd178389ca22ca13f646`.
  - Decisions: none; the production cutover used the owner-approved ADR-0001
    parent order, exact upstream tree, `docs/vnext/**` overlay, fail-closed
    checks, fast-forward-only publication, and no-history-rewrite policy.
  - Verification: before preparation, the worktree was clean; local, tracking,
    and directly queried remote `vnext` refs matched the final pre-cutover
    commit; the upstream URL and commit matched ADR-0001; the archive tag was
    annotated locally and remotely and peeled to the approved legacy commit;
    `git diff --check` and `git fsck --no-dangling` passed; and the preparation
    branch name was unused. Before publication, the cutover had exactly the two
    approved parents in order and both were ancestors; `git diff --quiet`
    proved byte/mode/tree identity with OpenMW outside `docs/vnext/**` and with
    the pre-cutover parent inside `docs/vnext/**`; the only six upstream
    differences were the six reviewed vNext documents; all explicit legacy
    path and `RakNet|CrabNet|CoreScripts` component checks were empty; local
    Markdown links, `git diff --check`, clean-worktree, and `git fsck
    --no-dangling` checks passed; and the concurrent-advance check passed.
    After publication, `HEAD`, local `vnext`, `origin/vnext`, and a direct
    remote query all resolved to the cutover commit, with a clean worktree. The
    disposable worktree and merged preparation branch were then removed.
  - Owner review: the project owner explicitly authorized the real cutover in
    the 2026-08-25 Phase 0 exit-gate review; observed production evidence
    matched the approved ADR scenarios with no deviation.
  - Follow-ups: Slice 1.3 adds the machine-checkable baseline provenance
    manifest/check. Build and upstream-test proof remains owned by Slices
    1.4–1.7, and compiled legacy exclusion proof remains Slice 1.8.
- 2026-08-25 — Slice 1.3 — Implemented
  - Change: this commit adds the machine-readable
    [`BASELINE_PROVENANCE.json`](BASELINE_PROVENANCE.json), the cross-platform
    `scripts/verify_vnext_baseline.py` checker, and isolated Git-fixture tests.
    The manifest records the pinned OpenMW commit/tree, approved cutover graph,
    every intentional path difference, and SHA-256 identities for the baseline
    CMake and platform dependency-declaration inputs.
  - Decisions: none; this mechanical control implements ADR-0001 and ADR-0002
    without changing their approved baseline, preserved-path, platform, or
    dependency policies. Inherited runner-resolved and floating dependency
    inputs are recorded as Phase 1 follow-ups, not silently approved as
    reproducible.
  - Verification: `python -m unittest
    scripts.tests.test_verify_vnext_baseline -v` passed six success/failure
    scenarios; `python -m py_compile scripts/verify_vnext_baseline.py
    scripts/tests/test_verify_vnext_baseline.py` passed; staged-tree
    `python scripts/verify_vnext_baseline.py --index` enumerated exactly nine
    intentional additions and verified ten dependency-declaration file hashes;
    `git diff --check` and `git fsck --no-dangling` passed.
  - Owner review: no new architecture, authority, state-scope, security,
    gameplay, or user-visible behavior decision was introduced; no separate
    decision approval or implementation demo is required for this mechanical
    provenance slice.
  - Follow-ups: Slice 1.4 establishes the local preset and reproducibly resolved
    dependency evidence; Slices 1.5–1.7 integrate the checker and archive exact
    platform dependency/license manifests; Slice 1.8 proves compiled legacy
    exclusion.
- 2026-08-25 — Slice 1.4 — In Progress
  - Change: this commit adds versioned cross-platform CMake configure/build
    presets, `scripts/run_vnext_baseline.py`, a commit- and hash-pinned Windows
    dependency lock/provisioning path, runner unit tests, and
    [`LOCAL_BASELINE_BUILD.md`](LOCAL_BASELINE_BUILD.md). The runner verifies
    baseline provenance before configuration and retains JSON test and
    environment/dependency evidence under the ignored build directory.
  - Decisions: none; the implementation applies accepted ADR-0002 Option A.
    The Windows preset configures the existing `openmw-cs` target because the
    pinned upstream CMake assigns its Windows manifest whenever `WIN32` is true;
    the build preset still selects only the three upstream test targets, so no
    baseline-source patch or additional product target is introduced.
  - Verification: `python -m unittest
    scripts.tests.test_run_vnext_baseline
    scripts.tests.test_verify_vnext_baseline -v` passed 13 tests;
    `python -m py_compile scripts/run_vnext_baseline.py
    scripts/verify_vnext_baseline.py
    scripts/tests/test_run_vnext_baseline.py
    scripts/tests/test_verify_vnext_baseline.py` passed; JSON parsing of
    `CMakePresets.json`, the dependency lock, and the provenance manifest
    passed; `python scripts/verify_vnext_baseline.py --index` enumerated exactly
    14 intentional differences and verified 13 dependency-declaration inputs;
    and `git diff --cached --check` passed. From an MSVC 2022 v143 x86-64
    developer environment, `python scripts/run_vnext_baseline.py all --index`
    configured with CMake 3.31.6/Ninja 1.12.1 and compiled all 1,245 Ninja
    actions; after correcting the runner's DLL search path, `python
    scripts/run_vnext_baseline.py test` passed `components-tests` (1,395),
    `openmw-tests` (490), and `openmw-cs-tests` (154), with zero failures or
    disabled tests. A clean-checkout rerun remains pending before completion.
  - Owner review: no new architecture, authority, state-scope, security,
    gameplay, or user-visible behavior decision was introduced. This mechanical
    slice does not require a separate decision review or implementation demo.
  - Follow-ups: run the committed source through the complete command from a
    clean disposable worktree, record its exact evidence, then mark Slice 1.4
    **Implemented**. Slices 1.5–1.7 still own supported-platform CI and retained
    CI environment/license artifacts.
- 2026-08-25 — Slice 1.4 — Implemented
  - Change: implementation commit
    `67afd26db8648a14d560a55600d51b96789acac3` establishes the versioned local
    presets, fail-closed runner, pinned Windows dependency provisioning,
    machine-readable evidence, tests, and local build documentation described
    above. This status update records the completed clean-checkout gate.
  - Decisions: none; the completed implementation remains within accepted
    ADR-0002 Option A and does not change platform support, dependency policy,
    compiler policy, target ownership, state scope, or gameplay behavior.
  - Verification: from a clean detached worktree at exact implementation commit
    `67afd26db8648a14d560a55600d51b96789acac3`, `python
    scripts/run_vnext_baseline.py all` passed the provenance check for exactly
    14 intentional differences and 13 dependency-declaration inputs,
    configured with CMake 3.31.6, Ninja 1.12.1, MSVC 19.44.35228.0/v143, and Qt
    6.6.3, and completed all 1,245 Ninja actions. The generated JSON reports
    record `components-tests` (1,395), `openmw-tests` (490), and
    `openmw-cs-tests` (154) with zero failures and zero disabled tests (2,039
    total). The evidence artifact records the exact source commit, dependency
    lock SHA-256
    `f5c6e975a8c2340959f95c1f417b86581fbc5c9441c197b052fabcb5499d66ee`,
    vcpkg archive SHA-512, 120 package list files, and 119 copyright files.
    The runner/verifier unit suite also passed all 13 tests; Python compilation,
    JSON parsing, staged provenance verification, and staged diff checks passed.
  - Owner review: no new architecture, authority, state-scope, security,
    gameplay, or user-visible behavior decision was introduced. The clean run
    matched the already accepted build-policy decision, so no additional demo
    gate is required for this mechanical slice.
  - Follow-ups: Slices 1.5–1.7 add and retain evidence from supported Linux,
    Windows, and macOS CI. Slice 1.8 proves compiled legacy multiplayer
    exclusion; Phase 1 remains **In Progress** until those gates pass.
- 2026-08-25 — Slice 1.5 — In Progress
  - Change: this commit adds a separate Ubuntu 24.04 baseline workflow for the
    approved GCC 13 and Clang 18 gates, removes the inherited floating
    `ubuntu-latest` job, adds an inherited Linux CI preset that builds and
    installs the complete upstream desktop target set, and routes configure,
    build, upstream tests, install, and evidence through the repository-owned
    baseline runner. A new fail-closed capture script archives resolved dpkg
    versions, apt sources/policy, runner metadata, and dereferenced package
    copyright files alongside the installed tree and JSON test reports.
  - Decisions: none; the runner label, compiler matrix, Ninja generator,
    platform-native dependency flow, evidence requirements, and per-change
    cadence implement owner-approved ADR-0002 Option A. The workflow pins
    `actions/checkout` v6.0.2 and `actions/upload-artifact` v4.6.2 by exact
    commits and introduces no architecture, authority, state-scope, or gameplay
    behavior decision.
  - Verification: `python -m unittest
    scripts.tests.test_capture_vnext_linux_ci
    scripts.tests.test_run_vnext_baseline
    scripts.tests.test_verify_vnext_baseline -v` passed 23 tests; `python -m
    py_compile` passed for both runners and their tests; JSON parsing of
    `CMakePresets.json` and `BASELINE_PROVENANCE.json` passed; staged
    `python scripts/verify_vnext_baseline.py --index` and `git diff --cached
    --check` passed. Read-only `git ls-remote` checks resolved checkout v6.0.2
    to `de0fac2e4500dabe0009e67214ff5f5447ce83dd` and upload-artifact v4.6.2
    to `ea165f8d65b6e75b540449e92b4886f43607fa02`. SHA-256-verified actionlint
    v1.7.12 accepted the workflow, and Visual Studio's bundled CMake
    4.3.1-msvc1 parsed the preset file and listed the Linux CI build preset; the
    host condition correctly hides the Linux configure preset on Windows.
  - Owner review: no new decision or user-visible behavior is introduced. The
    implementation follows the already approved ADR-0002 scenarios; hosted CI
    evidence still requires review before this slice can be **Implemented**.
  - Follow-ups: publish the commit through the normal review path, require both
    Linux compiler jobs to pass from the committed tree, inspect the retained
    environment/package/license/install artifacts, record the workflow run URLs
    and artifact evidence, and then mark Slice 1.5 **Implemented**. Slices
    1.6–1.8 remain separate Phase 1 work.

- 2026-08-25 — Slice 1.6 — In Progress
  - Change: this commit adds a dedicated Windows Server 2022 baseline workflow,
    an inherited full-desktop Windows CI preset, and fail-closed capture of the
    resolved runner/MSVC/SDK environment, dependency manifest and lock, vcpkg
    package and license evidence, pinned Qt license reference, installed tree,
    and JSON test reports. It removes the duplicate inherited Windows push job
    while leaving its reusable packaging workflow available to upstream release
    jobs.
  - Decisions: none; Windows Server 2022, Visual Studio 2022/v143, Ninja,
    repository-owned commands, native pinned dependencies, and per-change CI
    implement owner-approved ADR-0002 Option A. Exact action commits are pinned.
    The current local host exposes only Visual Studio 18, so the runner rejected
    it instead of silently expanding the approved toolchain.
  - Verification: `python -m unittest
    scripts.tests.test_capture_vnext_windows_ci
    scripts.tests.test_capture_vnext_linux_ci
    scripts.tests.test_run_vnext_baseline
    scripts.tests.test_verify_vnext_baseline -v` passed 33 tests; `python -m
    py_compile` passed for both platform evidence scripts, the baseline runner,
    verifier, and their tests; JSON parsing and `git diff --cached --check`
    passed; staged `python scripts/verify_vnext_baseline.py --index` enumerated
    exactly 22 intentional differences and verified 17 dependency-declaration
    files. SHA-256-verified actionlint v1.7.12 accepted the Windows, Linux, and
    inherited push workflows. Read-only upstream checks resolved
    `ilammy/msvc-dev-cmd` v1 to
    `0b201ec74fa43914dc39ae48a89fd1d8cb592756`, and the pinned provisioning
    command verified the OpenMW manifest, dependency archive, and existing Qt
    install. The complete hosted Windows build/test/install/evidence run remains
    pending.
  - Owner review: no new architecture, authority, state-scope, security,
    gameplay, or user-visible behavior decision is introduced. The work follows
    the accepted ADR-0002 scenarios; hosted evidence still requires review
    before this slice can be **Implemented**.
  - Follow-ups: publish the committed workflow, require its Windows Server 2022
    job to pass, inspect the retained environment/package/license/install
    artifact, record the run URL and evidence, then mark Slice 1.6
    **Implemented**. Slices 1.7 and 1.8 remain separate Phase 1 work.
- 2026-08-25 — Slice 1.5 — In Progress
  - Change: diagnosed the first two hosted Linux runs and added a bounded repair
    that configures both full-build CI presets with the repository-local
    `build/vnext-baseline-install` prefix. OpenMW 0.51 computes several Linux
    install destinations from the configure-time prefix, so the runner's later
    `cmake --install --prefix` argument alone could not redirect those cached
    absolute paths away from `/usr/local`.
  - Decisions: none; this fixes the already approved ADR-0002 repository-owned
    build/install path without changing platform support, dependencies,
    architecture, authority, state scope, or gameplay behavior.
  - Verification: hosted Linux runs
    [32918583062](https://github.com/poisson-fish/TES3MP/actions/runs/32918583062)
    and
    [32919556702](https://github.com/poisson-fish/TES3MP/actions/runs/32919556702)
    both configured and built the full GCC 13/Clang 18 matrix and passed all
    three upstream test binaries before failing closed when install attempted
    `/usr/local/etc/openmw`. After the repair, `python -m unittest
    scripts.tests.test_capture_vnext_windows_ci
    scripts.tests.test_capture_vnext_linux_ci
    scripts.tests.test_run_vnext_baseline
    scripts.tests.test_verify_vnext_baseline -v` passed 33 tests; Python
    compilation and JSON parsing passed; Visual Studio's bundled CMake
    4.3.1-msvc1 listed the updated Windows presets; and `git diff --check`
    passed. `python scripts/verify_vnext_baseline.py --index` enumerated exactly
    22 intentional differences, verified 17 dependency-declaration inputs, and
    passed; hosted proof remains pending.
  - Owner review: no new decision or user-visible behavior is introduced. The
    repair remains within the accepted ADR-0002 policy and does not require a
    separate decision or demo review.
  - Follow-ups: publish the repair, require the GCC 13 and Clang 18 hosted jobs
    to build, test, install wholly under the repository, capture evidence, and
    upload their retained artifacts before marking Slice 1.5 **Implemented**.
- 2026-08-25 — Slice 1.7 — In Progress
  - Change: this commit adds a dedicated macOS baseline workflow, an inherited
    full-desktop macOS CI preset, commit- and hash-pinned OpenMW dependency
    provisioning for arm64 and x86-64, and fail-closed capture of runner,
    Xcode, AppleClang, Homebrew formula/version/license, vcpkg
    package/license, Qt license, install-tree, and upstream-test evidence. The
    obsolete inherited push workflow is removed after all three platform gates
    received dedicated replacements; the reusable upstream macOS packaging
    workflow remains available to release composition.
  - Decisions: none; macOS 15, Xcode 16/AppleClang, Ninja, arm64 per-change
    coverage, and x86-64 scheduled/release/manual coverage implement the
    owner-approved ADR-0002 Option A. The dependency repository commit and both
    architecture bundle hashes reuse OpenMW 0.51's accepted `2026-02-24`
    dependency generation.
  - Verification: the two commit-pinned OpenMW dependency manifests resolved
    to repository commit `0224d1bb5c7910024be22dc967828f8bf7a41817` and
    matched recorded SHA-256 values
    `6e8cd833e575d66727446ff445546e830254ae76299fa7f6b647a458706262a5`
    (arm64) and
    `f9f9ca974fcbbad9f3298c90d495da28e4de56aa03176e4c0399c183c725fc29`
    (x86-64); their declared archives are independently SHA-512 pinned.
    `python -m unittest scripts.tests.test_capture_vnext_macos_ci
    scripts.tests.test_capture_vnext_windows_ci
    scripts.tests.test_capture_vnext_linux_ci
    scripts.tests.test_run_vnext_baseline
    scripts.tests.test_verify_vnext_baseline -v` passed 43 tests; Python
    compilation and all modified JSON parsing passed; SHA-256-verified
    actionlint v1.7.12 accepted the dedicated and retained macOS workflows;
    Visual Studio's bundled CMake 4.3.1-msvc1 parsed the cross-platform preset
    file; `git diff --check` passed; and `python
    scripts/verify_vnext_baseline.py --index` enumerated exactly 25 intentional
    differences and verified 18 dependency-declaration inputs. Hosted evidence
    remains to be finalized after publication. The first published arm64 run
    (`32923415388`) provisioned the pinned dependencies and completed all 1,343
    Ninja build actions, including the three upstream test executables, before
    failing closed because deployment-mode CMake places those executables in
    `OpenMW.app/Contents/MacOS` while the runner still checked the build root.
    The follow-up maps Darwin test execution to the bundle runtime directory
    and adds a regression assertion for that platform-specific path. Exact
    repair run `32925227820` then executed the arm64 test binary and exposed
    seven upstream Euler-angle assertions whose strict one-float-epsilon bound
    rejects valid Apple Silicon results at singular angles; all observed
    differences are below `1e-6`, the tolerance already used by the adjacent
    Euler-angle test. The bounded follow-up changes only that test tolerance;
    production math and runtime behavior are unchanged.
  - Owner review: no architecture, authority, state-scope, security, gameplay,
    or user-visible behavior decision is introduced. The slice follows the
    already approved ADR-0002 scenarios and does not require another decision
    review or implementation demo.
  - Follow-ups: publish the workflow, require the macOS 15 arm64 job to pass on
    the committed tree, manually dispatch and require the macOS 15 Intel job to
    pass, inspect both retained evidence artifacts, and then mark Slice 1.7
    **Implemented**.
- 2026-08-25 — Slice 1.5 — Implemented
  - Change: accepted the dedicated Ubuntu 24.04 GCC 13 and Clang 18 baseline
    gates after the repository-local install-prefix repair passed on the final
    Slice 1.7 tree.
  - Decisions: none; the jobs implement the already approved ADR-0002 Linux
    policy and introduce no architecture or gameplay behavior.
  - Verification: hosted push run
    [`32927109314`](https://github.com/poisson-fish/TES3MP/actions/runs/32927109314)
    passed both compiler jobs on commit
    `8e378c2c39f52ccd09962e368c3d810398203b4f`. Exact-run artifacts
    `9592632556` (GCC 13, SHA-256
    `17d35e9bdc25dfdd7a721806fd18eda25a4bcbf7f836e77737034df474fe7321`)
    and `9592800250` (Clang 18, SHA-256
    `2657c2718210298f660a62f1d3f4cbd034e943db7ffb14dff4340c3b9460c272`)
    were retained. Content review of the immediately preceding successful
    artifacts confirmed Ubuntu 24.04.4 x86-64, GNU 13.3.0 and Clang 18.1.3,
    CMake 3.31.6, Ninja 1.13.2, 390 installed files per job, and 1,397 + 490
    + 154 upstream tests with zero failures or disabled tests. The GCC artifact
    contained 1,446 package rows and 1,435 copyright records; Clang contained
    1,445 and 1,434 respectively, and every recorded evidence hash matched the
    downloaded file.
  - Owner review: no additional approval is required; the slice is mechanical
    evidence for accepted ADR-0002.
  - Follow-ups: Slice 1.8 remains the only incomplete Phase 1 slice.
- 2026-08-25 — Slice 1.6 — Implemented
  - Change: accepted the dedicated Windows Server 2022/MSVC v143 baseline gate
    after full build, upstream tests, repository-local install, evidence
    capture, and retention passed on the final Slice 1.7 tree.
  - Decisions: none; the job implements the accepted ADR-0002 Windows policy
    and introduces no architecture or gameplay behavior.
  - Verification: hosted push run
    [`32927109296`](https://github.com/poisson-fish/TES3MP/actions/runs/32927109296)
    passed on commit
    `8e378c2c39f52ccd09962e368c3d810398203b4f`; exact-run artifact
    `9592404044` was retained with SHA-256
    `df0e11751e1dac8de012d61eb3e1d864182f4b028d98eeaa192e5f0db09b8435`.
    Downloaded evidence from the immediately preceding successful tree
    confirmed Windows Server 2022 x64, MSVC 19.44.35228/v143, CMake 3.31.6,
    Ninja 1.13.2, Qt 6.6.3, 435 installed files, and 1,395 + 490 + 154
    upstream tests with zero failures or disabled tests. Its hash-verified
    dependency archive contained 120 package lists and 120 license records.
  - Owner review: no additional approval is required; the slice is mechanical
    evidence for accepted ADR-0002.
  - Follow-ups: Slice 1.8 remains the only incomplete Phase 1 slice.
- 2026-08-25 — Slice 1.7 — Implemented
  - Change: accepted the dedicated macOS 15 gates after both supported
    architectures passed full build, all upstream tests, repository-local app
    bundle installation, fail-closed evidence capture, and artifact retention.
    The final bounded baseline fix uses the same `1e-6` single-precision
    tolerance as the adjacent Euler-angle test; production math is unchanged.
  - Decisions: none; the workflow cadence and toolchains implement accepted
    ADR-0002, while the test-only portability repair changes no architecture,
    authority, state scope, gameplay behavior, or user-visible behavior.
  - Verification: per-change arm64 run
    [`32927109264`](https://github.com/poisson-fish/TES3MP/actions/runs/32927109264)
    and manually dispatched arm64/Intel run
    [`32927114955`](https://github.com/poisson-fish/TES3MP/actions/runs/32927114955)
    passed on commit
    `8e378c2c39f52ccd09962e368c3d810398203b4f`. Reviewed exact-run artifacts
    `9592528855` (arm64, SHA-256
    `9a98a8f478b9c735b497e3895fb72c7f2ab64782f8fea4bfd07ec5050174f6ab`)
    and `9593222068` (Intel, SHA-256
    `24dd32264d75d7c402d2929709d021abade8fd5e98509b66a3aaaabefe9973a3`)
    recorded macOS 15/Darwin 24.6.0, Xcode 16.0/AppleClang 16.0.0, CMake
    4.4.0, Ninja 1.13.2, Qt 6.11.1, 1,020 installed paths, and 1,397 + 490
    + 154 upstream tests with zero failures or disabled tests on each
    architecture. Each contained 120 vcpkg package lists and 118 license files;
    the arm64 and Intel archives recorded 168 and 187 Homebrew formulas. All
    six recorded evidence-file hashes matched for both artifacts, including
    dependency-archive SHA-256 values
    `4a8f9437923216dc901fdd193a0ef07d9cc24b98c82813c6381cb82fbb71c81b`
    and `bcba13d79e7b978f2e9baa684b75308a2e6c9586ffca4d6278d473a238c64b36`.
  - Owner review: no additional approval is required; the slice satisfies the
    already approved ADR-0002 scenarios without a new design or demo gate.
  - Follow-ups: Slice 1.8 is now the next eligible implementation slice. Phase
    1 remains **In Progress** until compiled legacy multiplayer exclusion is
    proven.

- 2026-08-26 — Slice 1.8 — Implemented
  - Change: implementation commit `13b4282cfa9918a932d36825479b645c0127e4ff`
    adds `scripts/verify_vnext_legacy_exclusion.py`, its
    failure-case suite, baseline-runner integration, retained JSON evidence,
    provenance coverage, and local documentation. Every supported platform's
    existing `all --ci` path now fails during configuration if an archived
    TES3MP server/browser, packet-processor, RakNet/CrabNet, or CoreScripts
    path or token is tracked by the active tree or reachable from generated
    compilation commands or Ninja build edges.
  - Decisions: none; this is a mechanical Phase 1 exclusion control over the
    already approved OpenMW baseline and existing Ninja CI path. It introduces
    no vNext multiplayer target, architecture, authority, state-scope,
    security, gameplay, or user-visible behavior choice.
  - Verification: `python -m unittest discover -s scripts/tests -v` passed all
    51 repository-owned Python tests, including clean evidence generation and
    rejection of a tracked legacy path, RakNet CMake declaration, legacy
    compilation source, `tes3mp-server` Ninja target, and empty compilation
    database. `python -m py_compile` passed for the new verifier/tests and
    modified runner/tests. `python scripts/verify_vnext_baseline.py --index`
    accounted for exactly 28 intentional differences and verified 19 hashed
    dependency-declaration inputs. From an MSVC 2022 v143 x86-64 developer
    environment, `python scripts/run_vnext_baseline.py all --index` configured
    with CMake 3.31.6/Ninja 1.12.1 and MSVC 19.44.35228.0, verified 3,724 staged
    tracked paths, 53 CMake metadata files, 1,232 compilation commands, and
    1,875 Ninja build edges, rebuilt the affected baseline targets, and passed
    `components-tests` (1,395), `openmw-tests` (490), and `openmw-cs-tests`
    (154) with zero failures. `git diff --cached --check` passed.
    A subsequent committed-tree rerun at
    `13b4282cfa9918a932d36825479b645c0127e4ff` passed `python -m unittest
    discover -s scripts/tests -v` (51 tests), Python compilation, `python
    scripts/verify_vnext_baseline.py`, and `python
    scripts/verify_vnext_legacy_exclusion.py`. From the same MSVC 2022 v143
    x86-64 environment, `python scripts/run_vnext_baseline.py all` then passed
    a fresh configure, the same 3,724-path/53-metadata/1,232-command/1,875-edge
    exclusion proof, the build, and all 2,039 upstream tests with zero failures.
    The retained local evidence records exact source commit `13b4282cfa`, CMake
    3.31.6, Ninja 1.12.1, MSVC 19.44.35228.0, and Qt 6.6.3. `git diff --check`
    passed before this status update.
    Hosted push runs
    [`32934599668`](https://github.com/poisson-fish/TES3MP/actions/runs/32934599668),
    [`32934599672`](https://github.com/poisson-fish/TES3MP/actions/runs/32934599672),
    and
    [`32934599670`](https://github.com/poisson-fish/TES3MP/actions/runs/32934599670)
    passed on exact source `2cad2dbb28b63848f2b43d533d5ebab26aa79cff`.
    Linux GCC 13 and Clang 18 each verified 3,724 tracked paths, 53 CMake files,
    1,216 compile commands, and 1,725 Ninja edges, then passed 1,397 + 490 +
    154 upstream tests. Windows MSVC 2022 v143 verified 3,724/53/1,310/2,166
    and passed 1,395 + 490 + 154 tests. macOS 15 arm64/Xcode 16 verified
    3,724/53/1,284/2,349 and passed 1,397 + 490 + 154 tests. All jobs passed
    baseline provenance for 19 dependency inputs and uploaded retained evidence
    artifacts `9595155142`, `9595191295`, `9595061296`, and `9595066122`, whose
    names bind them to the exact source commit and whose successful upload steps
    include `vnext-legacy-exclusion.json`.
  - Owner review: on 2026-08-26 the project owner explicitly directed acceptance
    of the successful current-HEAD Linux, Windows, and macOS baseline runs for
    Slice 1.8 and directed that the cancelled manual macOS rerun not be used.
    This accepts the three-OS implementation evidence; it does not by itself
    approve the separate Phase 1 exit gate or authorize Phase 2 production.
  - Follow-ups: Phase 1 exit-gate approval is recorded below; hold the Phase 2
    kickoff and obtain the required ADR approvals before dependent production
    implementation begins.

- 2026-08-26 — Phase 1 exit gate — Implemented
  - Change: marked Phase 1 and its program-tracker row **Implemented** after all
    Slices 1.1–1.8 and the clean OpenMW 0.51 baseline exit criteria passed.
  - Decisions: no architecture, authority, state-scope, or gameplay decision
    changed. The owner accepted the successful current-HEAD Linux, Windows, and
    macOS arm64 Slice 1.8 matrix and excluded the cancelled manual macOS rerun
    from evidence; the already accepted Slice 1.7 Intel baseline evidence
    remains unchanged.
  - Verification: the active tree remains based on pinned OpenMW commit
    `f4bec41444214a7903bebd178389ca22ca13f646`; the baseline verifier accounts
    for exactly 28 intentional differences and 19 dependency-declaration
    inputs; supported desktop build/install/upstream-test evidence is recorded
    in Slices 1.4–1.7; and Slice 1.8 records fail-closed tracked-tree,
    CMake-metadata, compilation-database, and Ninja-graph exclusion proof from
    local Windows plus hosted Linux GCC/Clang, Windows MSVC, and macOS arm64
    runs. All accepted jobs passed with retained evidence artifacts.
  - Owner review: explicit Phase 1 exit-gate approval and authorization to mark
    the phase complete received on 2026-08-26. This approval makes Phase 2
    eligible for kickoff; it does not pre-approve any Phase 2 ADR choice or
    dependent production implementation.
  - Follow-ups: hold the Phase 2 kickoff, present ADR-0003 through ADR-0007 and
    deterministic simulation/protocol compatibility decision packets in their
    eligible order, and obtain explicit owner approval before production work.

## Phase 2 — Security and architecture decisions

[Back to the phase tracker](IMPLEMENTATION_PLAN.md#phase-2--security-and-architecture-decisions)

- Authentication must be an owned interface; choosing a transport does not imply
  exposing that library's identity or connection types outside the adapter.
- The protocol version is semantic product metadata, not an engine commit hash.
- Deferred gameplay-specific details may be appended to ADR-0006 before their
  phases, but the initial command/state ownership model must be decided now.
- A selection proof is disposable evidence, not the production wrapper created
  in Phase 6.

- 2026-08-26 — Slice 2.1 — Implemented
  - Change: added accepted
    [`ADR-0003`](adr/ADR-0003-hostile-internet-threat-model.md), defining the
    hostile-Internet threat posture, assets, trust boundaries, attacker
    capabilities, abuse scenarios, phase-owned mitigations, required
    verification, and explicit deferrals.
  - Decisions: the project owner approved recommended Option A. Every client and
    network input, including authenticated clients, remains untrusted; the
    dedicated-server host/operator is the initial trusted boundary; secure
    transport and authentication do not grant gameplay authority; and future
    scripts, persistence, metrics, and administration remain behind separate
    typed least-privilege boundaries.
  - Verification: `python scripts/verify_vnext_baseline.py --index` accounts for
    the new ADR as the 29th intentional difference while retaining all 19
    dependency-input hashes; `git diff --cached --check`, local Markdown-link
    checks, required ADR section/status checks, and the 51-test repository-owned
    Python suite pass.
  - Owner review: Option A explicitly approved in the 2026-08-26 working
    session after presentation of hostile-Internet, cooperative-client, and
    hostile-host options with scenarios, tradeoffs, recommendation, and proposed
    acceptance evidence.
  - Follow-ups: Slice 2.2 is the next eligible decision slice. ADR-0004 must be
    approved before a schema/codec selection or dependent production code.

- 2026-08-26 — Slice 2.2 — In Progress
  - Change: this implementation commit accepted
    [`ADR-0004`](adr/ADR-0004-protocol-schema-codec-evolution-policy.md); added
    an exact source/archive/license lock, safe proof runner, explicitly numbered
    v1/v2 schemas and pinned generated C++, an isolated C++20 proof decoder,
    schema-policy checks, deterministic fuzz corpus generation, a Clang
    ASan/UBSan/libFuzzer target, retained evidence, Android ARM64 assessment,
    and the accepted Windows/Linux/macOS workflow matrix.
  - Decisions: the project owner approved Option A without changes. FlatBuffers
    `v25.12.19` is pinned to commit `7e163021e59cca4f8e1e35a7c828b5c6b7915953`;
    only generated accessors/builders and verifier are permitted; generated
    views remain private and are converted to owned validated values; explicit
    stable IDs and additive optional evolution are mandatory; and all excluded
    dynamic, reflection, nested, mutable, object, RPC, and 64-bit-offset
    surfaces remain prohibited.
  - Verification: local Windows x86-64 MSVC 2022 v143 proof builds pinned `flatc`
    from the SHA-256-verified source, reproduces the committed headers, and
    passes exact framing, every-truncation, corrupt input, byte/depth/table,
    unknown-union, UTF-8/numeric, pre-allocation, no-partial-output, owned
    lifetime, and bidirectional v1/v2 evolution scenarios. The repository-owned
    Python suite passes 64 tests; Actionlint 1.7.12 accepts the new workflow;
    `python scripts/verify_vnext_baseline.py --index` accounts for all 45
    intentional differences and verifies all 34 dependency-input hashes; and
    local Markdown-link, ADR section/status, generated-drift, JSON/compile,
    `git diff --cached --check`, and clean staged-tree checks pass.
  - Owner review: Option A and its complete restricted profile and proposed
    acceptance tests explicitly approved in the 2026-08-26 working session.
  - Follow-ups: obtain and review retained GCC 13, Clang 18 sanitizer/fuzzer,
    macOS arm64, macOS x86-64, and Windows hosted artifacts before changing
    Slice 2.2 to **Implemented**. No Phase 4 production codec or dependent Phase
    3 implementation may begin before the remaining Phase 2 gates.

- 2026-08-26 — Slice 2.2 — In Progress
  - Change: inspected hosted run `32946871261`; Linux GCC 13, Linux Clang 18
    with the sanitizer/fuzzer smoke, and macOS arm64 passed, while Windows
    failed the generated-header byte comparison because Git's Windows checkout
    conversion was not constrained. Added a narrow `.gitattributes` rule that
    pins only the committed proof headers to LF, plus a regression test and
    proof documentation.
  - Decisions: none. This preserves the approved schema, generator arguments,
    generated output, dependency pin, supported matrix, and exact byte-drift
    policy; it only makes the repository representation platform-independent.
  - Verification: hosted run `32946871261` passed Linux GCC 13, Linux Clang 18
    with its 30-second ASan/UBSan/libFuzzer smoke, and macOS arm64. Locally,
    `python -m unittest discover -s scripts/tests -v` passes 65 tests;
    `python scripts/run_vnext_flatbuffers_proof.py` passes from the approved
    Visual Studio 2022 v143 x64 environment and retains evidence; `python -m
    py_compile` passes for the proof runner, its tests, and the baseline
    verifier; JSON parsing passes; cached `git check-attr` reports `text: set`
    and `eol: lf` for both generated headers; `python
    scripts/verify_vnext_baseline.py --index` accounts for all 48 intentional
    differences and verifies all 35 dependency inputs; and `git diff --cached
    --check` passes. Replacement hosted Windows and macOS x86-64 evidence is
    still pending.
  - Owner review: no architecture, authority, state-scope, security, gameplay,
    or user-visible behavior decision is introduced by the line-ending repair.
  - Follow-ups: publish the repair so the Windows proof can rerun, then manually
    dispatch and review the full workflow including macOS x86-64 before marking
    Slice 2.2 **Implemented**.

- 2026-08-26 — Slice 2.2 — In Progress
  - Change: push run `32989871740` passed Windows MSVC, Linux GCC 13, Linux
    Clang 18 with the 30-second sanitizer/fuzzer smoke, and macOS arm64 at
    `da918dd462`; manually dispatched run `32995432424` then passed the full
    five-job matrix including macOS x86-64. Retained-artifact review found that
    Windows checkout conversion changed the recorded lock and schema hashes,
    and that unspecified C++ function-argument evaluation order made three seed
    corpus files differ between compiler families. The working-tree repair pins
    every byte-hashed text input to LF, sequences FlatBuffer builder mutations,
    pins all five deterministic seed hashes in the dependency lock, fails the
    proof on seed drift, and excludes libFuzzer-added working entries from the
    retained seed-corpus identity.
  - Decisions: none. The change enforces the already approved exact-input and
    deterministic-corpus proof requirements without altering the schema,
    protocol evolution policy, dependency selection, or production behavior.
  - Verification: all five jobs and artifacts from run `32995432424` were
    reviewed; their dependency, generated-header, compiler, test, corpus, and
    license fields exposed the two issues above. Locally, the full Windows MSVC
    2022 v143 selection proof passes with the five pinned seed identities;
    `python -m unittest discover -s scripts/tests -v` passes 66 tests; `python
    -m py_compile` passes for the proof runner, its tests, and the baseline
    verifier; `python scripts/verify_vnext_baseline.py --index` accounts for all
    48 intentional differences and verifies all 35 dependency inputs; and `git
    diff --cached --check` passes. Replacement hosted artifact evidence remains
    pending.
  - Owner review: no new architecture, authority, state-scope, security,
    gameplay, or user-visible behavior decision is introduced. Slice 2.2 stays
    **In Progress** because passing jobs alone did not satisfy retained-evidence
    consistency.
  - Follow-ups: publish the determinism repair, rerun the full five-job matrix,
    and compare retained exact input, generated, seed-corpus, license, compiler,
    and test identities before owner review and any **Implemented** status
    change.

- 2026-08-26 — Slice 2.2 — In Progress
  - Change: commit `da24423a1a56ac0e499eebd962b6499db3866b0f`
    published the exact-input and deterministic-seed repair. Manually dispatched
    [run `32996083180`](https://github.com/poisson-fish/TES3MP/actions/runs/32996083180)
    executed the complete five-job selection-proof matrix at that commit.
  - Decisions: none; the repair implements the already approved ADR-0004 proof
    contract and changes no schema, codec selection, protocol behavior, or
    dependency version.
  - Verification: Windows MSVC 2022 v143, Linux GCC 13, Linux Clang 18 with the
    30-second ASan/UBSan/libFuzzer smoke, macOS arm64 Xcode 16, and macOS x86-64
    Xcode 16 all passed. Retained artifacts `9616640336`, `9616634804`,
    `9616631479`, `9616613594`, and `9616613060` were downloaded and compared
    against the committed lock. All five contain the identical lock SHA-256
    `a1cce8c4475460a5dcc29c0164b6743b3e228019068c08d6453c462512a95766`,
    two schema hashes, two generated-header hashes, five pinned seed hashes, and
    Apache-2.0 license SHA-256
    `cfc7749b96f63bd31c3c42b5c471bf756814053e847c10f3eb003417bc523d30`;
    platform/compiler identities and declared proof/fuzz tests also match the
    approved matrix.
  - Owner review: technical completion evidence is ready for review. Explicit
    owner acceptance is pending, so Slice 2.2 remains **In Progress**.
  - Follow-ups: obtain owner completion acceptance, then change Slice 2.2 to
    **Implemented**. Phase 2 still separately requires Slices 2.3–2.6.

- 2026-08-26 — Slice 2.2 — Implemented
  - Change: recorded completion of the owner-approved restricted FlatBuffers
    selection and its exact cross-platform proof at implementation commit
    `da24423a1a56ac0e499eebd962b6499db3866b0f` and evidence-record commit
    `f99239244cf689cdb494615785a95360520ff2f3`.
  - Decisions: no new decision; ADR-0004 remains accepted without amendment.
  - Verification: full five-job run `32996083180`, retained artifacts
    `9616640336`, `9616634804`, `9616631479`, `9616613594`, and `9616613060`,
    cross-platform exact-identity comparison, the local Windows proof, 66
    repository-owned Python tests, Python compilation, baseline provenance,
    JSON, Markdown-link, and diff checks all passed as recorded above.
  - Owner review: the project owner explicitly approved the Slice 2.2
    completion evidence in the 2026-08-26 working session and authorized the
    **Implemented** status change.
  - Follow-ups: none for Slice 2.2. Phase 2 remains **In Progress** and next
    requires completion of Slices 2.3–2.6.

- 2026-08-26 — Slice 2.3 — In Progress
  - Change: accepted and then amended
    [`ADR-0005`](adr/ADR-0005-transport-security-authentication-resumption.md)
    to select standalone GameNetworkingSockets `v1.6.0` direct-IP transport with
    automatic basic encryption, a separate optional shared join-password
    authenticator, and automatic short-lived single-use resume tokens. Updated
    ADR-0003 with the corresponding explicitly accepted active endpoint
    impersonation risk and retained the trust-integration audit as a resolved
    historical gate rather than a patch proposal.
  - Decisions: production transport must be encrypted but is intentionally not
    endpoint-authenticated for the first community-server milestone. Steam,
    relay, P2P, operator certificates, trust anchors, pinning, certificate
    revocation, private upstream APIs, and a maintained GameNetworkingSockets
    patch are excluded. A join password is sent only after encrypted connection
    establishment, is treated as a low-value server-specific secret, and is not
    replaced by a replayable static password hash. Persistent accounts and
    administrative credentials remain outside this decision.
  - Verification: primary upstream release, API, platform, build, security,
    license, maintenance, and standards sources were reviewed on 2026-08-26;
    the tagged-source public/private certificate paths were audited; the
    repository-owned Python suite passes 64 tests; local Markdown links and
    manifest JSON parsing pass; `git diff --cached --check` passes; and
    `python scripts/verify_vnext_baseline.py --index` accounts for all 47
    intentional differences and verifies all 34 dependency-declaration inputs.
    Cross-platform selected-library proof evidence is still pending.
  - Owner review: the owner explicitly approved the amended automatic-encryption
    recommendation on 2026-08-26 after reviewing password handling, resume-token
    behavior, operator burden, the lack of dependency patches, and the active
    man-in-the-middle limitation. This supersedes the earlier configured-trust
    profile and rejects all A1 patch/CA work.
  - Follow-ups: create the disposable GameNetworkingSockets/OpenSSL/Protocol
    Buffers dependency lock and selection proof against the amended ADR-0005
    acceptance tests. Slice 2.2 hosted evidence remains independently in
    progress and was intentionally not monitored in this working session.

- 2026-08-26 — Slice 2.3 — In Progress
  - Change: added the disposable selected-library proof, exact source/archive/
    license lock, safe source acquisition and extraction, restricted static
    build, real encrypted direct-IP client/server capture harness, fault and
    lifecycle scenarios, retained evidence, Android ARM64 assessment, and the
    required five-job desktop workflow. This remains selection evidence and
    creates no production multiplayer target or Phase 6 wrapper.
  - Decisions: after explicit owner approval of dependency Option A, ADR-0005
    records GameNetworkingSockets `1.6.0`, OpenSSL `3.5.8` LTS, Protobuf C++
    `6.33.4`, and Abseil `20250512.1`. The proof uses unmodified upstream
    sources, static libraries, OpenSSL, and the already approved exclusions.
    Proof-only resource budgets are test limits, not production interface or
    gameplay-state decisions.
  - Verification: a clean local Windows x86-64 MSVC 2022 run verified every
    pinned archive and license, built all four source dependencies with the
    approved profile, and passed encryption/plaintext rejection, passive
    password/token canary capture, authentication ordering and failure,
    resume-token contention/rotation/invalidation, generation teardown,
    bounded queue/flood, reliable ordering, separate-lane head-of-line, and
    stable telemetry scenarios. The lock validator passes; the repository-owned
    Python suite passes 75 tests; Python compilation passes; Actionlint 1.7.12
    accepts the workflow; and JSON, Markdown-link, provenance, and diff checks
    pass. Hosted evidence is still pending.
  - Owner review: the project owner explicitly approved dependency Option A on
    2026-08-26 before implementation. No additional architecture, authority,
    state-scope, or gameplay-behavior decision was made by this proof slice.
  - Follow-ups: extend the proof through the remaining ADR-0005 slow-reader,
    excessive-segment, handshake/disconnect-flood, and actual close/unread-data
    lifecycle cases without treating proof budgets as production policy. Then
    publish and run the complete Windows, GCC 13, Clang 18 ASan/UBSan, macOS
    arm64, and scheduled/manual macOS x86-64 matrix; compare retained exact
    dependency, license, toolchain, build-profile, test, and redaction evidence;
    and request owner completion acceptance before changing Slice 2.3 to
    **Implemented**. Slices 2.4–2.6 remain separately required.

- 2026-08-26 — Slice 2.3 — In Progress
  - Change: completed the remaining disposable hostile-resource and actual
    lifecycle proof cases. The real GameNetworkingSockets harness now exercises
    slow-reader receive byte/message limits and recovery, excessive segments per
    packet, fail-closed maximum-message handling, a 32-connection handshake and
    disconnect flood behind an eight-connection admission cap and bounded
    callback drain, and close-with-unread-data handle invalidation. The exact
    proof budgets and retained scenario list are locked and regression-tested.
  - Decisions: none. The added numeric limits are disposable proof workloads,
    not production transport budgets, architecture, authority, state-scope, or
    gameplay behavior. They exercise the already owner-approved ADR-0005
    acceptance scenarios without changing the selected dependencies or profile.
  - Verification: a clean Windows x86-64 MSVC 2022 v143 run of `python
    scripts/run_vnext_gamenetworkingsockets_proof.py` reverified all four source
    archives and five licenses, rebuilt OpenSSL 3.5.8, Protobuf 6.33.4/Abseil
    20250512.1, and unmodified GameNetworkingSockets 1.6.0, then passed all nine
    proof scenarios in 2.2 seconds and retained lock hash
    `d5263f5cd197ee39f2ed862097b691396f4ad7759c19d715d98eb3c562ae295c`.
    An incremental MSVC `/W4 /WX` build passed, and `ctest --repeat
    until-fail:20` passed 20 consecutive runs in 33.06 seconds. `python -m
    unittest discover -s scripts/tests` passed 76 tests; both changed Python
    files passed `py_compile`; lock validation passed; `python
    scripts/verify_vnext_baseline.py --index` accounted for all 56 intentional
    differences and verified all 43 dependency inputs; and `git diff --cached
    --check` passed. Both changed JSON files parsed, all 32 local vNext Markdown
    links resolved, and the retained proof evidence contained neither password
    nor resume-token canaries.
  - Owner review: no new owner decision was required. Slice 2.3 completion
    acceptance remains pending the approved hosted matrix and retained-artifact
    consistency review.
  - Follow-ups: publish the completed proof, run Windows MSVC 2022, Linux GCC
    13, Linux Clang 18 ASan/UBSan, macOS arm64, and manual macOS x86-64; compare
    exact dependency, license, lock, toolchain, build-profile, budget, test, and
    redaction evidence; then request owner completion acceptance before changing
    Slice 2.3 to **Implemented**. Slices 2.4–2.6 remain separately required.

- 2026-08-26 — Slice 2.3 — In Progress
  - Change: diagnosed the first complete hosted proof run and corrected one
    portable API type mismatch in the disposable proof. GameNetworkingSockets'
    `SendMessages` result parameter uses its public `int64` typedef; using
    `std::int64_t` happened to match on Windows but is `long` rather than the
    required `long long` on the hosted Linux ABI.
  - Decisions: none. The one-line correction adopts the selected library's
    public API type and changes no dependency, proof budget, transport behavior,
    architecture, authority, state scope, or gameplay behavior.
  - Verification: hosted run
    [`33011891871`](https://github.com/poisson-fish/TES3MP/actions/runs/33011891871)
    passed Windows MSVC 2022 v143 and macOS arm64 Xcode 16, while GCC 13 job
    `98320008864` and Clang 18 ASan/UBSan job `98320008970` both failed at the
    same compile-time pointer-type mismatch; the push-only macOS x86-64 job was
    correctly skipped. Successful retained artifacts `9623265318` and
    `9623081766` were downloaded and compared: both retain the identical lock
    SHA-256 `d5263f5cd197ee39f2ed862097b691396f4ad7759c19d715d98eb3c562ae295c`,
    dependency and license identities, build profile, budgets, nine-test list,
    passing CTest output, and no password or resume-token canaries. After the
    correction, the incremental MSVC 2022 `/W4 /WX` build and all nine proof
    scenarios passed locally in 2.09 seconds. The 76-test repository-owned
    Python suite, exact lock validation, both changed JSON files, all 32 local
    vNext Markdown links, and staged-diff checks passed; baseline provenance
    accounted for all 56 intentional differences and verified all 43 dependency
    inputs.
  - Owner review: no new owner decision was required. Completion acceptance
    remains pending a fully passing replacement matrix and retained-artifact
    consistency review.
  - Follow-ups: publish the portable type correction, require replacement GCC
    13 and Clang 18 ASan/UBSan evidence plus the already approved Windows and
    macOS jobs, run the manual macOS x86-64 job, compare all five retained
    artifacts, and request owner completion acceptance before changing Slice
    2.3 to **Implemented**.

- 2026-08-26 — Slice 2.3 — In Progress
  - Change: amended the approved sanitizer proof profile after the first
    portable replacement run reached execution. The Clang job now builds
    Protobuf and Abseil with the same ASan/UBSan instrumentation as their
    GameNetworkingSockets consumers, keeps OpenSSL explicitly unsanitized, and
    excludes UBSan `function` only on the unmodified GameNetworkingSockets
    target. The proof harness retains the check; container-overflow, leak, and
    halt-on-error behavior remain enabled; and regression tests reject broad
    runtime suppression.
  - Decisions: the owner explicitly approved recommended sanitizer Option A on
    2026-08-26. Keep the pinned, unmodified dependency selection and make its
    C++ instrumentation coherent; document the one narrow upstream callback
    cast exclusion; and treat any remaining container report as a dependency
    blocker. The owner rejected immediate patch/upgrade/replacement work and
    broad process-wide sanitizer suppression.
  - Verification: hosted run
    [`33015281679`](https://github.com/poisson-fish/TES3MP/actions/runs/33015281679)
    passed GCC 13 job `98331778758`, Windows MSVC 2022 v143, and macOS arm64
    Xcode 16. Clang 18 job `98331778984` compiled successfully, then reported
    the pinned GameNetworkingSockets callback type-erasure under UBSan and a
    Protobuf repeated-field container report across the previously mixed
    instrumentation boundary; manual macOS x86-64 was correctly skipped. After
    the approved amendment, lock validation and 12 focused runner tests passed,
    and a clean locked Windows MSVC 2022 dependency rebuild passed all nine
    scenarios in 2.16 seconds with retained evidence and lock SHA-256
    `367fd5820e41b77c5857ccf1dff9b2d0698cebf8dba5385f81204f1dba64350c`.
    The complete repository-owned Python suite passed 78 tests; Python
    compilation, both changed JSON files, all 32 local vNext Markdown links,
    retained-evidence canary scanning, and staged-diff checks passed. Baseline
    provenance accounted for all 56 intentional differences and verified all
    43 dependency inputs.
  - Owner review: the sanitizer-scope decision and ADR-0005 amendment are
    explicitly approved. Slice completion acceptance remains pending a fully
    passing hosted matrix and five-artifact consistency review.
  - Follow-ups: publish the approved sanitizer profile, require its replacement
    Clang 18 ASan/UBSan run to pass without the container report or any broad
    suppression, run manual macOS x86-64, compare all five retained artifacts,
    and request owner completion acceptance before changing Slice 2.3 to
    **Implemented**.

- 2026-08-26 — Slice 2.3 — Implemented
  - Change: recorded completion of the owner-approved restricted standalone
    GameNetworkingSockets selection and its five-platform proof at implementation
    commit `d5d7a1d1f49715bd41f2eb090393785e67924598`.
  - Decisions: no new transport or security decision was introduced. The owner
    accepted the completed evidence for ADR-0005 and authorized the Slice 2.3
    status change. The approved automatic-encryption profile, explicit lack of
    endpoint authentication, shared join-password boundary, single-use resume
    tokens, dependency pins, and narrow sanitizer scope remain unchanged.
  - Verification: push run
    [`33020799953`](https://github.com/poisson-fish/TES3MP/actions/runs/33020799953)
    passed Linux GCC 13, Linux Clang 18 ASan/UBSan, Windows MSVC 2022 v143, and
    macOS arm64. Manually dispatched run
    [`33022971583`](https://github.com/poisson-fish/TES3MP/actions/runs/33022971583)
    passed the complete matrix, including macOS x86-64. Retained artifacts
    `9627645351`, `9627621339`, `9627619303`, `9627437471`, and `9627400661`
    were downloaded and compared against the committed lock. All five contain
    identical dependency, transitive-license, build-profile, budget,
    endpoint-identity-claim, and nine-scenario identities; every embedded lock
    matches SHA-256
    `367fd5820e41b77c5857ccf1dff9b2d0698cebf8dba5385f81204f1dba64350c`;
    every retained license matches its manifest; and every scenario passes.
    Exactly one artifact uses the approved Clang 18.1.3 ASan/UBSan profile, with
    no sanitizer report, runtime error, credential canary, or broad suppression.
  - Owner review: the project owner explicitly approved completion Option A in
    the 2026-08-26 working session and authorized the **Implemented** status.
  - Follow-ups: none for Slice 2.3. Phase 2 remains **In Progress**; Slice 2.4 is
    next and requires owner review of authority and state-scope options before
    ADR-0006 or dependent implementation work.

- 2026-08-26 — Slice 2.4 — In Progress
  - Change: added proposed
    [`ADR-0006`](adr/ADR-0006-authority-state-scope-prediction-presentation.md)
    with separate proposal/validation/commit authority, state-scope,
    visibility, lifetime, prediction, presentation, and delegation concepts;
    five option sets; representative single-player, two-player, late-join,
    reconnect, contention, hostile-client, VR, delegation, script/admin/replay,
    and resync scenarios; an initial subsystem mapping; all ten domain GDR
    question sets; and named acceptance tests/demo steps. No production schema,
    state model, reducer, API, or multiplayer target was added.
  - Decisions: none accepted. The proposal recommends Option A for Decisions
    1–5: server command/reducer mutation; explicit scope and lifetime with
    global physical-world and per-player progression defaults; reversible local
    prediction with authoritative reconciliation; typed non-durable
    presentation samples; and no delegation in the first slice with future
    server-validated epoch leases. All five remain pending owner review.
  - Verification: `python -m unittest discover -s scripts/tests -v` passed all
    78 repository-owned tests. `python scripts/verify_vnext_baseline.py --index`
    accounted for exactly 57 intentional differences, including the proposed
    ADR, and verified all 43 dependency-declaration inputs. `python -m
    json.tool docs/vnext/BASELINE_PROVENANCE.json`, `git diff --cached
    --check`, a local-link scan across all 16 vNext Markdown files, and focused
    checks for proposed status, required ADR sections, five decision sets, and
    GDR-0001 through GDR-0010 coverage all passed. No production target changed,
    so a product build or runtime test is not applicable to this decision-packet
    step.
  - Owner review: decision packet prepared for the 2026-08-26 working session;
    explicit approval or amendment is pending. Slice 2.4 remains **In Progress**
    and dependent production work is not authorized.
  - Follow-ups: review Decisions 1–5 with the owner, amend or accept ADR-0006,
    run the approved scenario/document checks, and request completion acceptance
    before marking Slice 2.4 **Implemented**. Slices 2.5 and 2.6 remain gated.

- 2026-08-26 — Slice 2.4 — Implemented
  - Change: accepted
    [`ADR-0006`](adr/ADR-0006-authority-state-scope-prediction-presentation.md)
    and recorded the approved initial authority, state-scope, prediction,
    presentation-sample, and future-delegation framework. The ADR and project
    status now make Slice 2.5 the next eligible Phase 2 decision slice. No
    production schema, state model, reducer, API, or multiplayer target was
    added.
  - Decisions: the owner approved Option A for Decisions 1–5. Canonical mutation
    uses server commands/reducers; scope and lifetime are explicit with the
    recommended bounded defaults; prediction is reversible presentation;
    presentation samples are typed, bounded, latest-wins, and non-durable; and
    the first slice has no delegation while later delegation requires
    server-validated epoch leases. For Decision 1, the owner required a
    friends-server validation profile: retain structural/safety, authority,
    revision/idempotency/epoch, and basic domain checks, but do not add elaborate
    anti-cheat or duplicate complete OpenMW mechanics solely to police input.
  - Verification: `python -m unittest discover -s scripts/tests -v` passed all
    78 repository-owned tests. `python scripts/verify_vnext_baseline.py --index`
    accounted for exactly 57 intentional differences and verified all 43
    dependency-declaration inputs. `python -m json.tool
    docs/vnext/BASELINE_PROVENANCE.json`, `git diff --cached --check`, a
    local-link scan across all 16 vNext Markdown files, and focused assertions
    for accepted status/date, all five approvals, the friends-server condition,
    GDR-0001 through GDR-0010 coverage, Slice 2.4 **Implemented**, Phase 2 **In
    Progress**, Phase 3 **Not Started**, and the Slice 2.5 next pointer all
    passed. No production target changed, so a product build or runtime test is
    not applicable to this decision-only completion.
  - Owner review: explicit A/A/A/A/A approval received in the 2026-08-26 working
    session, including the Decision 1 validation-depth condition. The owner
    approved this architecture framework only; domain gameplay details remain
    gated by GDR-0001 through GDR-0010.
  - Follow-ups: none for Slice 2.4. Phase 2 remains **In Progress**; Slice 2.5
    must present OpenMW hook and patch-queue options for owner approval before
    ADR-0007 or dependent implementation work.

- 2026-08-26 — Slice 2.5 — In Progress
  - Change: inspected the pinned OpenMW 0.51 composition, frame ordering, input,
    world/player/cell operations, Lua synchronization/events, CMake target, and
    application-test seams; confirmed that the active OpenMW application tree
    still has no vNext delta; and added proposed
    [`ADR-0007`](adr/ADR-0007-openmw-hook-patch-queue-policy.md). The packet
    presents four decision sets, repository-backed scenarios, a bounded initial
    hook budget, patch-registry fields, upstreaming criteria, named acceptance
    tests, failure modes, and Phase 8.1's exact-hook review boundary. No adapter,
    hook, patch registry, CMake target, or production code was added.
  - Decisions: none accepted. The proposal recommends Option A for Decisions
    1–4: a native app-local C++ adapter/coordinator; one main-thread frame seam
    plus feature-specific semantic hooks only as approved behavior requires;
    normal buildable Git commits plus a machine-checked semantic patch registry;
    and upstreaming general engine seams/fixes while retaining multiplayer
    policy locally. All four remain pending owner review.
  - Verification: `python -m unittest discover -s scripts/tests -v` passed all
    78 repository-owned tests. `python scripts/verify_vnext_baseline.py --index`
    accounted for exactly 58 intentional differences and verified all 43
    dependency-declaration inputs. `python -m json.tool
    docs/vnext/BASELINE_PROVENANCE.json`, `git diff --cached --check`, a
    local-link scan across all 17 vNext Markdown files, and focused ADR-0007
    structure/status and Phase 2/3 gate assertions passed. Repository-source
    assertions verified input/Lua/state/mechanics/physics/world frame ordering,
    `main.cpp`/`Engine` composition, Lua's separate-thread and synchronized-
    update behavior, no active OpenMW application/test delta from pinned
    `f4bec4144`, and no existing adapter or client-session CMake target.
  - Owner review: decision packet prepared for the 2026-08-26 working session;
    explicit approval or amendment is pending. Slice 2.5 remains **In Progress**
    and no OpenMW hook or dependent adapter implementation is authorized.
  - Follow-ups: review Decisions 1–4 with the owner, amend or accept ADR-0007,
    rerun the accepted document/provenance gates, and request completion
    acceptance before marking Slice 2.5 **Implemented**. Slice 2.6 and all Phase
    3 production targets remain gated.

- 2026-08-26 — Slice 2.5 — Implemented
  - Change: accepted
    [`ADR-0007`](adr/ADR-0007-openmw-hook-patch-queue-policy.md) and recorded the
    approved native integration, bounded semantic-hook, buildable-commit/patch-
    registry, and local upstream-candidate preparation framework. No local
    upstream-preparation clone was created because no engine patch or upstream
    candidate exists yet; its path remains developer-local and it is created
    only if a real candidate needs a clean changeset.
  - Decisions: the owner approved Option A for Decisions 1–3 and amended Option
    A for Decision 4. The project will not proactively contact OpenMW or submit
    patches for now. A separate local OpenMW repository may later prepare clean
    general-purpose changesets, but it is disposable and cannot be a submodule,
    source of truth, build input, CI requirement, or replacement for vNext's
    commits, patch registry, and baseline manifest. Upstream contact requires a
    later explicit owner decision.
  - Verification: all 78 repository-owned Python tests pass; indexed baseline
    provenance passes with 59 intentional differences and 43 dependency inputs;
    JSON parsing/path ordering, all 40 local Markdown links, the accepted ADR-
    0007/local-repository constraints, Phase 2/3 status gates, the absence of a
    staged OpenMW source delta, and staged/unstaged diff checks pass. Source
    inspection reconfirmed the current OpenMW main-thread frame order, Lua's
    synchronized application point, and the monolithic `openmw-lib` composition.
  - Owner review: explicit approval received in the 2026-08-26 working session,
    including the Decision 4 local-repository/no-current-upstreaming amendment.
    Exact Phase 8 hook call sites and gameplay semantics remain separately
    gated.
  - Follow-ups: none for Slice 2.5. Phase 2 remains **In Progress**; Slice 2.6 is
    the next eligible decision slice and all Phase 3 production targets remain
    gated until the Phase 2 exit review.

- 2026-08-26 — Slice 2.6 — In Progress
  - Change: inspected the accepted deterministic, protocol-evolution, and
    engine-independence constraints and added proposed
    [`ADR-0013`](adr/ADR-0013-deterministic-simulation-protocol-compatibility.md).
    It presents five owner-reviewable option sets, adversarial scenarios, named
    acceptance tests, consequences, failure mitigations, and replacement
    triggers. No production target or runtime behavior was added.
  - Decisions: none accepted. The proposal recommends Option A for all five
    decisions: a 30 Hz logical tick with bounded catch-up; checked integer/fixed-
    point canonical numerics; writer-assigned eligible tick and ingress order;
    injected time plus versioned labeled random streams and canonical bytes;
    and a released current-plus-previous-minor compatibility window with
    bounded required/optional capability negotiation.
  - Verification: all 78 repository-owned Python tests pass; indexed baseline
    provenance passes with 59 intentional differences and 43 dependency inputs;
    JSON parsing/path ordering, all 40 local Markdown links, ADR-0013's proposed
    five-decision/owner-pending state, Slice 2.6/Phase 2/Phase 3 status gates,
    and staged/unstaged diff checks pass.
  - Owner review: pending explicit approval or amendment of Decisions 1–5.
    Exact gameplay-domain values remain GDR-gated and are not selected here.
  - Follow-ups: present the five decision sets to the owner. Slice 2.6 remains
    **In Progress**, Phase 2 remains **In Progress**, and Phase 3 production work
    remains gated.

- 2026-08-26 — Slice 2.6 — Implemented
  - Change: accepted
    [`ADR-0013`](adr/ADR-0013-deterministic-simulation-protocol-compatibility.md)
    and recorded the approved deterministic tick, canonical numeric, command-
    ordering, logical-time/randomness/canonical-encoding, and protocol-
    compatibility contracts. No production target or runtime behavior was
    added.
  - Decisions: the owner approved Option A for Decisions 1–5: 30 Hz with no more
    than four due ticks per scheduler pump; checked `1/1024`-unit fixed-point
    positions and 32-bit turn orientation; single-writer eligible-tick/ingress-
    ordinal order; injected logical time and versioned labeled random streams;
    and current-plus-previous-minor capability negotiation within one major.
  - Verification: all 78 repository-owned Python tests pass; indexed baseline
    provenance passes with 59 intentional differences and 43 dependency inputs;
    JSON parsing/path ordering, all 41 local Markdown links, ADR-0013's accepted
    five-decision state, Slice 2.6/Phase 2/Phase 3 status gates, and staged/
    unstaged diff checks pass.
  - Owner review: explicit approval received in the 2026-08-26 working session.
    Exact gameplay-domain values remain GDR-gated. This approval does not itself
    approve the Phase 2 exit review or Phase 3 kickoff.
  - Follow-ups: none for Slice 2.6. Every Phase 2 slice is implemented, but Phase
    2 remains **In Progress** pending its required explicit exit-gate review;
    Phase 3 remains **Not Started**.

- 2026-08-26 — Phase 2 — Implemented
  - Exit review: the owner explicitly approved the Phase 2 exit and movement to
    Phase 3 in the 2026-08-26 working session.
  - Gate evidence: ADR-0003 through ADR-0007 and ADR-0013 are accepted; the
    selected FlatBuffers and GameNetworkingSockets proof targets build on the
    supported Linux, Windows, and macOS matrices without OpenMW coupling; and
    ADR-0003 covers every threat category named by the exit gate.
  - Verification: all 78 then-existing repository-owned Python tests passed;
    indexed baseline provenance passed with 59 intentional differences and 43
    dependency inputs; JSON, all 41 local Markdown links, accepted-decision,
    phase-status, and diff checks passed.
  - Follow-ups: none. Later gameplay decisions remain GDR-gated, OpenMW hook
    sites remain Phase 8-gated, and the owner separately approved ADR-0014 for
    the Phase 3 kickoff.

## Phase 3 — Independent targets and test scaffold

[Back to the phase tracker](IMPLEMENTATION_PLAN.md#phase-3--independent-targets-and-test-scaffold)

- Prefer explicit constructors and checked arithmetic for network-visible numeric
  types. Do not use primitive aliases where IDs or epochs could be mixed.
- Canonical transforms need a documented coordinate system, units, numeric
  representation, and normalization rules before they enter a schema.
- Test support should be a first-class reusable library, not private helpers tied
  to one integration test.
- Sanitizer and fuzz options should remain opt-in for local builds and explicit in
  CI presets so normal developer builds stay predictable.

- 2026-08-26 — Slice 3.1 — In Progress
  - Change: added the accepted
    [`ADR-0014`](adr/ADR-0014-phase3-target-topology-boundary-enforcement.md)
    target topology, five engine-independent C++20 static libraries under
    `components/tes3mp`, one app-local `openmw_tes3mp_adapter`, stable `TES3MP::`
    aliases, a direct-dependency allowlist, forbidden-include-family checks, and
    focused fail-closed CMake graph tests. The adapter is not linked into the
    OpenMW executable and all translation units are behavior-free anchors.
  - Decisions: the owner approved Phase 3 kickoff and Option A: separate
    `tes3mp_protocol`, `tes3mp_transport`, `tes3mp_server_core`,
    `tes3mp_client_session`, and `tes3mp_test_support` targets plus the app-local
    adapter. GameNetworkingSockets and gameplay/OpenMW lifecycle behavior remain
    outside this slice.
  - Verification: all 83 repository-owned Python tests pass, including the five
    focused boundary tests. Those tests independently
    configure/build the five non-OpenMW targets, configure/build the adapter
    against an isolated `openmw-lib` leaf, reject an undeclared direct link,
    reject a forbidden include family, and reject a reverse production-to-test-
    support edge. The real `build/vnext-baseline` graph configures and builds
    `tes3mp_test_support` and `openmw_tes3mp_adapter` with MSVC/Ninja. The real
    legacy-exclusion gate passes across 3,767 tracked paths, 58 CMake files,
    1,239 compile commands, and 1,935 Ninja edges. Indexed provenance passes with
    73 intentional differences and 43 dependency inputs; JSON/path ordering,
    all 43 local Markdown links, ADR/status/graph assertions, and staged/
    unstaged diff checks pass.
  - Owner review: the architecture is approved; implementation-demo acceptance
    is pending before Slice 3.1 may become **Implemented**.
  - Follow-ups: run the full repository and provenance gates, present the target
    graph/failure evidence, and request demo acceptance. Slice 3.2 production
    work remains gated.

- 2026-08-26 — Slice 3.1 — Implemented
  - Change: retained the exact accepted ADR-0014 target graph and fail-closed
    checks demonstrated in the preceding in-progress note; no runtime behavior
    was added during acceptance.
  - Decisions: no amendment. The owner accepted the six-target Option A
    implementation as demonstrated.
  - Verification: all 83 repository-owned Python tests, focused independent/
    adapter configure-build checks, intentional forbidden-link/include failures,
    real MSVC/Ninja targets, real legacy exclusion, indexed provenance with 73
    intentional differences and 43 dependency inputs, all 43 local Markdown
    links, graph/status assertions, and diff checks pass.
  - Owner review: implementation-demo acceptance received in the 2026-08-26
    working session.
  - Follow-ups: none for Slice 3.1. Slice 3.2 is the next eligible slice; its
    strong-type architecture requires owner approval before public APIs land.

- 2026-08-26 — Slice 3.2 — In Progress
  - Change: inspected OpenMW's existing `Misc::StrongTypedef`, ESM identity,
    ordering, hashing, formatting, and overflow conventions; added proposed
    [`ADR-0015`](adr/ADR-0015-strong-value-types-identity-counter-policy.md)
    with five option sets, scopes, scenarios, acceptance tests, consequences,
    failure mitigations, and replacement triggers. No production header or
    strong value implementation was added.
  - Decisions: none accepted. The proposal recommends Option A for all five
    decisions: ten scoped `u64` semantic types; zero-invalid identities/one-based
    counters with tick zero valid; a project-owned explicit policy template;
    optional-returning checked advancement; and type-qualified debug formatting
    with codec/allocation separation.
  - Verification: all 83 repository-owned Python tests pass; indexed provenance
    passes with 74 intentional differences and 43 dependency inputs; JSON/path
    ordering, all 45 local Markdown links, ADR-0015's proposed five-decision/
    owner-pending state, Slice 3.1/3.2 and Phase 2/3 status assertions, and
    staged/unstaged diff checks pass.
  - Owner review: pending explicit approval or amendment of Decisions 1–5.
  - Follow-ups: present the five decisions to the owner. Slice 3.2 production
    work remains gated.

- 2026-08-26 — Slice 3.2 — In Progress
  - Change: accepted ADR-0015 and added `tes3mp/strong_value.hpp` plus
    `tes3mp/value_types.hpp` to `tes3mp_protocol`. The API defines ten scoped
    semantic `uint64_t` types, zero-invalid named factories except zero-valid
    `ServerTick`, declared counter `initial()`/checked `next()`, explicit raw
    access, same-type total ordering/hash, and stable `TypeName{decimal}` text.
    A standalone C++20 contract executable is composed through
    `tes3mp_test_support`; codecs, allocation, persistence, schemas, OpenMW, and
    gameplay behavior remain absent.
  - Decisions: the owner approved Option A for Decisions 1–5: the ten-type `u64`
    catalog and scopes, unrepresentable invalid/default state, project-owned
    explicit policy template, optional-returning no-wrap advancement, and stable
    type-qualified formatting with explicit external boundaries.
  - Verification: all 84 repository-owned Python tests pass, including six
    focused boundary tests. Their isolated CMake
    projects compile/link all independent targets and aliases, run the C++
    semantic contract, build the adapter leaf, and prove forbidden direct links
    and includes fail closed. The C++ contract contains compile-time negative
    construction/conversion/comparison/arithmetic checks, underlying size/
    alignment/trivial-copy assertions, plus runtime zero/start/order/hash/
    formatting/advance/exhaustion checks. The real MSVC/Ninja baseline graph
    builds and runs `tes3mp_protocol_tests_run`. Real legacy exclusion passes
    across 3,771 tracked paths, 58 CMake files, 1,240 compile commands, and 1,942
    Ninja edges. Indexed provenance passes with 77 intentional differences and
    43 dependency inputs; JSON/path ordering, all 45 local Markdown links,
    accepted ADR/catalog/isolation/status assertions, and staged/unstaged diff
    checks pass.
  - Owner review: ADR approval is recorded; implementation-demo acceptance is
    pending before Slice 3.2 may become **Implemented**.
  - Follow-ups: run all gates and present boundary/max-value evidence. Slice 3.3
    production work remains gated.

- 2026-08-26 — Slice 3.2 — Implemented
  - Change: retained the exact accepted ADR-0015 API and contract tests
    demonstrated in the preceding in-progress note; no API or runtime behavior
    changed during acceptance.
  - Decisions: no amendment. The owner accepted the ten-type Option A
    implementation as demonstrated.
  - Verification: all 84 repository-owned Python tests and six focused target-
    boundary tests pass. The real MSVC/Ninja graph builds and runs
    `tes3mp_protocol_tests_run`; real legacy exclusion passes across 3,771
    tracked paths, 58 CMake files, 1,240 compile commands, and 1,942 Ninja
    edges. Indexed provenance with 77 intentional differences and 43 dependency
    inputs, JSON/path ordering, all 45 local Markdown links, status/contract
    assertions, and staged/unstaged diff checks pass.
  - Owner review: implementation-demo acceptance received in the 2026-08-26
    working session.
  - Follow-ups: none for Slice 3.2. Slice 3.3 is the next eligible slice, and
    its spatial/state API decisions require owner approval before production
    headers land.

- 2026-08-26 — Slice 3.3 — In Progress
  - Change: inspected OpenMW 0.51 cell, worldspace, exterior-grid, position,
    and rotation representations plus the later protocol, server-core,
    content-identity, cell/interest, and movement gates. Added proposed
    [`ADR-0016`](adr/ADR-0016-canonical-spatial-command-snapshot-primitives.md)
    with five option sets, scenarios, acceptance tests, failure mitigations,
    and replacement triggers. No Slice 3.3 production header, codec, schema,
    state storage, reducer, adapter conversion, or gameplay behavior was added.
  - Decisions: none accepted. The proposal recommends Option A for Decisions
    1–5: a tagged context-scoped interior/exterior cell value; contextual
    absolute exterior coordinates; three-axis fixed-point transform values with
    fixed right-handed composition; linear velocity only; and value-only
    command/order metadata plus per-entity snapshot values.
  - Verification: all 84 repository-owned Python tests pass, including the six
    focused independent-target tests and the existing C++ strong-value
    contract. Indexed provenance passes with 78 intentional differences and 43
    dependency inputs; JSON/path ordering, all 55 local Markdown links,
    ADR-0016's proposed five-decision/owner-pending state, Slice 3.2/3.3 and
    Phase 3 status assertions, and staged/unstaged diff checks pass. A new
    product build/runtime test is not applicable because this step changes only
    the decision packet and production code remains gated.
  - Owner review: pending explicit approval or amendment of Decisions 1–5.
  - Follow-ups: present Decisions 1–5 to the owner, amend or accept ADR-0016,
    then implement only the approved Slice 3.3 value surface and independent
    contract tests.

- 2026-08-26 — Slice 3.3 — In Progress
  - Change: accepted ADR-0016 and added `CellSpaceId`, tagged interior/exterior
    `CellId`, signed fixed-point `Position3`, modular `Turn32`, three-axis
    `Orientation3`, `Transform`, and per-tick `LinearVelocity3` values to
    `tes3mp_protocol`. Added fully initialized client-command, optional entity-
    precondition, writer-admission, and authoritative spatial-snapshot value
    records. A bounded little-endian snapshot round trip exists only in
    `tes3mp_test_support`; it is not a protocol schema or wire contract.
  - Decisions: the owner approved Option A for Decisions 1–5: context-scoped
    tagged cells, contextual absolute exterior coordinates, right-handed
    `Rz * Ry * Rx` fixed-turn orientation, linear velocity only, and value-only
    metadata/snapshot records. Content mapping, final cell rules, gameplay
    validation, movement, codecs, collection budgets, reducers, delivery, and
    OpenMW conversions remain deferred to their owning gates.
  - Verification: all 84 repository-owned Python tests pass, including six
    focused boundary tests whose independent graph builds and runs both C++
    value-contract executables without OpenMW. The real MSVC/Ninja baseline
    graph configures, builds, and runs `tes3mp_protocol_tests_run`. Real legacy
    exclusion passes across 3,777 tracked paths, 58 CMake files, 1,242 compile
    commands, and 1,947 Ninja edges. Indexed provenance passes with 83
    intentional differences and 43 dependency inputs. JSON/path ordering,
    all 55 local Markdown links, accepted ADR/decision/status assertions, and
    staged/unstaged diff checks pass.
  - Owner review: ADR approval is recorded; implementation-demo acceptance is
    pending before Slice 3.3 may become **Implemented**.
  - Follow-ups: run all gates and present the interior/exterior, negative-grid,
    numeric-boundary, orientation-basis, command/admission-separation, malformed
    test-encoding, and round-trip evidence. Slice 3.4 remains gated.

- 2026-08-26 — Slice 3.3 — Implemented
  - Change: retained the exact accepted ADR-0016 value surface and independent
    contract tests demonstrated in the preceding in-progress note; no API or
    runtime behavior changed during acceptance.
  - Decisions: no amendment. The owner accepted the five-decision Option A
    implementation as demonstrated.
  - Verification: all 84 repository-owned tests, six focused independent-target
    checks, both C++ contracts in the real MSVC/Ninja graph, real legacy
    exclusion across 3,777 paths/58 CMake files/1,242 compile commands/1,947
    Ninja edges, indexed provenance with 83 intentional differences and 43
    dependency inputs, all 55 local Markdown links, ADR/status assertions, and
    staged/unstaged diff checks pass.
  - Owner review: implementation-demo acceptance received in the 2026-08-26
    working session.
  - Follow-ups: none for Slice 3.3. Slice 3.4 is the next eligible slice; its
    deterministic facility interfaces and target ownership require owner
    approval before production APIs land.

- 2026-08-26 — Slice 3.4 — In Progress
  - Change: inspected ADR-0013's approved tick/randomness contracts, ADR-0014's
    target graph, the current engine-independent API, and later scheduler,
    fault, protocol-session, server-core, transport, and checksum gates. Added
    proposed [`ADR-0017`](adr/ADR-0017-deterministic-facilities-harness-boundaries.md)
    with five option sets, exact recommended RNG derivation, scenarios,
    acceptance tests, failure mitigations, and replacement triggers. No Slice
    3.4 production facility, link, trace harness, or runtime behavior was added.
  - Decisions: none accepted. The proposal recommends Option A for Decisions
    1–5: project-owned monotonic instants/interface; passive bounded server-core
    scheduler; numeric-keyed versioned server-core RNG streams; a bounded test-
    support-only duplex byte link; and exact trace bytes plus a non-canonical
    test diagnostic digest.
  - Verification: all 84 repository-owned Python tests pass, including the six
    focused independent-target tests and both existing C++ value contracts.
    Indexed provenance passes with 84 intentional differences and 43 dependency
    inputs; JSON/path ordering, all 57 local Markdown links, ADR-0017's proposed
    five-decision/owner-pending state, Slice 3.3/3.4 and Phase 3 status
    assertions, and staged/unstaged diff checks pass. A new product build/runtime
    test is not applicable because this step changes only the decision packet
    and production code remains gated.
  - Owner review: pending explicit approval or amendment of Decisions 1–5.
  - Follow-ups: present Decisions 1–5 to the owner, amend or accept ADR-0017,
    then implement only the approved deterministic facility and harness APIs.

- 2026-08-26 — Slice 3.4 — Implemented
  - Change: accepted ADR-0017 and added the project-owned monotonic nanosecond
    instant/clock seam, passive rational 30 Hz scheduler with four-tick bounded
    catch-up, numeric-keyed SplitMix64/xoshiro256** V1 streams with snapshot/
    restore and unbiased bounded sampling, checked manual clock, independently
    bounded FIFO byte duplex, stable typed test trace, and explicitly non-
    canonical `TestTraceDigestV1`. Added two independent C++ contract
    executables and a fail-closed production-to-test-support include check. No
    wall-clock adapter, thread, reducer, gameplay behavior, fault policy,
    production transport interface, or canonical checksum was added.
  - Decisions: the owner approved Option A for ADR-0017 Decisions 1–5 without
    amendment: project-owned monotonic observation, passive server-core
    scheduler, numeric-keyed versioned server-core RNG, test-support-only
    bounded link, and exact trace bytes plus test-only diagnostic digest.
  - Verification: `python -m unittest discover -s scripts/tests -v` passes all
    85 repository-owned tests, including seven focused target-boundary tests.
    The isolated MSVC 19.44/Ninja graph builds and runs all four C++ contract
    executables. The deterministic contracts prove exact 1/30-second rational
    boundaries and 18,000 sequential ticks over ten irregularly advanced
    minutes; four-tick stall batches; backwards-clock, deadline-overflow, and
    tick-exhaustion errors; pinned SplitMix64/xoshiro/derivation vectors;
    stream isolation, restore, rejection sampling, and zero-bound behavior;
    independent message/byte budgets, FIFO boundaries, directional close; and
    two byte-identical 198-byte scripted traces with pinned diagnostic digest
    `75802a50e6b6fc66`. The real MSVC/Ninja baseline graph configures, builds,
    and runs `tes3mp_protocol_tests_run`; real legacy exclusion passes across
    3,791 tracked paths, 58 CMake files, 1,249 compile commands, and 1,960 Ninja
    edges. Indexed provenance passes with 97 intentional differences and 43
    dependency inputs; JSON/path ordering and staged/unstaged diff checks pass.
  - Owner review: ADR approval and implementation-demo acceptance received in
    the 2026-08-26 working session. The accepted demo is conditional on the
    recorded gates passing without behavior or boundary deviations; they pass.
  - Follow-ups: none for Slice 3.4. Slice 3.5 is the next eligible slice and
    still owns latency, loss, jitter, duplication, reordering, stall, and
    disconnect fault controls.

- 2026-08-26 — Slice 3.5 — In Progress
  - Change: accepted
    [`ADR-0018`](adr/ADR-0018-deterministic-network-fault-controls.md) and added
    a passive test-support-only `FaultInjectingLink` wrapper, test-only numeric
    channel keys, immutable bounded profiles, isolated per-path/per-dimension
    deterministic random streams, scheduled latency/jitter/reorder delivery,
    integer-rate loss/duplication, explicit path stall/disconnect controls, and
    an independent C++ contract executable. The accepted FIFO byte duplex adds
    only a read-only budget observer so impossible messages reject before they
    can become stuck in the wrapper.
  - Decisions: the owner approved Option A for ADR-0018 Decisions 1–5. Faults
    wrap rather than modify the base-link semantics; delivery is passive and
    clock-driven; direction/channel identity is test-only; random decisions are
    seeded and isolated; and stalls, disconnects, duplication, pending queues,
    and underlying directional backpressure follow the approved bounded rules.
    No protocol channel, production transport, authority, state-scope, or
    gameplay behavior was selected.
  - Verification: `python -m unittest discover -s scripts/tests -v` passes all
    85 repository-owned tests, including seven focused target-boundary tests.
    The isolated and real MSVC 19.44/Ninja engine-independent graphs build and
    run all five C++ contract executables through
    `tes3mp_protocol_tests_run`. Fault contracts cover invalid/duplicate/65-path
    configuration, exact latency, direction/channel isolation, certain loss,
    certain at-most-one duplication with atomic admission, bounded jitter and
    reordered delivery, stall/resume, disconnect cleanup, independent reverse-
    direction progress under base-link backpressure, due-time overflow, and a
    pinned repeatable 97-byte seeded trace with diagnostic digest
    `935d71908b6496c8`; another path cannot perturb it and a changed seed changes
    it. `python scripts/verify_vnext_baseline.py --index` accounts for 101
    intentional differences and 43 dependency inputs. Fresh supported-preset
    configuration plus `python scripts/verify_vnext_legacy_exclusion.py
    --build-dir build/vnext-baseline --index` checks 3,795 staged paths, 58
    CMake files, 1,251 compile commands, and 1,965 Ninja edges. Manifest JSON,
    all 59 local vNext Markdown links, formatting, and staged diff checks pass.
  - Owner review: explicit Option A approval for Decisions 1–5 received in the
    2026-08-26 working session. Implementation-demo acceptance is pending.
  - Follow-ups: present the implementation demo and obtain owner acceptance
    before changing Slice 3.5 to **Implemented**. Slice 3.6 remains gated.

- 2026-08-26 — Slice 3.5 — Implemented
  - Change: retained implementation commit `3f572f27f3` and its accepted
    ADR-0018 fault-wrapper API and contracts without amendment; this status-only
    follow-up records completion after the required owner demo review.
  - Decisions: no amendment. The owner accepted the implemented Option A
    behavior for Decisions 1–5 as demonstrated.
  - Verification: the committed implementation passes all 85 repository-owned
    Python tests and all five C++ contracts. The owner-reviewed demo covers
    independent delayed/immediate paths, exact loss and duplication endpoints,
    bounded reordered delivery, path-local stall/resume and disconnect,
    reverse-direction backpressure isolation, checked overflow, and the pinned
    97-byte trace with digest `935d71908b6496c8`. Committed-tree baseline
    provenance passes with 101 intentional differences and 43 dependency
    inputs; legacy exclusion passes across 3,795 paths, 58 CMake files, 1,251
    compile commands, and 1,965 Ninja edges.
  - Owner review: implementation-demo acceptance received in the 2026-08-26
    working session.
  - Follow-ups: none for Slice 3.5. Slice 3.6 is the next eligible slice; Phase
    3 remains **In Progress** until Slices 3.6 and 3.7 and the exit gate pass.

- 2026-08-26 — Slice 3.6 — In Progress
  - Change: inspected the accepted platform/toolchain, schema/fuzzer,
    transport-sanitizer, target-boundary, and deterministic-harness policies;
    current CMake presets and vNext workflows; the five existing C++ contract
    executables; and the absence of reusable project-owned sanitizer, race, or
    fuzz plumbing. Added proposed
    [`ADR-0019`](adr/ADR-0019-runtime-safety-and-fuzz-ci-policy.md) with five
    option sets, scenarios, acceptance tests, failure mitigations, and review
    triggers. No production CMake option, sanitizer flag, runner, fuzz target,
    seed corpus, or workflow job was added.
  - Decisions: none accepted. The proposal recommends owned-target
    registration with a fast independent graph; separate Linux Clang 18
    ASan+UBSan and ThreadSanitizer profiles; one libFuzzer executable per
    bounded parser beginning with the test-only spatial decoder; two short
    per-change safety jobs; and repository-owned presets/runner/JSON evidence.
  - Verification: `python -m unittest discover -s scripts/tests -v` passes all
    85 repository-owned tests. Staged-tree
    `python scripts/verify_vnext_baseline.py --index` passes with 102
    intentional differences and 43 dependency inputs; JSON parsing and manifest
    path ordering, all 61 local vNext Markdown links, `git diff --cached
    --check`, and status/ADR assertions pass. Repository status/history and the
    accepted ADR plus build/workflow inspection identify Phase 3/Slice 3.6 as
    the only eligible work and confirm that no existing decision settles these
    five choices. A new product build or sanitizer run is not applicable
    because this step changes only the decision packet and production
    implementation remains gated.
  - Owner review: pending explicit approval or amendment of Decisions 1–5.
  - Follow-ups: present Decisions 1–5 to the owner, amend or accept ADR-0019,
    then implement only the approved Slice 3.6 safety plumbing and evidence.

- 2026-08-26 — Slice 3.6 — In Progress
  - Change: accepted ADR-0019 and added fail-closed target-scoped CMake
    instrumentation for every selected TES3MP library/executable, standalone
    uninstrumented/ASan+UBSan/ThreadSanitizer presets, a bounded libFuzzer entry
    over the test-only spatial decoder with three size-boundary seeds, and a
    repository-owned runner that verifies corpus bounds, all expected compiled
    sources, exact Clang 18 instrumentation, contract executables, and retained
    JSON/log/reproducer evidence. Added two dedicated 20-minute per-change
    Linux jobs and policy/failure tests. No OpenMW-wide flags, third-party
    instrumentation, production decoder, runtime suppression, thread,
    protocol behavior, authority, state scope, or gameplay behavior was added.
  - Decisions: the owner approved Option A for ADR-0019 Decisions 1–5 without
    amendment: owned-target registration with a fast independent graph;
    separate Linux Clang 18 ASan+UBSan and ThreadSanitizer profiles; one
    libFuzzer executable per bounded parser beginning with the test spatial
    decoder; dedicated bounded per-change jobs; and repository-owned presets,
    runner, and versioned JSON evidence.
  - Verification: `python -m unittest discover -s scripts/tests -v` passes all
    95 repository-owned tests, including 17 focused runtime-safety/target-
    boundary tests. The uninstrumented standalone preset builds with MSVC
    19.44/Ninja and runs all five C++ contracts; the real baseline graph
    configures, builds the adapter, and runs the same contracts. The approved
    sanitizer preset intentionally rejects local MSVC before generation.
    Staged legacy exclusion checks 3,805 paths, 59 CMake files, 1,251 compile
    commands, and 1,965 Ninja edges. Staged provenance passes with 111
    intentional differences and 43 dependency inputs; all three seed blobs
    retain exact binary sizes 101/111/2, all 61 local Markdown links resolve,
    and Python compilation, both JSON documents, manifest ordering, and
    `git diff --cached --check` pass. Hosted Linux Clang 18
    ASan+UBSan/libFuzzer and ThreadSanitizer execution is pending publication
    of this workflow change.
  - Owner review: explicit Option A approval for Decisions 1–5 received in the
    2026-08-26 working session. Implementation-demo acceptance is pending the
    hosted jobs and retained-artifact review.
  - Follow-ups: publish the implementation, require both hosted safety jobs,
    review their exact instrumentation/corpus/toolchain evidence and any
    reproducers, then present the implementation demo before changing Slice 3.6
    to **Implemented**. Slice 3.7 remains gated.

- 2026-08-26 — Slice 3.6 — In Progress
  - Change: retained implementation commit `1be40d1a4b` and its accepted
    ADR-0019 safety profiles, fuzz harness, runner, workflow, and contracts
    without amendment; this status-only follow-up records the owner-reviewed
    local implementation demo before publication.
  - Decisions: no amendment. The owner accepted the implemented Option A
    behavior for Decisions 1–5 as demonstrated.
  - Verification: the accepted demo covers the ordinary uninstrumented graph,
    all five instrumented target registrations, separate ASan+UBSan/fuzzer and
    ThreadSanitizer configurations, bounded 101/111/2-byte seed corpus, strict
    compiler/profile failures, complete compile-command instrumentation checks,
    and retained JSON/log/reproducer fields. The committed local evidence
    remains 95 Python tests, five C++ contracts in standalone and real baseline
    graphs, 3,805-path legacy exclusion, and provenance with 111 intentional
    differences and 43 dependency inputs. Hosted safety execution remains
    pending publication.
  - Owner review: implementation-demo acceptance received in the 2026-08-26
    working session.
  - Follow-ups: publish the commits, wait for both hosted safety jobs, review
    their retained artifacts, and record the results before changing Slice 3.6
    to **Implemented**. Slice 3.7 remains gated.

- 2026-08-27 — Slice 3.6 — In Progress
  - Change: reviewed the first hosted execution of commit `d21b66241f`. Both
    runtime-safety jobs and the Linux/macOS baseline jobs stopped while compiling
    `fault_injection_tests.cpp`: GCC 13, Clang 18 with libstdc++ 14, and Apple
    Clang with libc++ correctly rejected class-template argument deduction of a
    function type in the test table that MSVC had accepted. Made each test
    callable an explicit function pointer; production targets, instrumentation,
    fuzzing policy, and runtime behavior are unchanged.
  - Decisions: no ADR amendment or new architecture, authority, state-scope, or
    gameplay-behavior decision. This is a mechanical source-portability repair
    within the accepted ADR-0019 toolchain matrix.
  - Verification: failed runtime-safety run `33043774198` records ASan+UBSan
    artifact `9634839470` (digest
    `27738fafe1e5db094344ff3dcce072da1d069c63d4b1012ddd5bf9ab8b5b4f60`) and
    ThreadSanitizer artifact `9634839001` (digest
    `28470dfd8f5add3c2427eb167ccb2e238c52bf7bc9e074c16499224e4f04b0fb`);
    both failed during the common contract build before sanitizer execution.
    Linux baseline run `33043774128` and macOS ARM64 baseline run `33043774191`
    independently reproduce the same compiler diagnostic. Post-repair local
    verification passes all 95 repository-owned Python tests and all five C++
    contracts in both the standalone and real baseline graphs under MSVC
    19.44/Ninja. Staged provenance accounts for 111 intentional differences and
    43 dependency inputs; legacy exclusion checks 3,805 paths, 59 CMake files,
    1,251 compile commands, and 1,965 Ninja edges. Both relevant JSON documents,
    all 61 local vNext Markdown links, and `git diff --cached --check` pass.
    Replacement hosted verification is pending.
  - Owner review: the owner reported CI completion in the 2026-08-27 working
    session; the previously accepted implementation demo and Decisions 1–5 are
    unchanged.
  - Follow-ups: publish the repair, require replacement baseline and safety
    runs, and review retained safety artifacts before changing Slice 3.6 to
    **Implemented**. Slice 3.7 remains gated.

- 2026-08-27 — Slice 3.6 — Implemented
  - Change: retained accepted ADR-0019, implementation commit `1be40d1a4b`,
    demo-acceptance commit `d21b66241f`, and portability repair commit
    `fc8f178081` without amendment. This status update closes the slice after
    reviewing the replacement hosted execution and its retained artifacts.
  - Decisions: no amendment. The approved target scope, separate sanitizer
    profiles, per-parser fuzzer, per-change cadence, and evidence contract are
    satisfied. No production wire, threading, authority, state-scope, or
    gameplay behavior was introduced.
  - Verification: [runtime-safety run `33086841428`](https://github.com/poisson-fish/TES3MP/actions/runs/33086841428)
    passes both Clang 18.1.3 Linux x86-64 jobs. The ThreadSanitizer job runs all
    five contracts with 17 owned sources instrumented. The ASan+UBSan job runs
    the same contracts with 18 owned sources instrumented, then executes
    43,182,600 libFuzzer inputs in 31 seconds with the pinned 101/111/2-byte
    corpus, 256-byte input cap, 512 MiB RSS cap, and no sanitizer finding or
    retained reproducer. ASan+UBSan artifact `9652568259` has digest
    `705b3e86e9e93176bd254376879466635988297bdec8f605303abd65c58d3794`;
    ThreadSanitizer artifact `9652547046` has digest
    `6ed693d02d38823cb1de45aed98bcfcf7319214a1ecc266c6eaa980370e0d3a7`.
    Replacement Linux run `33086841250`, Windows run `33086841350`, and macOS
    run `33086841457` also pass the portability repair on the supported baseline
    matrix.
  - Owner review: implementation-demo acceptance was received in the 2026-08-26
    working session; the owner confirmed the replacement CI is green on
    2026-08-27.
  - Follow-ups: none for Slice 3.6. Slice 3.7 is the next eligible slice and its
    observability architecture requires owner approval before public APIs land.

- 2026-08-27 — Slice 3.7 — In Progress
  - Change: inspected the accepted target topology, deterministic-state policy,
    hostile-Internet/redaction requirements, future committed-change sinks, and
    transport/admin telemetry ownership. Added proposed
    [`ADR-0020`](adr/ADR-0020-owned-observability-interfaces-and-test-sinks.md)
    with five option sets, scenarios, acceptance tests, failure mitigations, and
    review triggers. No production header, sink, target dependency, metric/event
    category, backend, thread, queue, or behavior was added.
  - Decisions: none accepted. The proposal recommends Option A for Decisions
    1–5: server-core-owned ports plus test-support recorders; explicit injected
    non-blocking/noexcept sink attempts; closed integer metric operations and
    low-cardinality enum dimensions; closed typed structured events with no raw
    text/bytes; and fixed-capacity FIFO test recorders with reject-newest and
    explicit dropped-count evidence.
  - Verification: retained CI evidence establishes Slice 3.6 completion and the
    repository/decision inspection identifies Slice 3.7 as the only eligible
    implementation work. All 95 repository-owned Python tests pass; staged
    provenance accounts for 112 intentional differences and 43 dependency
    inputs; legacy exclusion checks 3,806 paths, 59 CMake files, 1,251 compile
    commands, and 1,965 Ninja edges; and JSON/path ordering, all 63 local vNext
    Markdown links, proposal/status assertions, and staged diff checks pass. A
    new product build is not applicable because production implementation
    remains approval-gated.
  - Owner review: pending explicit approval or amendment of Decisions 1–5.
  - Follow-ups: present Decisions 1–5 to the owner, accept or amend ADR-0020,
    then implement only the approved Slice 3.7 observability interfaces and test
    sinks. Phase 3 remains **In Progress** until Slice 3.7 and the exit gate pass.

- 2026-08-27 — Slice 3.7 — In Progress
  - Change: accepted ADR-0020 and added server-core-owned closed metric/event
    values, explicit metrics/event sink interfaces, explicit no-op sinks, and a
    non-owning observability bundle. Added test-support-only preallocated metric
    and event FIFO recorders with a 1,024-observation maximum, reject-newest
    overflow, saturating dropped counts, clear/reuse, and no allocation after
    construction. Added a sixth independent contract executable and registered
    every new source/contract in the ADR-0019 safety evidence. No production
    backend, exporter, formatting, wall clock, thread, queue, protocol field,
    canonical mutation, authority, state-scope, or gameplay behavior was added.
  - Decisions: the owner approved Option A for ADR-0020 Decisions 1–5 without
    amendment: server-core ownership with test-support recorders; explicit
    injected non-blocking/noexcept attempts; closed integer metric definitions
    and enum dimensions; closed typed events without raw strings/bytes; and
    fixed-capacity deterministic FIFO recorders with explicit overflow evidence.
  - Verification: the standalone MSVC 19.44/Ninja graph builds the new owned
    sources and runs all six C++ contracts. The observability contract covers
    metric operation/unit/value matching, invalid keys and dimensions,
    duplicate/over-limit dimensions, typed event payloads and semantic ticks,
    explicit no-op composition, zero/one/1,024/over-limit capacities, FIFO and
    reject-newest behavior, dropped counts, clear/reuse, identical-run evidence,
    and canonical-result independence. All 95 repository-owned Python tests
    pass. The real MSVC/Ninja baseline graph builds the same owned sources and
    runs all six contracts. Staged provenance accounts for 117 intentional
    differences and 43 dependency inputs; legacy exclusion checks 3,811 paths,
    59 CMake files, 1,254 compile commands, and 1,971 Ninja edges. Documentation
    JSON/path ordering, all 63 local vNext Markdown links, accepted-decision and
    status assertions, Python compilation, and staged diff checks pass. Hosted
    safety verification is pending.
  - Owner review: explicit Option A approval for Decisions 1–5 received in the
    2026-08-27 working session. Implementation-demo acceptance is pending.
  - Follow-ups: complete the full local verification set, publish the
    implementation, require both hosted safety jobs, review their updated six-
    contract/source evidence, and present the implementation demo before
    changing Slice 3.7 to **Implemented** or running the Phase 3 exit gate.

- 2026-08-27 — Slice 3.7 — In Progress
  - Change: retained implementation commit `57973b65c7` and its accepted
    ADR-0020 interfaces, bounded recorders, and contracts without amendment.
    This status-only update records owner demo acceptance and review of the
    hosted sanitizer/race/fuzz artifacts.
  - Decisions: no amendment. The owner accepted the implemented Option A
    behavior for Decisions 1–5 as demonstrated.
  - Verification: [runtime-safety run `33094255400`](https://github.com/poisson-fish/TES3MP/actions/runs/33094255400)
    passes both Linux Clang 18.1.3 jobs with all six contracts. ThreadSanitizer
    instruments 20 owned sources; ASan+UBSan instruments 21 including the
    fuzzer. The bounded 30-second fuzz smoke executes 31,921,040 inputs with the
    pinned 101/111/2-byte corpus and retains no finding or reproducer.
    ASan+UBSan artifact `9655707741` has digest
    `3c3a895577a668caff9e854fb87894863636c2afc24ce6930c31f08eebfe8a2c`;
    ThreadSanitizer artifact `9655699866` has digest
    `0df43fa638dc728adc4ae3ea06eb848d785f17fb424f65369f1229193f85d167`.
    Linux baseline run `33094255420`, Windows baseline run `33094255379`, and
    macOS baseline run `33094255438` remain in progress.
  - Owner review: implementation-demo acceptance received in the 2026-08-27
    working session.
  - Follow-ups: require the supported platform baseline matrix to pass, review
    its evidence, then change Slice 3.7 to **Implemented** and present the
    complete Phase 3 exit-gate evidence for separate owner approval.

- 2026-08-27 — Slice 3.7 — Implemented
  - Change: retained accepted ADR-0020 and implementation commit `57973b65c7`
    without behavioral amendment. Closed the slice after the supported Phase 3
    platform matrix completed on the demo-accepted tree.
  - Decisions: none. The owner had already approved ADR-0020 Option A for all
    five decisions and accepted the implementation demo.
  - Verification: all six latest vNext workflows completed successfully for
    `e757e063c461fa6a676f477927633416c9a0996b`: runtime safety
    [`33095908384`](https://github.com/poisson-fish/TES3MP/actions/runs/33095908384),
    FlatBuffers proof
    [`33095908412`](https://github.com/poisson-fish/TES3MP/actions/runs/33095908412),
    macOS baseline
    [`33095908403`](https://github.com/poisson-fish/TES3MP/actions/runs/33095908403),
    GameNetworkingSockets proof
    [`33095908292`](https://github.com/poisson-fish/TES3MP/actions/runs/33095908292),
    Windows baseline
    [`33095908387`](https://github.com/poisson-fish/TES3MP/actions/runs/33095908387),
    and Linux baseline
    [`33095908319`](https://github.com/poisson-fish/TES3MP/actions/runs/33095908319).
    Linux GCC 13/Clang 18, Windows MSVC 2022 v143, macOS arm64 Xcode 16, all
    four non-Intel jobs in each dependency proof, and both Clang 18 safety jobs
    passed. The Phase 3 push policy intentionally skipped Intel; the manually
    dispatched Intel baseline and five-platform dependency proofs retained from
    Phases 1 and 2 satisfy its declared cadence. Current safety artifacts are
    `9656379611` (ASan+UBSan/fuzz, SHA-256
    `6910645bb146cec2ccc4299510a8300b0795d9db6c4f84cfa61d52450aab6cc7`)
    and `9656367967` (ThreadSanitizer, SHA-256
    `f1f5881cc07cfb3437a09360279da008b20a55f0732e67f22a34c0be41980921`).
  - Owner review: implementation-demo acceptance received in the 2026-08-27
    working session.
  - Follow-ups: none for Slice 3.7. Phase 3 exit remains a separate owner gate.

- 2026-08-27 — Phase 3 — In Progress
  - Change: assembled the exit-gate evidence and implemented the owner-approved
    Phase 4+ CI cadence amendment in ADR-0002 and ADR-0019. All six vNext hosted
    workflows now expose manual dispatch only. A phase-exit dispatch exercises
    every declared job, including macOS x86-64; slice work continues to require
    applicable local verification.
  - Decisions: the owner explicitly approved one complete hosted CI gate at the
    end of each Phase 4+ phase instead of automatic push, pull-request,
    schedule, or release runs. No architecture, authority, state-scope, or
    gameplay behavior changed.
  - Gate evidence: protocol and server-core build and test in the standalone
    graph without OpenMW or GameNetworkingSockets; fail-closed tests reject an
    introduced OpenMW/transport include or undeclared dependency; and the
    deterministic harness reproduces exact normal and injected-fault traces and
    diagnostic checksums from identical seeds. The local six-contract graph,
    real baseline graph, legacy-exclusion gate, provenance gate, repository
    tests, and the hosted evidence listed above provide the retained proof.
  - Verification: all 95 repository-owned Python tests pass, including workflow
    policy assertions that all six vNext workflows are manual-only and that the
    Intel jobs are unconditional within a dispatch. Staged provenance accounts
    for 117 intentional differences and verifies 43 dependency inputs. Legacy
    exclusion checks 3,811 tracked paths, 59 CMake files, 1,254 compile
    commands, and 1,971 Ninja edges. The provenance JSON parses, all 63 local
    vNext Markdown links resolve, changed Python compiles, and staged diff
    checks pass.
  - Owner review: explicit Phase 3 exit approval remains pending. Phase 4 is
    still **Not Started**, and no Phase 4 production work is eligible yet.
  - Follow-ups: complete local verification of this cadence/status update,
    publish it without launching automatic vNext workflows, and present the
    Phase 3 exit gate for owner approval.

- 2026-08-27 — Phase 3 — Implemented
  - Change: marked Phase 3 implemented after explicit exit approval and replaced
    the README's historical slice-by-slice status narrative with a concise
    current-state, product-scope, boundary, invariant, and workflow guide. No
    production behavior or CI policy changed in this closeout.
  - Decisions: no ADR or GDR amendment. The owner explicitly approved the Phase
    3 exit and progression toward Phase 4 in the 2026-08-27 working session.
  - Gate evidence: every Slice 3.1–3.7 deliverable is implemented and demo-
    accepted. The independent target graph, fail-closed dependency checks,
    deterministic normal/fault traces, six contract executables, local and
    hosted safety profiles, supported Phase 3 platform matrix, provenance, and
    legacy exclusion satisfy all three exit conditions.
  - Verification: all 95 repository-owned Python tests pass. Staged provenance
    accounts for 117 intentional differences and verifies 43 dependency inputs;
    legacy exclusion checks 3,811 paths, 59 CMake files, 1,254 compile commands,
    and 1,971 Ninja edges. All 63 local vNext Markdown links resolve, changed
    Python compiles, and staged diff checks pass. The README is reduced from 265
    to 144 lines.
  - Owner review: Phase 3 exit approval received in the 2026-08-27 working
    session. Phase 4 production remains subject to its kickoff and unresolved
    protocol/session architecture decisions.
  - Follow-ups: verify and publish this documentation-only closeout without an
    automatic hosted run, then present the Phase 4 kickoff and Slice 4.1
    decision options before production implementation begins.

## Phase 4 — Bounded protocol and in-memory session

[Back to the phase tracker](IMPLEMENTATION_PLAN.md#phase-4--bounded-protocol-and-in-memory-session)

- Decode into validated temporary values; only deliver a message to a session or
  reducer after the entire envelope and payload pass bounds and semantic checks.
- Capability negotiation should produce an immutable negotiated-capability set
  used by encoders and handlers for the lifetime of a connection.
- Keep authentication claims opaque to protocol code. The session may consume a
  validated principal/session result but must not know backend credential format.
- Golden files cover vNext evolution only and must include an explanation when
  intentionally updated.

- 2026-08-27 — Slice 4.1 — In Progress
  - Change: this implementation commit accepts
    [`ADR-0021`](adr/ADR-0021-bounded-protocol-framing-and-decode-boundary.md)
    and adds the production `T3MP` format-one frame encoder/decoder, closed
    message classes and initial stable kind registry, fixed 4/16/64 KiB class
    budgets, owned decoded payloads, enum/numeric errors, a seventh standalone
    contract executable, and a bounded production-decoder fuzz target/corpus.
    No FlatBuffers payload schema, session transition, transport behavior,
    authority, state scope, or gameplay behavior is introduced.
  - Decisions: the owner explicitly approved ADR-0021 Option A for Decisions
    1–4 and the Phase 4 slice boundaries in the 2026-08-27 working session.
    The implementation uses the approved 12-byte little-endian header, compiled
    class/kind mapping, fixed class budgets, and allocation-after-complete-frame-
    validation owned result boundary without amendment.
  - Verification: from the MSVC 2022 v143 x86-64 developer environment,
    `cmake --preset tes3mp-standalone --fresh` and `cmake --build --preset
    tes3mp-standalone --parallel 4` build and pass all seven contract
    executables. `python -m unittest discover -s scripts/tests -v` passes all
    95 repository-owned tests. `python scripts/verify_vnext_baseline.py --index`
    accounts for 124 intentional differences and verifies 43 dependency inputs.
    `python scripts/verify_vnext_legacy_exclusion.py --index --build-dir
    build/tes3mp-standalone` checks 3,818 tracked paths, 59 CMake files, 22
    compile commands, and 69 Ninja edges. Changed Python compiles, JSON parses,
    local vNext Markdown links resolve, `git diff --cached --check` passes, and
    the public protocol header contains no OpenMW, transport-library, platform,
    FlatBuffers-generated, or test-support include.
  - Owner review: Phase 4 kickoff and architecture approval received. The Slice
    4.1 implementation demo and acceptance remain pending, so the slice stays
    **In Progress**.
  - Follow-ups: present the golden frame, owned-buffer overwrite, structured
    malformed-input failures, exact class-limit cases, and fuzz registration for
    owner demo acceptance; then mark Slice 4.1 **Implemented**. Slice 4.2 still
    requires its capability/version-negotiation decision packet before schemas
    or behavior land.

- 2026-08-27 — Slice 4.1 — Implemented
  - Change: retained implementation commit `98e62f5f19` without code changes and
    marked Slice 4.1 **Implemented** after the required owner demo acceptance.
  - Decisions: none. ADR-0021 remains accepted without amendment; the owner
    directed that Slice 4.2 must not begin in this session.
  - Verification: the accepted demo and implementation retain the green seven-
    contract MSVC build, 95 repository-owned Python tests, provenance evidence
    for 124 intentional differences and 43 dependency inputs, legacy-exclusion
    evidence for 3,818 paths, 59 CMake files, 22 compile commands, and 69 Ninja
    edges, changed-Python compilation, JSON parsing, 64 resolved local vNext
    Markdown links, and clean diff checks.
  - Owner review: implementation-demo evidence explicitly accepted by the
    project owner in the 2026-08-27 working session.
  - Follow-ups: none for Slice 4.1. Phase 4 remains **In Progress**; Slice 4.2
    stays **Not Started** and requires its kickoff decision packet in a new
    session before production schemas or behavior begin.

- 2026-08-27 — Slice 4.2 — In Progress
  - Change: accepted
    [`ADR-0022`](adr/ADR-0022-version-and-capability-negotiation.md) and added
    three size-prefixed/file-identified FlatBuffer schemas, pinned generated
    C++, owned hello/rejection values, bounded capability offers, deterministic
    pure negotiation, initial-peer classification, an eighth independent
    contract, generated-drift enforcement, and a dedicated decoder fuzz target.
    No authentication, session transition, transport behavior, release-version
    constant, gameplay capability, authority, state scope, or OpenMW behavior
    was added.
  - Decisions: the owner approved Option A for ADR-0022 Decisions 1–5 on
    2026-08-27. Payloads use `T3CH`/`T3SH`/`T3RJ`; negotiation selects the
    highest overlapping minor and checks requirements in both directions;
    capability IDs are nonzero stable `u32` values with separate 32-entry
    sorted/disjoint optional and required sets; valid vNext incompatibility uses
    typed numeric rejection while non-vNext/TES3MP 0.8 input receives no wire
    response; and decoding/negotiation remain separate from Slice 4.3 state.
  - Verification: from the Visual Studio 2022 v17.14.39/MSVC 19.44.35228
    x86-64 environment, a fresh standalone configure/build and
    `tes3mp_protocol_tests_run` pass all eight C++ contracts. A separate fresh
    configure acquires the SHA-256-pinned FlatBuffers source and passes the same
    target. The pinned FlatBuffers proof runner reproduces all three production
    generated headers exactly and passes its isolated proof. All 97 repository-
    owned Python tests pass. Indexed baseline provenance accounts for 137
    intentional differences and verifies 50 dependency inputs; indexed legacy
    exclusion checks 3,831 tracked paths, 59 CMake files, 24 compile commands,
    and 74 Ninja edges. Both changed JSON files parse, all 67 local vNext
    Markdown links resolve, changed Python compiles, the public header remains
    isolated from FlatBuffers/generated, OpenMW, transport, platform, and test-
    support headers, and `git diff --cached --check` passes.
  - Owner review: architecture approval received for Option A on 2026-08-27.
    Implementation-demo acceptance remains pending, so Slice 4.2 stays **In
    Progress**.
  - Follow-ups: present the three golden payloads, current/previous-minor
    selection, optional/required capability cases, typed rejection, malformed
    input, owned-buffer, and legacy/no-reply evidence for owner acceptance.
    Slice 4.3 remains gated until Slice 4.2 is **Implemented**.

- 2026-08-27 — Slice 4.2 — Implemented
  - Change: retained implementation commit `e89621e970` without code changes and
    marked Slice 4.2 **Implemented** after the required owner demo acceptance.
  - Decisions: none. ADR-0022 remains accepted without amendment.
  - Verification: the accepted demo and implementation retain the green eight-
    contract MSVC builds, exact pinned FlatBuffers regeneration/proof, 97
    repository-owned Python tests, provenance evidence for 137 intentional
    differences and 50 dependency inputs, legacy-exclusion evidence for 3,831
    paths, 59 CMake files, 24 compile commands, and 74 Ninja edges, JSON and
    Python compilation, 67 resolved local vNext Markdown links, public-header
    isolation, and clean diff checks.
  - Owner review: implementation-demo evidence explicitly accepted by the
    project owner in the 2026-08-27 working session, with direction to begin
    Slice 4.3.
  - Follow-ups: none for Slice 4.2. Phase 4 remains **In Progress**.

- 2026-08-27 — Slice 4.3 — In Progress
  - Change: added proposed
    [`ADR-0023`](adr/ADR-0023-session-state-machines-and-authentication-provider-boundary.md)
    with role/state placement, Phase 4/6 wire boundary, asynchronous provider,
    opaque material/result, deadline/cancellation, and structured-observability
    options. No production session, authentication, protocol, authority, state-
    scope, or gameplay behavior changed.
  - Decisions: the project owner approved Option A for ADR-0023 Decisions 1–6
    on 2026-08-27 without amendment.
  - Verification: all 97 repository-owned Python tests pass. Indexed baseline
    provenance accounts for 138 intentional differences and verifies 50
    dependency inputs; indexed legacy exclusion checks 3,832 tracked paths, 59
    CMake files, 24 compile commands, and 74 Ninja edges. The changed JSON
    parses, all 69 local vNext Markdown links resolve, and staged diff checks
    pass. This documentation-only kickoff does not change the retained green
    Slice 4.2 C++ or FlatBuffers evidence.
  - Owner review: Slice 4.2 completion, Slice 4.3 kickoff, and the recommended
    session/authentication architecture were explicitly approved.
  - Follow-ups: implement the approved boundary and present its automated/demo
    evidence before marking Slice 4.3 **Implemented**.

- 2026-08-27 — Slice 4.3 — In Progress
  - Change: implemented the accepted ADR-0023 role-specific client/server
    machines in the existing targets, the move-only 0–256-byte authentication
    material, pollable session-owned provider operation, minimal `PrincipalId`
    result, three-stage injected timeout policy, stale attempt/generation
    rejection, idempotent cancellation/terminal handling, and closed typed
    session observability. Added the ninth standalone contract and registered
    every new production/test source with runtime-safety enforcement, while
    closing the existing handshake-fuzzer ASan preset omission. No
    authentication wire kind/schema, real provider, resume behavior, release
    timeout default, player identity, authority, durable state, OpenMW, or
    gameplay behavior was added.
  - Decisions: implemented the project owner's approved Option A for ADR-0023
    Decisions 1–6 without amendment. No additional architecture, authority,
    state-scope, or gameplay-behavior decision arose during implementation.
  - Verification: from the Visual Studio 2022 v17.14.39/MSVC 19.44.35228
    x86-64 environment, the standalone `tes3mp_protocol_tests_run` target builds
    and passes all nine C++ contracts. The session contract covers exhaustive
    63-cell server and 81-cell client state/event matrices, immediate/delayed
    success, denial/malformed/unavailable outcomes, empty/exact-256/257-byte
    bounds, secret-canary ownership, every timeout stage at deadline-minus-one
    and exact deadline, checked deadline overflow, wrong attempt/generation,
    duplicate operation, cancellation/destruction/close, non-resurrection,
    typed observations, and deterministic replay. All 97 repository-owned
    Python tests pass; the pinned FlatBuffers regeneration and isolated proof
    pass; indexed provenance accounts for 146 intentional differences and 50
    dependency inputs; indexed legacy exclusion checks 3,840 tracked paths, 59
    CMake files, 28 compile commands, and 81 Ninja edges. Changed Python
    compiles, both changed JSON files parse, all 69 local vNext Markdown links
    resolve, public-header/observability redaction scans find no forbidden
    engine/transport/generated/test-support or secret-bearing surface, and
    staged/worktree diff checks pass.
  - Owner review: architecture approval is complete. Implementation-demo
    acceptance remains pending, so Slice 4.3 stays **In Progress**.
  - Follow-ups: present the successful/delayed/rejected provider paths, exact
    deadline and secret bounds, stale/cancellation behavior, atomic matrix, and
    redaction evidence for owner acceptance. Slice 4.4 remains gated until this
    slice is marked **Implemented**.

- 2026-08-27 — Slice 4.3 — Implemented
  - Change: retained implementation commit `edbedbd632` without production-code
    changes and marked Slice 4.3 **Implemented** after the required owner demo
    acceptance.
  - Decisions: none. ADR-0023 remains accepted without amendment.
  - Verification: the accepted implementation retains the green nine-contract
    MSVC build, exhaustive server/client state-event matrices, provider/secret/
    timeout/lifetime/redaction coverage, 97 repository-owned Python tests,
    exact pinned FlatBuffers proof, provenance evidence for 146 intentional
    differences and 50 dependency inputs, legacy-exclusion evidence for 3,840
    tracked paths, 59 CMake files, 28 compile commands, and 81 Ninja edges,
    JSON and Python validation, 69 resolved local Markdown links, public-header
    isolation, and clean diff checks.
  - Owner review: implementation-demo evidence explicitly accepted by the
    project owner in the 2026-08-27 working session.
  - Follow-ups: none for Slice 4.3. Phase 4 remains **In Progress**; Slice 4.4
    stays **Not Started** and requires its architecture/protocol decision packet
    before production implementation begins.

- 2026-08-27 — Slice 4.4 — In Progress
  - Change: inspected the accepted protocol, deterministic-ordering,
    identity/counter, canonical primitive, authority, transport, framing,
    negotiation, and session contracts and added proposed
    [`ADR-0024`](adr/ADR-0024-reliable-operation-latest-wins-envelope-contract.md)
    with five option sets, scenarios, acceptance tests, consequences, failure
    mitigations, and review triggers. No production envelope header, schema,
    codec, message kind, session behavior, authority, state scope, or gameplay
    behavior was added.
  - Decisions: none accepted. The proposal recommends separate directional
    typed compositions; one reliable command header plus optional entity
    precondition and one later typed body; snapshot session/generation/tick plus
    optional contiguous-finalized command acknowledgement; layered validation;
    and deferring complete `T3RO`/`T3LS` roots until Slice 4.5 supplies reviewed
    concrete bodies.
  - Verification: all 97 repository-owned Python tests pass. Indexed baseline
    provenance accounts for 147 intentional differences and verifies 50
    dependency inputs; indexed legacy exclusion checks 3,841 tracked paths, 59
    CMake files, 28 compile commands, and 81 Ninja edges. The provenance JSON
    parses, all 71 local vNext Markdown links resolve, and staged diff checks
    pass. A new product build is not applicable because production
    implementation remains approval-gated.
  - Owner review: pending explicit approval or amendment of Decisions 1–5.
  - Follow-ups: present Decisions 1–5 to the owner, accept or amend ADR-0024,
    then implement only the approved Slice 4.4 envelope-header surface and
    independent contract tests. Slice 4.5 remains gated.

- 2026-08-27 — Slice 4.4 — In Progress
  - Change: accepted
    [`ADR-0024`](adr/ADR-0024-reliable-operation-latest-wins-envelope-contract.md)
    and added protocol-owned `ReliableOperationHeader` and
    `LatestWinsSnapshotHeader` values. The reliable header owns one existing
    client command header plus an explicit optional entity precondition. The
    snapshot header binds server tick and optional contiguous-finalized command
    progress to a target session and generation. Added a tenth standalone
    contract and registered it with target-boundary and runtime-safety policy.
    No wire root, schema, decoder, generic payload, batch, snapshot sequence,
    writer admission, authority, state scope, or gameplay behavior was added.
  - Decisions: the owner approved Option A for ADR-0024 Decisions 1–5 on
    2026-08-27 without amendment. Complete typed `T3RO`/`T3LS` roots remain
    gated on Slice 4.5's separately reviewed command/snapshot bodies and bounds.
  - Verification: from the Visual Studio 2022 v17.14.39/MSVC 19.44.35228
    x86-64 environment, a fresh standalone configure/build runs all ten C++
    contracts successfully. The envelope contract covers required command
    metadata, optional preconditions, one-operation atomicity, absence of
    writer/canonical/generic payload fields, target session/generation/tick,
    absent and present acknowledgements, tick recency without a new sequence,
    entry-scoped revision/epoch, public-header isolation, and deterministic
    equality. All 97 repository-owned Python tests pass, and exact pinned
    FlatBuffers regeneration plus the isolated proof pass without schema drift.
    Indexed provenance accounts for 149 intentional differences and verifies
    50 dependency inputs; indexed legacy exclusion checks 3,843 tracked paths,
    59 CMake files, 29 compile commands, and 85 Ninja edges. Both changed C++
    files pass `clang-format --dry-run --Werror`; the provenance JSON parses,
    all 72 local vNext Markdown links resolve, the new public header contains
    no generated/FlatBuffers, OpenMW, transport-library, test-support, generic
    byte/string/map include or surface, and staged diff checks pass.
  - Owner review: architecture approval is complete. Implementation-demo
    acceptance remains pending, so Slice 4.4 stays **In Progress**.
  - Follow-ups: present the reliable header with absent/present precondition,
    command ID/sequence separation, writer-field exclusion, snapshot
    acknowledgement absence/progress, tick recency, and public-header isolation
    for owner acceptance. Slice 4.5 remains gated until this slice is
    **Implemented**.

- 2026-08-27 — Slice 4.4 — Implemented
  - Change: retained implementation commit `ee17ebdd0a` without production-code
    changes and marked Slice 4.4 **Implemented** after the required owner demo
    acceptance.
  - Decisions: none. ADR-0024 remains accepted without amendment.
  - Verification: the accepted implementation retains a green fresh MSVC build
    of all ten C++ contract executables, 97 Python tests, exact FlatBuffers
    regeneration/proof, provenance checks with 149 intentional differences and
    50 dependency inputs, legacy-exclusion checks covering 3,843 tracked paths,
    59 CMake files, 29 compile commands, and 85 Ninja edges, plus formatting,
    JSON, 72 local-link, public-header-isolation, and staged-diff checks.
  - Owner review: implementation-demo evidence explicitly accepted by the
    project owner in the 2026-08-27 working session.
  - Follow-ups: none for Slice 4.4. Phase 4 remains **In Progress**; Slice 4.5
    stays **Not Started** and requires a separate behavior/protocol decision
    packet before typed bodies, schemas, or exchange behavior land.

- 2026-08-27 — Slice 4.5 — In Progress
  - Change: inspected the implemented Phase 4 framing, negotiation, session,
    envelope, strong-identity, spatial-primitive, in-memory-link, target-policy,
    and test boundaries. Added proposed
    [`ADR-0025`](adr/ADR-0025-minimal-player-command-world-snapshot-exchange.md)
    with five architecture option sets and proposed
    [`GDR-0011`](gdr/GDR-0011-phase4-minimal-player-exchange-semantics.md) with
    four separately reviewable gameplay/authority/state-scope option sets. No
    production schema, generated source, codec, session state, fake peer,
    authority, canonical mutation, or gameplay behavior changed.
  - Decisions: the project owner approved Option A for ADR-0025 Decisions 1–5
    and GDR-0011 Decisions 1–4 on 2026-08-27 without amendment. The accepted
    design is closed typed `T3RO`/`T3LS`
    body unions; one velocity-only player motion intent with a required entity
    precondition; a deterministically ordered, target-session selected spatial
    view capped at 256 entries; pure codecs plus role-specific established-
    session/timeline guards; atomic confirmed client state; and a synchronous
    test-support fake peer composed from the existing framed hello, typed
    authentication, and in-memory-link boundaries.
  - Verification: all 97 repository-owned Python tests pass. Indexed baseline
    provenance accounts for 151 intentional differences and verifies 50
    dependency inputs. Indexed legacy exclusion checks 3,845 tracked paths, 59
    CMake files, 29 compile commands, and 85 Ninja edges. The provenance JSON
    parses, all 80 local vNext Markdown links resolve, both proposed records
    have the required decision/options, consequences, failure, review-trigger,
    acceptance-test, and pending-owner sections, only `docs/vnext` paths are
    staged, and staged diff checks pass. A product build is not applicable
    because this approval-gated kickoff changes documentation only.
  - Owner review: explicit, independent approval received for both records.
    Implementation-demo acceptance remains pending, so Slice 4.5 stays **In
    Progress**.
  - Follow-ups: implement only the approved schemas, codecs, session guards,
    and fake-peer exchange, then present the named behavior and failure evidence
    for owner acceptance. Slice 4.6 remains gated.

- 2026-08-27 — Slice 4.5 — In Progress
  - Change: this implementation commit adds the accepted closed-union `T3RO`
    velocity-intent and `T3LS` spatial-view schemas, exact pinned generated C++,
    verifier-first owned codecs, required command precondition, strictly entity-
    ordered 0–256-entry views, explicit post-authentication session binding,
    role-specific context/timeline guards, atomic client confirmed-state commit,
    and a reusable synchronous fake peer over the existing framed hello, typed
    authentication, and in-memory link. It adds an eleventh contract executable
    and registers both production decoders with a bounded ASan/libFuzzer target
    and corpus. It does not add a reducer, canonical mutation, prediction,
    rendering, transport library, socket, OpenMW dependency, or broad gameplay
    command.
  - Decisions: implemented the owner's approved ADR-0025 Option A Decisions
    1–5 and GDR-0011 Option A Decisions 1–4 without amendment. The server guard
    validates session/generation but remains the sole future mutation boundary;
    the client stores only a complete confirmed snapshot, and snapshot selection
    remains target-session specific.
  - Verification: from the Visual Studio 2022 v17.14.39/MSVC 19.44.35228
    x86-64 environment, `cmake --preset tes3mp-standalone --fresh` and `cmake
    --build --preset tes3mp-standalone --parallel 4` build and pass all eleven
    C++ contracts. The exchange contract covers exact `T3RO`/`T3LS` golden
    bytes, owned-buffer lifetime, empty/exact-256/257-entry bounds, strict order,
    truncation and identifier/mutation failures, atomic stale/duplicate/
    contradictory/acknowledgement guards, old-generation rejection, differing
    same-tick per-session views, and the complete fake-peer trace. The pinned
    FlatBuffers proof regenerates all five production headers exactly and its
    isolated test passes. All 97 repository-owned Python tests pass. Indexed
    provenance accounts for 164 intentional differences and verifies 54
    dependency inputs; indexed legacy exclusion checks 3,858 tracked paths, 59
    CMake files, 32 compile commands, and 91 Ninja edges. All changed C++ passes
    `clang-format --dry-run --Werror`; changed Python compiles, JSON parses,
    public-header and reliable-command-surface isolation scans pass, local vNext
    Markdown links resolve, and staged diff checks pass. The sanitizer-backed
    fuzzer is registered for the Linux phase-exit profile and is not locally
    executable in this MSVC environment.
  - Owner review: architecture, authority, state-scope, and gameplay-behavior
    approval is complete. Implementation-demo acceptance remains pending, so
    Slice 4.5 stays **In Progress**.
  - Follow-ups: present the golden/ownership/bounds/failure/session-isolation/
    end-to-end evidence for owner acceptance, then mark Slice 4.5
    **Implemented**. Slice 4.6 remains gated and owns exhaustive property,
    mutation, corpus, and fuzz expansion.

- 2026-08-27 — Slice 4.5 — Implemented
  - Change: retained implementation commit `077a08da48` without production-code
    changes and marked Slice 4.5 **Implemented** after the required owner demo
    acceptance.
  - Decisions: none. ADR-0025 and GDR-0011 remain accepted without amendment.
  - Verification: the accepted implementation retains the green eleven-contract
    fresh MSVC build, exact five-header FlatBuffers regeneration/proof, all 97
    repository-owned Python tests, provenance evidence for 164 intentional
    differences and 54 dependency inputs, legacy-exclusion evidence for 3,858
    tracked paths, 59 CMake files, 32 compile commands, and 91 Ninja edges,
    formatting, JSON/Python validation, 80 resolved local Markdown links,
    public-surface isolation, and clean diff checks.
  - Owner review: implementation-demo evidence explicitly accepted by the
    project owner in the 2026-08-27 working session.
  - Follow-ups: none for Slice 4.5. Phase 4 remains **In Progress**; Slice 4.6
    is the next eligible slice and stays **Not Started** until its work begins.

- 2026-08-27 — Slice 4.6 — In Progress
  - Change: this implementation commit expands all production Phase 4 codec
    contracts with deterministic
    scalar, collection, and payload-boundary properties plus exhaustive
    single-bit mutation checks that either reject or normalize through a second
    owned round trip. Added reproducible valid golden seeds for the frame,
    `T3CH`, `T3SH`, `T3RJ`, `T3RO`, and `T3LS` decoders; each contract can
    deliberately regenerate those files and the normal CMake contract target
    verifies their exact current encoding and successful decode. Added
    decode/encode/decode metamorphic assertions to all three production fuzzer
    groups and a fail-closed seven-decoder registry that checks source
    registration, corpus presence, SHA-256-pinned production seeds, target
    completeness, and retained evidence. The safety inventory now also includes
    the previously built but omitted Slice 4.4 envelope contract. No production
    schema, codec, protocol value, session behavior, authority, state scope, or
    gameplay behavior changed.
  - Decisions: none. This testing-only implementation follows the already
    accepted ADR-0004 verifier/fuzz/evolution requirements and ADR-0019
    sanitizer/fuzz policy without selecting a new architecture or behavior.
  - Verification: from the Visual Studio 2022 v17.14.39/MSVC 19.44.35228
    x86-64 environment, `cmake --preset tes3mp-standalone --fresh` and `cmake
    --build --preset tes3mp-standalone --parallel 4` build and pass all eleven
    contract executables plus three checked-in golden-corpus verification runs.
    All three changed production fuzzer translation units compile independently
    with MSVC C++20. `python -m unittest discover -s scripts/tests -v` passes
    all 98 repository-owned tests. `python scripts/run_vnext_flatbuffers_proof.py`
    reproduces all five production generated headers exactly and passes the
    isolated proof. `python scripts/verify_vnext_baseline.py --index` accounts
    for 170 intentional differences and verifies 54 dependency inputs. `python
    scripts/verify_vnext_legacy_exclusion.py --index --build-dir
    build/tes3mp-standalone` checks 3,864 tracked paths, 59 CMake files, 32
    compile commands, and 91 Ninja edges. Changed C++ passes
    `clang-format --dry-run --Werror`; changed Python compiles; JSON parses and
    retains sorted provenance paths; all 80 local vNext Markdown links resolve;
    and staged diff checks pass. The Linux Clang 18 ASan/UBSan/libFuzzer and
    ThreadSanitizer executions remain the manual Phase 4 exit gate rather than
    a Windows-local slice requirement.
  - Owner review: no new architecture, authority, state-scope, or gameplay
    decision is introduced. Implementation-demo acceptance remains pending, so
    Slice 4.6 and Phase 4 stay **In Progress**.
  - Follow-ups: present the boundary-property, exhaustive mutation,
    regenerated/verified golden-corpus, decoder-registry, and metamorphic-fuzz
    evidence for owner acceptance. After Slice 4.6 acceptance, manually dispatch
    and review every Phase 4 hosted exit-gate job, including macOS x86-64,
    before marking Phase 4 **Implemented** or beginning Phase 5.

- 2026-08-27 — Slice 4.6 — Implemented
  - Change: retained implementation commit `7361a43af7` without production-code
    changes and marked Slice 4.6 **Implemented** after the required owner demo
    acceptance.
  - Decisions: none. ADR-0004 and ADR-0019 remain accepted without amendment;
    the slice introduced no architecture, authority, state-scope, or gameplay
    behavior decision.
  - Verification: the accepted implementation retains the green fresh MSVC
    build of all eleven contract executables plus three golden-corpus checks,
    independent MSVC C++20 compilation of all three changed production fuzzer
    translation units, 98 repository-owned Python tests, exact five-header
    FlatBuffers regeneration/proof, provenance evidence for 170 intentional
    differences and 54 dependency inputs, legacy-exclusion evidence for 3,864
    paths, 59 CMake files, 32 compile commands, and 91 Ninja edges, formatting,
    JSON/Python validation, 80 resolved local Markdown links, and clean diff
    checks.
  - Owner review: implementation-demo evidence explicitly accepted by the
    project owner in the 2026-08-27 working session.
  - Follow-ups: all Phase 4 slices are now **Implemented**, but Phase 4 remains
    **In Progress**. Manually dispatch and review the complete Phase 4 hosted
    baseline, dependency-proof, and runtime-safety matrix, including macOS
    x86-64, then obtain explicit Phase 4 exit-gate approval before marking the
    phase **Implemented** or beginning Phase 5.

- 2026-08-27 — Phase 4 exit gate — In Progress
  - Change: pushed phase-completion candidate `035a8932d5` and manually
    dispatched all six required hosted workflows. Runtime-safety run
    [33137988875](https://github.com/poisson-fish/TES3MP/actions/runs/33137988875)
    passed its Clang 18 ASan/UBSan/libFuzzer job but failed its ThreadSanitizer
    job while linking the protocol-frame contract: that test's global
    allocation counters collided with the TSan runtime allocator interceptors.
    The narrow correction disables only those test-owned global allocator
    replacements in TSan builds, retains the structured fail-closed decode
    assertion there, and retains the zero-allocation assertion in ordinary and
    ASan/UBSan builds. It also avoids a Clang warning from shifting a one-byte
    unsigned value by eight bits in test support. No production schema, codec,
    protocol value, session behavior, authority, state scope, or gameplay
    behavior changed.
  - Decisions: none. This is a mechanical hosted-test compatibility correction
    under the accepted ADR-0019 sanitizer policy; it does not amend an ADR or
    GDR and does not weaken production runtime-safety coverage.
  - Verification: the targeted nine runtime-safety runner tests pass; a fresh
    MSVC standalone configure/build passes all eleven C++ contracts and all
    three checked-in golden-corpus verification runs; an explicit MSVC C++20
    compile/link/run of the protocol-frame contract with the TSan interposition
    definition proves the conditional contract path; and changed C++ passes
    `clang-format --dry-run --Werror`.
  - Owner review: the owner authorized dispatching the hosted matrix. Phase 4
    remains **In Progress** because the corrected commit must pass the complete
    six-workflow matrix and then receive explicit exit-gate approval.
  - Follow-ups: finish reviewing the first dispatch, commit and push the
    correction, rerun all six hosted workflows against that single corrected
    candidate, record exact job evidence, and request Phase 4 exit approval.

- 2026-08-27 — Phase 4 exit gate — Awaiting Owner Approval
  - Change: committed the TSan contract-harness correction as `3fe7ba2986`,
    pushed it to `origin/vnext`, and manually dispatched the complete hosted
    matrix against exact commit
    `3fe7ba2986209fe519f660e4d670155382462933`. The superseded candidate's three
    still-running baseline workflows were cancelled after its runtime-safety
    gate had already failed; none is used as completion evidence.
  - Decisions: none. ADR-0019 remains accepted without amendment, and no
    architecture, authority, state-scope, or gameplay-behavior choice changed.
  - Gate evidence: all six corrected-candidate workflows completed
    successfully: Linux baseline
    [`33138536438`](https://github.com/poisson-fish/TES3MP/actions/runs/33138536438),
    Windows baseline
    [`33138537847`](https://github.com/poisson-fish/TES3MP/actions/runs/33138537847),
    macOS baseline
    [`33138539514`](https://github.com/poisson-fish/TES3MP/actions/runs/33138539514),
    FlatBuffers proof
    [`33138541086`](https://github.com/poisson-fish/TES3MP/actions/runs/33138541086),
    GameNetworkingSockets proof
    [`33138542487`](https://github.com/poisson-fish/TES3MP/actions/runs/33138542487),
    and runtime safety
    [`33138543978`](https://github.com/poisson-fish/TES3MP/actions/runs/33138543978).
    All 17 jobs passed: Linux GCC 13 and Clang 18; Windows MSVC 2022 v143;
    macOS arm64 and x86-64 with Xcode 16; all five FlatBuffers proof jobs; all
    five GameNetworkingSockets proof jobs including Clang 18 ASan/UBSan; and
    both TES3MP Clang 18 ThreadSanitizer and ASan/UBSan/fuzz jobs.
  - Verification: all 17 evidence artifacts were retained. Runtime-safety
    artifact `9673044842` (ASan/UBSan/fuzz) has SHA-256
    `8b929ddc1d2d6bcf67bb70cc6a8edda7d0aa939a3b9344c355accb54f8d5a7dc`;
    artifact `9672993368` (ThreadSanitizer) has SHA-256
    `3bcc2113660e81881c2779a94863e5cbbb4f3a7354f2260752ff4e389ce8ec3b`.
    The retained local correction evidence is 98 repository-owned Python
    tests, all eleven MSVC contracts plus three golden-corpus checks,
    provenance for 170 intentional differences and 54 dependency inputs,
    legacy exclusion over 3,864 paths, 59 CMake files, 32 compile commands, and
    91 Ninja edges, formatting, JSON validation, and clean diff checks.
  - Owner review: the automated Phase 4 exit gate is green. Explicit owner
    exit approval remains pending, so Phase 4 stays **In Progress** and Phase 5
    remains **Not Started**.
  - Follow-ups: obtain explicit Phase 4 exit-gate approval, then mark Phase 4
    **Implemented** in a status-only closeout before beginning any Phase 5 work.

- 2026-08-27 — Phase 4 — Implemented
  - Change: marked Phase 4 **Implemented** after every Slice 4.1–4.6
    deliverable was implemented and demo-accepted, the corrected complete
    hosted matrix passed, and the project owner explicitly approved the phase
    exit gate. No production code or accepted behavior changed in this
    status-only closeout.
  - Decisions: none. ADR-0004, ADR-0019, ADR-0021 through ADR-0025, and
    GDR-0011 remain accepted without amendment.
  - Gate evidence: the simulated client handshake, capability negotiation,
    authentication boundary, reliable/latest-wins envelopes, and bounded
    in-memory player exchange satisfy the declared functional gate. Malformed,
    oversized, legacy, stale, duplicate, contradictory, and capability-failure
    contracts prove fail-closed behavior without partial state commits. The
    complete 17-job hosted evidence is recorded immediately above.
  - Verification: retained local verification covers 98 repository-owned
    Python tests, all eleven MSVC contracts plus three golden-corpus checks,
    exact five-header FlatBuffers regeneration, provenance for 170 intentional
    differences and 54 dependency inputs, legacy exclusion over 3,864 paths,
    59 CMake files, 32 compile commands, and 91 Ninja edges, plus formatting,
    JSON, Markdown-link, and clean-diff checks. All six corrected-candidate
    hosted workflows passed on `3fe7ba2986209fe519f660e4d670155382462933`.
  - Owner review: explicit Phase 4 exit-gate approval received from the project
    owner in the 2026-08-27 working session.
  - Follow-ups: none for Phase 4. Phase 5 is now eligible, but its kickoff and
    any unresolved Slice 5.1 architecture/authority decisions require owner
    review before production implementation begins.

## Phase 5 — Deterministic authoritative server core

[Back to the phase tracker](IMPLEMENTATION_PLAN.md#phase-5--deterministic-authoritative-server-core)

- Define total command ordering explicitly, including ties among connections and
  commands arriving for the same tick. Never depend on hash-container iteration.
- State-change records should describe domain changes, not serialized network or
  database bytes. Protocol, scripting, persistence, and metrics adapt from the
  same committed domain record.
- The first checksum need cover only canonical state present in this phase, but
  later phases must extend it whenever they add durable data.
- Persistence is intentionally only an interface here so Phase 20 does not force
  a later redesign of the server mutation boundary.

- 2026-08-27 — Slice 5.1 — In Progress
  - Change: confirmed the Phase 4 exit commit and clean active tree, inspected
    the accepted deterministic scheduler, command/envelope values, target graph,
    and Phase 4 exchange boundary, and added proposed
    [`ADR-0026`](adr/ADR-0026-phase5-writer-command-intake-and-ordering.md) with
    five owner-gated option sets. No production intake, queue, ordering,
    canonical state, reducer, authority, or gameplay behavior changed.
  - Decisions: none accepted. The recommendation is an owned typed server-core
    proposal; writer-context submission with a sealed cutoff and drain-time
    stamps; hard v1 ceilings of 4,096 global pending, 128 pending per session
    generation, and 1,024 commands per tick; typed immediate full-queue
    rejection with FIFO tick deferral; and a dedicated intake/tick coordinator
    that returns ordered batches without canonical mutation.
  - Verification: `python -m unittest discover -s scripts/tests` passes all 98
    repository-owned Python tests. Staged
    `python scripts/verify_vnext_baseline.py --index` accounts for 171
    intentional differences and verifies 54 dependency inputs. Staged legacy
    exclusion checks 3,865 tracked paths, 59 CMake files, 1,254 compile
    commands, and 1,971 Ninja edges. The provenance JSON parses, all 83 local
    vNext Markdown links resolve, and staged diff checks pass. A C++ product
    build is not applicable because production code remains approval-gated.
  - Owner review: pending explicit Phase 5 kickoff approval or amendment of
    ADR-0026 Decisions 1–5 and the proposed acceptance tests.
  - Follow-ups: after approval, record the accepted options and implement only
    the approved Slice 5.1 intake, ordering, limits, observability, and tests.
    Slices 5.2–5.7 remain gated in plan order.

- 2026-08-27 — Slice 5.1 — In Progress
  - Change: recorded owner approval of
    [`ADR-0026`](adr/ADR-0026-phase5-writer-command-intake-and-ordering.md) and
    implemented its five Option A decisions. The server-core-owned
    `ServerCommandProposal` copies typed session, command, precondition, and
    motion values without retaining a wire root; the single-threaded
    `ServerCommandIntakeCoordinator` composes the existing fixed scheduler,
    applies the 4,096 global/128 per-session-generation/1,024 per-tick limits,
    stamps sealed FIFO batches at writer drain time, retains deferred suffixes,
    and fails closed without a partial batch at scheduler or ordinal exhaustion.
    Closed metrics/events make acceptance, deferral, rejection, pending work,
    and terminal errors observable without identity labels or user text.
  - Decisions: the project owner approved ADR-0026 Option A for Decisions 1–5
    and the proposed acceptance tests on 2026-08-27. The implementation adds no
    canonical store, validation, deduplication, reducer, acknowledgement,
    transport action, client disconnect, persistence, or gameplay behavior.
  - Verification: a clean-directory standalone MSVC 19.44 C++20 configure,
    52-step build, and run pass all 12 contract executables, including the 12
    named Slice 5.1 ordering, cutoff, catch-up, limit, conservation, exhaustion,
    lifetime, no-validation, and dependency-isolation scenarios. `python -m
    unittest discover -s scripts/tests` passes all 98 repository-owned tests.
    Staged baseline provenance accounts for 174 intentional differences and 54
    dependency inputs; staged legacy exclusion checks 3,868 tracked paths, 59
    CMake files, 1,254 compile commands, and 1,971 Ninja edges. All six changed
    C++ files pass `clang-format --dry-run --Werror`; the provenance JSON parses,
    all 84 local vNext Markdown links resolve, the new public header contains no
    wire/generated/OpenMW/socket/platform/script/database surface, and staged
    diff checks pass. FlatBuffers regeneration/fuzz verification is not
    applicable because this slice changes no schema or decoder.
  - Owner review: architecture approval is complete. Implementation-demo
    acceptance remains pending, so Slice 5.1 stays **In Progress**.
  - Follow-ups: complete local repository verification, publish the
    implementation commit, and present the approved trace/limit/failure demo
    for owner acceptance. Slice 5.2 remains gated until Slice 5.1 is
    **Implemented**.

- 2026-08-27 — Slice 5.1 — Implemented
  - Change: retained implementation commit `e0dde571f5` without production-code
    changes and marked Slice 5.1 **Implemented** after the required owner demo
    acceptance.
  - Decisions: none. ADR-0026 remains accepted without amendment.
  - Verification: the accepted implementation retains a green clean-directory
    MSVC 19.44 C++20 configure/build and all 12 contract executables, 98 Python
    tests, provenance with 174 intentional differences and 54 dependency
    inputs, legacy exclusion over 3,868 tracked paths, 59 CMake files, 1,254
    compile commands, and 1,971 Ninja edges, six-file formatting, 84 local-link,
    public-header-isolation, JSON, and staged-diff checks.
  - Owner review: implementation-demo evidence explicitly accepted by the
    project owner in the 2026-08-27 working session.
  - Follow-ups: none for Slice 5.1. Slice 5.2 is now eligible but requires
    owner review of canonical state ownership, scope, invariants, and API shape
    before production state code lands.

- 2026-08-27 — Slice 5.2 — In Progress
  - Change: inspected the accepted authority/scope, deterministic simulation,
    spatial primitive, envelope, minimal exchange, and writer-intake records and
    implementations. Added proposed
    [`ADR-0027`](adr/ADR-0027-phase5-canonical-player-and-session-state.md) with
    five owner-gated option sets. No production canonical state, lifecycle,
    reducer, acknowledgement behavior, persistence, or gameplay code changed.
  - Decisions: none accepted. The recommendation is separate per-player entity
    and active-session-progress partitions; stable sorted vectors with hard
    256/256 ceilings and explicit unique bindings; one atomic spatial revision,
    last-change tick, and stable authority epoch; fully validated immutable
    construction with no general mutation API; and session-generation-scoped
    optional contiguous-finalized acknowledgement progress.
  - Verification: `python -m unittest discover -s scripts/tests -v` passes all
    98 repository-owned tests. Staged baseline provenance accounts for 175
    intentional differences and 54 dependency inputs; staged legacy exclusion
    checks 3,869 tracked paths, 59 CMake files, 1,254 compile commands, and
    1,971 Ninja edges. The provenance JSON parses, all 87 local vNext Markdown
    links resolve, and staged diff checks pass. A C++ product build, formatting,
    FlatBuffers regeneration, and fuzz verification are not applicable because
    this decision-only change adds no production source, schema, or decoder.
  - Owner review: pending explicit approval or amendment of ADR-0027 Decisions
    1–5 and the proposed acceptance tests. Identity creation, spawn, reconnect/
    replacement, persistence lifetime, cell transition, movement, reducer,
    interest, resync, and presentation behavior remain separately gated.
  - Follow-ups: present the decision packet and scenarios to the owner, record
    the chosen options, then implement only the approved Slice 5.2 state values,
    invariant checks, immutable access, observability, and tests. Slice 5.3
    remains gated.

- 2026-08-27 — Slice 5.2 — In Progress
  - Change: recorded owner approval of
    [`ADR-0027`](adr/ADR-0027-phase5-canonical-player-and-session-state.md)
    Option A for Decisions 1–5 and the presented acceptance tests. Added
    immutable player-entity and active-session-progress values, hard 256/256
    construction limits, strict stable ordering, unique identity and explicit
    binding checks, const spans and binary-search lookups, and a pure checked
    atomic spatial-advance operation. No general mutation or online install
    path exists.
  - Decisions: player spatial state remains distinct from active-session
    generation and contiguous-finalized acknowledgement progress. One entity
    revision covers cell/root transform and velocity; same-tick advances may
    increment it independently, while tick regression and exhaustion return
    typed errors. Identity and authority epoch cannot change through the
    operation. Lifecycle, persistence, reducer, command disposition,
    acknowledgement advancement, publication, movement, and gameplay remain
    gated.
  - Verification: a clean standalone MSVC 19.44 C++20 configure and 54-step
    build pass all 13 contract executables and three golden-corpus checks,
    including the 12 named Slice 5.2 scope, binding, ordering, bounds, revision,
    failure-preservation, acknowledgement, immutability, and dependency-
    isolation scenarios. `python -m unittest discover -s scripts/tests -v`
    passes all 98 repository-owned tests. Staged baseline provenance accounts
    for 178 intentional differences and 54 dependency inputs; staged legacy
    exclusion checks 3,872 tracked paths, 59 CMake files, 1,254 compile commands,
    and 1,971 Ninja edges. All three new C++ files pass formatting; the
    provenance JSON parses, all 88 local vNext Markdown links resolve, the new
    public header contains no wire/generated/OpenMW/socket/platform/script/
    database surface, and staged diff checks pass. FlatBuffers regeneration and
    fuzz verification are not applicable because this slice changes no schema
    or decoder.
  - Owner review: architecture approval is complete. Implementation-demo
    acceptance remains pending, so Slice 5.2 stays **In Progress**.
  - Follow-ups: complete the repository gates, publish the implementation
    commit, and present the stable-order/binding/boundary/revision demo for
    owner acceptance. Slice 5.3 remains gated.

- 2026-08-28 — Slice 5.2 — Implemented
  - Change: retained implementation commit `f1a1f0632e` without production-code
    changes and marked Slice 5.2 **Implemented** after the required owner demo
    acceptance.
  - Decisions: none. ADR-0027 remains accepted without amendment.
  - Verification: a fresh standalone MSVC 19.44 C++20 configure and 55-step
    build passed all 13 contract executables and three golden-corpus checks.
    `python -m unittest discover -s scripts/tests` passed all 98 repository-owned
    tests. Baseline provenance verified 178 intentional differences and 54
    dependency inputs; legacy exclusion checked 3,872 tracked paths, 59 CMake
    files, 1,254 compile commands, and 1,971 Ninja edges. The three Slice 5.2
    C++ files pass `clang-format --dry-run --Werror`; JSON, all local vNext
    Markdown links, public-header dependency isolation, and diff checks pass.
  - Owner review: implementation-demo evidence explicitly accepted by the
    project owner in the 2026-08-28 working session.
  - Follow-ups: none for Slice 5.2. Slice 5.3 is now eligible but requires owner
    review of command validation, disposition, acknowledgement progression, and
    atomic reducer installation before production reducer code lands.

- 2026-08-28 — Slice 5.3 — In Progress
  - Change: inspected the accepted authority, deterministic simulation,
    protocol envelope, minimal motion intent, writer intake, canonical state,
    acknowledgement, and observability contracts. Added proposed
    [`ADR-0028`](adr/ADR-0028-phase5-command-validation-and-atomic-reducer.md)
    with five owner-gated architecture option sets and proposed
    [`GDR-0012`](gdr/GDR-0012-phase5-minimal-motion-reducer-semantics.md) with
    four owner-gated behavior option sets. No production reducer, validation,
    acknowledgement advancement, canonical mutation, publication, or gameplay
    code changed.
  - Decisions: none accepted. The recommendation is a writer-confined owning
    reducer with sealed-batch invariant verification and per-command atomic installation;
    closed validation order and dispositions; exact-next finalization with
    atomic accepted-or-rejected acknowledgement; immutable candidate
    construction; current-epoch and same-batch duplicate checks while the
    cross-batch idempotency/online gate remains Slice 5.5; and exact canonical
    velocity replacement with no position integration, clamp, collision, cell,
    prediction, or presentation behavior.
  - Verification: `python -m unittest discover -s scripts/tests` passes all 98
    repository-owned tests. Staged baseline provenance accounts for 180
    intentional differences and 54 dependency inputs; staged legacy exclusion
    checks 3,874 tracked paths, 59 CMake files, 1,254 compile commands, and
    1,971 Ninja edges. The provenance JSON parses, all 96 local vNext Markdown
    links resolve, required ADR/GDR sections and proposal/status assertions pass,
    and staged diff checks pass. A C++ product build, formatting, schema
    regeneration, corpus, and fuzz verification are not applicable because no
    production source, schema, decoder, or corpus changes are proposed.
  - Owner review: pending explicit approval or amendment of ADR-0028 Decisions
    1–5, GDR-0012 Decisions 1–4, the Slice 5.5 boundary clarification, and the
    proposed acceptance tests.
  - Follow-ups: after approval, record the selected options and implement only
    the approved validation, reducer, acknowledgement, observability, and
    focused tests. Slice 5.4 and online reducer composition remain gated.

- 2026-08-28 — Slice 5.3 — In Progress
  - Change: recorded owner approval of
    [`ADR-0028`](adr/ADR-0028-phase5-command-validation-and-atomic-reducer.md)
    Option A for Decisions 1–5, its Slice 5.5 boundary clarification and
    acceptance tests, plus
    [`GDR-0012`](gdr/GDR-0012-phase5-minimal-motion-reducer-semantics.md)
    Option A for Decisions 1–4 and its behavior tests. No production reducer or
    gameplay code changed in this decision-acceptance commit.
  - Decisions: the approved reducer is writer-confined, verifies sealed intake
    batches, commits each finalizable command atomically through immutable
    candidate construction, uses the closed validation order, advances exact-
    next accepted-or-rejected acknowledgement progress, checks stable authority
    epoch and same-batch duplicates, and stays offline until Slice 5.5 adds the
    cross-batch command-ID window. Accepted motion exactly replaces velocity
    without changing transform or adding integration, clamp, collision, cell,
    prediction, or presentation behavior.
  - Verification: staged baseline provenance accounts for 180 intentional
    differences and 54 dependency inputs. The provenance JSON parses, all 98
    local vNext Markdown links resolve, accepted ADR/GDR register assertions
    pass, and staged diff checks pass. A product build and runtime tests are not
    applicable because this decision-acceptance change modifies no production
    source, schema, decoder, or corpus.
  - Owner review: architecture and gameplay approval is complete from the
    project owner in the 2026-08-28 working session. Implementation and demo
    acceptance remain pending.
  - Follow-ups: implement only the approved Slice 5.3 surface and tests. Slice
    5.4 and online reducer composition remain gated.

- 2026-08-28 — Slice 5.3 — In Progress
  - Change: implemented the approved writer-confined `CanonicalCommandReducer`
    over immutable canonical state. It verifies sealed-batch limits, scheduled
    tick, and strict ingress order before mutation; applies the closed command
    validation order; atomically replaces complete player/session candidates;
    advances exact-next accepted-or-rejected acknowledgement progress; and
    emits bounded closed disposition metrics/events. Accepted motion replaces
    velocity exactly while preserving transform and increments the entity
    revision once. Fifteen focused contract scenarios cover validation order,
    acknowledgement, atomicity, contention, intake boundaries, revision/tick
    failures, observability independence, velocity values, player isolation,
    dependency isolation, and the explicit Slice 5.5 online gate.
  - Decisions: no new architecture, authority, state-scope, or gameplay
    decisions were made. The implementation follows accepted
    [`ADR-0028`](adr/ADR-0028-phase5-command-validation-and-atomic-reducer.md)
    and [`GDR-0012`](gdr/GDR-0012-phase5-minimal-motion-reducer-semantics.md)
    Option A decisions. Same-batch command IDs are checked now; cross-batch ID
    reuse remains intentionally detectable only after Slice 5.5 adds the
    approved bounded window, so no online composition is permitted.
  - Verification: a fresh standalone MSVC 19.44 C++20 configure and 58-step
    build pass all 14 contract executables and three golden-corpus checks,
    including all 15 named Slice 5.3 reducer scenarios. `python -m unittest
    discover -s scripts/tests` passes all 98 repository-owned tests. Staged
    baseline provenance accounts for 183 intentional differences and verifies
    54 dependency inputs; staged legacy exclusion checks 3,877 tracked paths,
    59 CMake files, 1,254 compile commands, and 1,971 Ninja edges. All six
    changed C++ files pass `clang-format 22.1.3 --dry-run --Werror`; the
    provenance JSON parses, all 100 local vNext Markdown links resolve, the new
    public header contains no wire/generated/OpenMW/socket/platform/script/
    database surface, accepted ADR/GDR assertions pass, and staged diff checks
    pass. FlatBuffers regeneration and fuzz verification are not applicable
    because this slice changes no schema, decoder, or corpus.
  - Owner review: architecture and gameplay approval is complete.
    Implementation-demo acceptance remains pending, so Slice 5.3 stays **In
    Progress** and Slice 5.4 remains gated.
  - Follow-ups: publish the implementation commit and present the accepted,
    rejected, contention, observability, and Slice 5.5-gate evidence for owner
    acceptance. Do not compose the reducer into an online runtime.

- 2026-08-28 — Slice 5.3 — Implemented
  - Change: retained implementation commit `f57a4074db` without production-code
    changes and marked Slice 5.3 **Implemented** after the required owner demo
    acceptance.
  - Decisions: none. ADR-0028 and GDR-0012 remain accepted without amendment;
    online reducer composition remains prohibited until Slice 5.5 supplies the
    approved cross-batch command-ID idempotency window.
  - Verification: the accepted implementation retains a green fresh standalone
    MSVC 19.44 C++20 58-step build, all 14 contract executables and three
    golden-corpus checks, all 98 repository-owned Python tests, provenance with
    183 intentional differences and 54 dependency inputs, legacy exclusion over
    3,877 tracked paths, 59 CMake files, 1,254 compile commands, and 1,971 Ninja
    edges, six-file formatting, 100 local-link, public-header-isolation, JSON,
    ADR/GDR assertion, and staged-diff checks.
  - Owner review: implementation-demo evidence explicitly accepted by the
    project owner in the 2026-08-28 working session.
  - Follow-ups: none for Slice 5.3. Slice 5.4 is now eligible but requires owner
    review of immutable snapshot/event publication ownership, versioning,
    retention, and slow-reader policy before production publication code lands.

- 2026-08-28 — Slice 5.4 — In Progress
  - Change: inspected the accepted authority/state-scope, deterministic counter,
    observability, protocol snapshot timeline, canonical state, reducer, and
    Slice 5.5/5.6 boundaries. Added proposed
    [`ADR-0029`](adr/ADR-0029-phase5-immutable-canonical-publication-and-versioned-change-feed.md)
    with five owner-gated architecture and state-scope option sets. No
    production publication, reducer, protocol, checksum, resync, persistence,
    scripting, interest, runtime, or gameplay code changed.
  - Decisions: none accepted. The recommendation is a reducer-integrated
    immutable latest-publication slot; a checked global version per installed
    canonical candidate with batch exhaustion preflight; complete Phase 5 state
    plus typed replacement records; latest-batch-only retention with explicit
    version-gap snapshot replacement; and immutable reader handles with no
    callback, acknowledgement, queue wait, or writer backpressure. The
    publication remains domain-only and offline through Slice 5.5.
  - Verification: `python -m unittest discover -s scripts/tests` passes all 98
    repository-owned tests. Staged baseline provenance accounts for 184
    intentional differences and verifies 54 dependency inputs; staged legacy
    exclusion checks 3,878 tracked paths, 59 CMake files, 1,254 compile
    commands, and 1,971 Ninja edges. The provenance JSON parses, all 103 local
    vNext Markdown links resolve, ADR proposal/register and slice-status
    assertions pass, and staged diff checks pass. A C++ product build,
    formatting, FlatBuffers regeneration, corpus, and fuzz verification are not
    applicable because this proposal adds no production source, schema,
    decoder, or corpus.
  - Owner review: pending explicit approval or amendment of ADR-0029 Decisions
    1–5 and the proposed acceptance tests. No new GDR is proposed because the
    record publishes already committed complete domain state without selecting
    interest, visibility, movement, correction, or presentation behavior.
  - Follow-ups: after approval, record the selected options and implement only
    the approved immutable publication, state version, domain change records,
    reader gap handling, observability, and focused tests. Slice 5.5 and online
    composition remain gated.

- 2026-08-28 — Slice 5.4 — In Progress
  - Change: recorded owner approval of
    [`ADR-0029`](adr/ADR-0029-phase5-immutable-canonical-publication-and-versioned-change-feed.md)
    Option A for Decisions 1–5 and the proposed acceptance tests. No production
    publication or reducer code changed in this decision-acceptance commit.
  - Decisions: publication is reducer-integrated and immutable; one checked
    global version advances per installed canonical candidate; snapshots contain
    complete Phase 5 state and changes contain typed complete replacements; the
    core retains only the latest bounded batch and recovers version gaps from its
    snapshot; and readers receive immutable handles with no acknowledgement,
    callback, queue wait, shared lock, or writer backpressure. Protocol/interest,
    sinks, checksum/resync, and online composition remain gated.
  - Verification: staged baseline provenance accounts for 184 intentional
    differences and verifies 54 dependency inputs. The provenance JSON parses,
    all 103 local vNext Markdown links resolve, accepted ADR/register assertions
    pass, and staged diff checks pass. A product build and runtime tests are not
    applicable because this decision-acceptance change modifies no production
    source, schema, decoder, or corpus.
  - Owner review: architecture and state-scope approval is complete from the
    project owner in the 2026-08-28 working session. Implementation and demo
    acceptance remain pending.
  - Follow-ups: implement only the approved Slice 5.4 publication surface and
    tests. Slice 5.5 and online reducer composition remain gated.

- 2026-08-28 — Slice 5.4 — In Progress
  - Change: implemented the approved reducer-integrated immutable publication
    boundary. The reducer now shares immutable canonical state, starts at
    canonical version zero, conservatively preflights sealed-batch version
    capacity, increments once per installed player/session or acknowledgement-
    only candidate, records complete typed replacements in commit order, and
    atomically replaces one latest complete post-batch publication. Pure reader
    classification distinguishes no change, contiguous changes, a version gap
    requiring snapshot replacement, and an older retained publication. Thirteen
    focused Slice 5.4 scenarios cover initial/accepted/rejected publications,
    noncommitting dispositions, contiguous multi-commit versions, capacity,
    gap recovery, retained slow-reader handles, latest-only bounded retention,
    observability independence, dependency isolation, and later-slice gates.
  - Decisions: no new architecture, authority, state-scope, or gameplay
    decisions were made. The implementation follows accepted
    [`ADR-0029`](adr/ADR-0029-phase5-immutable-canonical-publication-and-versioned-change-feed.md)
    Option A Decisions 1–5. Publication remains complete Phase 5 domain state,
    latest-batch-only, immutable, non-backpressuring, and offline. Protocol/
    interest conversion, command-result delivery, persistence/replay/script
    sinks, checksum/resync, and cross-batch idempotency remain gated.
  - Verification: a fresh standalone MSVC 19.44 C++20 configure and 59-step
    build pass all 14 contract executables and three golden-corpus checks,
    including the 13 named Slice 5.4 scenarios. `python -m unittest discover -s
    scripts/tests` passes all 98 repository-owned tests. Staged baseline
    provenance accounts for 186 intentional differences and verifies 54
    dependency inputs; staged legacy exclusion checks 3,880 tracked paths, 59
    CMake files, 1,254 compile commands, and 1,971 Ninja edges. All eight changed
    C++ files pass `clang-format 22.1.3 --dry-run --Werror`; the provenance JSON
    parses, all 105 local vNext Markdown links resolve, the publication/reducer
    public headers contain no wire/generated/OpenMW/socket/platform/script/
    database surface, accepted ADR/register assertions pass, and staged diff
    checks pass. FlatBuffers regeneration and fuzz verification are not
    applicable because this slice changes no schema, decoder, or corpus.
  - Owner review: architecture and state-scope approval is complete.
    Implementation-demo acceptance remains pending, so Slice 5.4 stays **In
    Progress** and Slice 5.5 remains gated.
  - Follow-ups: publish the implementation commit and present the initial,
    accepted, acknowledgement-only, contiguous/gap, slow-reader, bounded-
    retention, exhaustion, and observability evidence for owner acceptance. Do
    not compose the reducer/publication path into an online runtime.

- 2026-08-28 — Slice 5.4 — Implemented
  - Change: retained implementation commit `d7e7b25950` without production-code
    changes and marked Slice 5.4 **Implemented** after the required owner demo
    acceptance.
  - Decisions: none. ADR-0029 remains accepted without amendment. Publication
    remains domain-only and online composition remains prohibited until Slice
    5.5 supplies cross-batch idempotency, checksum, and resync safety.
  - Verification: the accepted implementation retains a green fresh standalone
    MSVC 19.44 C++20 59-step build, all 14 contract executables and three
    golden-corpus checks, all 98 repository-owned Python tests, provenance with
    186 intentional differences and 54 dependency inputs, legacy exclusion over
    3,880 tracked paths, 59 CMake files, 1,254 compile commands, and 1,971 Ninja
    edges, eight-file formatting, 105 local-link, public-header-isolation, JSON,
    ADR assertion, and staged-diff checks.
  - Owner review: implementation-demo evidence explicitly accepted by the
    project owner in the 2026-08-28 working session.
  - Follow-ups: none for Slice 5.4. Slice 5.5 is now eligible but requires owner
    review of cross-batch command-ID retention and duplicate disposition,
    checksum scope/algorithm/versioning, resync request semantics, and the
    remaining online-composition gate before production safety code lands.

- 2026-08-28 — Slice 5.5 — In Progress
  - Change: inspected the accepted Phase 5 intake, state, reducer, and immutable
    publication contracts and added proposed
    [`ADR-0030`](adr/ADR-0030-phase5-idempotency-checksum-and-resync-boundary.md).
    The proposal defines five approval-gated decisions and 15 focused acceptance
    tests; it makes no production-code, protocol-schema, runtime, transport,
    interest, persistence, or gameplay-behavior change.
  - Decisions: recommend Option A for a per-active-session-generation immutable
    1,024-finalization command history; exact-next retained-ID rejection with a
    final acknowledgement-only disposition and deterministic FIFO eviction;
    explicit canonical V1 bytes plus CRC-64/ECMA-182 V1; a metadata-only
    current-session resync request returning the latest immutable publication;
    and removal of only the named core blocker while real online composition
    remains gated. No GDR is proposed because the slice changes committed safety
    state and recovery mechanics without selecting player-visible gameplay,
    interest, or presentation behavior.
  - Verification: all 98 repository-owned Python tests pass; staged baseline
    provenance passes with 187 intentional differences and 54 dependency
    inputs; staged legacy exclusion passes over 3,881 tracked paths, 59 CMake
    files, 1,254 compile commands, and 1,971 Ninja build edges; JSON,
    whitespace, proposal/owner-gate assertions, and all 109 local Markdown links
    pass. Product build, formatting, schema, and fuzz gates are not applicable
    because this decision packet changes documentation/provenance only.
  - Owner review: pending explicit approval or amendment of ADR-0030 Decisions
    1–5, the 1,024-record bounded retry horizon, CRC-64/ECMA-182 V1 selection,
    and the proposed acceptance tests.
  - Follow-ups: do not implement dependent Slice 5.5 production code until the
    ADR is accepted. After approval, implement only the bounded server-core
    idempotency, canonical encoding/checksum, resync values, observability, and
    focused tests; do not add wire, interest, socket, or runtime composition.

- 2026-08-28 — Slice 5.5 — In Progress
  - Change: recorded owner approval of
    [`ADR-0030`](adr/ADR-0030-phase5-idempotency-checksum-and-resync-boundary.md)
    without production-code changes.
  - Decisions: the project owner approved Option A for Decisions 1–5, the
    per-active-session-generation immutable 1,024-finalization retry horizon,
    exact-next duplicate finalization and FIFO eviction, explicit canonical V1
    bytes with CRC-64/ECMA-182 V1, metadata-only current-session resync, the
    remaining composition gate, and all 15 proposed acceptance tests.
  - Verification: baseline provenance and the 98 repository-owned Python tests
    pass on the decision candidate. Product build, formatting, schema, and fuzz
    gates are not applicable because this acceptance changes documentation only.
  - Owner review: architecture, authority, state-scope, compatibility,
    resource-bound, and recovery approval is complete from the project owner in
    the 2026-08-28 working session. Implementation and demo acceptance remain
    pending.
  - Follow-ups: implement only the approved bounded server-core idempotency,
    canonical encoding/checksum, resync values, observability, and focused
    tests. Do not add wire, interest, socket, target-projection, automatic-
    delivery, or runtime composition.

- 2026-08-28 — Slice 5.5 — In Progress
  - Change: implementation commit `ac627deafc` adds the accepted bounded
    per-generation finalized-command
    history, unified same/cross-batch duplicate membership, exact-next duplicate
    finalization, deterministic 1,024/1,025 FIFO eviction, immutable shared
    history state, and closed invariant validation. Added explicit complete
    Phase 5 canonical V1 little-endian bytes, allocation-free
    CRC-64/ECMA-182 V1 publication checkpoints, checkpoint ticks, and
    metadata-only current-session resync selection returning an existing latest
    immutable publication. Added one focused executable containing all 15
    approved Slice 5.5 scenarios and updated the prior reducer gate regression.
  - Decisions: no new architecture, authority, state-scope, compatibility, or
    gameplay decisions were made. The implementation follows accepted
    [`ADR-0030`](adr/ADR-0030-phase5-idempotency-checksum-and-resync-boundary.md)
    Option A for Decisions 1–5. Canonical history remains active-generation
    safety state; checksum is divergence evidence, not authentication; resync
    accepts no client state; and no wire, interest, socket, target projection,
    delivery action, or runtime loop was added.
  - Verification: a fresh standalone MSVC 19.44 C++20 configure and 63-step
    build pass all 15 contract executables and three golden-corpus checks,
    including all 15 named Slice 5.5 acceptance scenarios. `python -m unittest
    discover -s scripts/tests -v` passes all 98 repository-owned tests. Staged
    baseline provenance passes with 192 intentional differences and 54
    dependency inputs; staged legacy exclusion passes over 3,886 tracked paths,
    59 CMake files, 1,254 compile commands, and 1,971 Ninja build edges. All 12
    changed/new C++ headers and sources pass `clang-format --dry-run --Werror`;
    JSON, whitespace, target-boundary, public-header isolation, and staged-diff
    checks pass. Schema regeneration, golden-corpus updates, and fuzz runs are
    not applicable because this slice changes no wire schema or bounded decoder.
  - Owner review: architecture/authority/state-scope approval is complete.
    Implementation-demo acceptance remains pending, so Slice 5.5 stays **In
    Progress** and Slice 5.6 remains gated.
  - Follow-ups: present duplicate, history-retention/eviction, epoch, exact-byte/
    CRC, observability-independence, resync, and no-runtime-composition evidence
    for owner acceptance.

- 2026-08-28 — Slice 5.5 — Implemented
  - Change: retained implementation commit `ac627deafc` without production-code
    changes and marked Slice 5.5 **Implemented** after the required owner demo
    acceptance.
  - Decisions: none. Accepted
    [`ADR-0030`](adr/ADR-0030-phase5-idempotency-checksum-and-resync-boundary.md)
    remains authoritative.
  - Verification: retained the fresh standalone MSVC 19.44 C++20 63-step build,
    all 15 contract executables, three golden-corpus checks, 98 repository-owned
    Python tests, staged provenance and legacy-exclusion checks, 12-file
    formatting check, JSON validation, whitespace check, target-boundary check,
    public-header isolation, and staged-diff check recorded above. This
    status-only closeout changes no production or test source.
  - Owner review: implementation-demo evidence was explicitly accepted by the
    project owner in the 2026-08-28 working session.
  - Follow-ups: none for Slice 5.5. Slice 5.6 is now eligible, but its sink
    ownership, delivery, failure, and resource-bound decisions require owner
    review before dependent production interfaces land.

- 2026-08-28 — Slice 5.6 — In Progress
  - Change: inspected the accepted reducer, immutable latest-publication,
    canonical change-record, checksum, observability, target-boundary, and later
    Phase 19/20 scripting/persistence requirements. Added proposed
    [`ADR-0031`](adr/ADR-0031-phase5-committed-domain-sink-boundary.md) with five
    owner-gated option sets. No production sink interface, reducer dispatch,
    runtime/backend, canonical state, authority, or gameplay behavior changed.
  - Decisions: none accepted. The recommendation is four nominal server-core
    sink roles over the same immutable publication handle; reducer-integrated
    post-publication fan-out in fixed role order; a maximum of one non-owning
    sink per role and four bounded `noexcept` attempts per committed batch;
    explicit absent/accepted/backpressured/failed results that never roll back,
    retry, or short-circuit canonical work; and no core-owned retention beyond
    the existing latest publication.
  - Verification: repository inspection confirms that
    `CanonicalStatePublication` already owns the complete immutable snapshot and
    ordered `CanonicalStateChangeRecord` values, reducer publication installs
    only after canonical candidates commit, and ADR-0020 observability remains
    a separate best-effort diagnostic boundary. `python -m unittest discover -s
    scripts/tests -q` passes all 98 repository-owned tests. Staged baseline
    provenance passes with 193 intentional differences and 54 dependency
    inputs; JSON parsing and staged whitespace/diff checks pass. No C++ build,
    schema regeneration, golden-corpus update, or fuzz run is applicable to this
    proposal-only documentation change.
  - Owner review: pending explicit approval or amendment of ADR-0031 Decisions
    1–5 and the proposed acceptance scenarios.
  - Follow-ups: do not add dependent production sink interfaces or reducer
    fan-out until the owner decision is recorded. Phase 19 still owns script
    runtime/event API policy; Phase 20 still owns persistence technology,
    durability acknowledgement, schema, replay format, and operator failure
    policy.

- 2026-08-28 — Slice 5.6 — In Progress
  - Change: changed ADR-0031 from **Proposed** to **Accepted** after explicit
    owner review. No production or test source changed in this decision-gate
    record.
  - Decisions: the project owner approved Option A for ADR-0031 Decisions 1–5
    and all 12 proposed acceptance-test contracts in the 2026-08-28 working
    session.
  - Verification: staged baseline provenance passes with 193 intentional
    differences and 54 dependency inputs; JSON parsing and staged
    whitespace/diff checks pass. No C++ build or test rerun is applicable to
    this approval-only record; the unchanged proposal already passed all 98
    repository-owned policy tests.
  - Owner review: architecture, ownership, state-scope, ordering, lifetime, and
    failure-policy approval is complete. Implementation and demo acceptance
    remain pending.
  - Follow-ups: implement only the approved server-core sink interfaces,
    post-publication fan-out/reporting, closed observability additions, bounded
    test fakes, and focused tests. Do not add a backend, runtime, worker, queue,
    persistence technology, script API, or gameplay behavior.

- 2026-08-28 — Slice 5.6 — In Progress
  - Change: implementation commit `2905c7a791` adds four nominal server-core
    persistence/replay/script/canonical-metrics sink ports, an explicit maximum-
    four non-owning bundle, and a complete per-role delivery report. The reducer
    installs each non-empty immutable publication as latest before offering the
    exact shared handle once in fixed role order. It attempts later roles after
    backpressure/failure, normalizes an invalid configured `NotConfigured`
    response to `Failed`, and exposes results without changing canonical
    success/error meaning. Added closed low-cardinality delivery observations
    and one focused executable with all approved scenarios plus invalid-result
    fail-closed coverage.
  - Decisions: no new architecture, authority, state-scope, compatibility, or
    gameplay decisions were made. The implementation follows accepted
    [`ADR-0031`](adr/ADR-0031-phase5-committed-domain-sink-boundary.md) Option A
    for Decisions 1–5. Version zero and no-commit work deliver nothing; server
    core owns no backend, worker, queue, retry, acknowledgement, database,
    script runtime/API, replay format, durability claim, or added retention.
  - Verification: a fresh standalone MSVC 19.44 C++20 configure and 65-step
    build pass all 16 contract executables and three golden-corpus checks,
    including all 13 focused sink scenarios. `python -m unittest discover -s
    scripts/tests -q` passes all 98 repository-owned tests. Staged baseline
    provenance passes with 195 intentional differences and 54 dependency
    inputs; staged legacy exclusion passes over 3,889 tracked paths, 59 CMake
    files, 43 compile commands, and 117 Ninja build edges. All seven changed/new
    C++ headers and sources pass `clang-format --dry-run --Werror`; the new
    public sink header compiles as an isolated MSVC C++20 translation unit; JSON,
    whitespace, target-boundary, and staged-diff checks pass. Schema
    regeneration, golden-corpus updates, and fuzz runs are not applicable
    because this slice changes no wire schema or bounded decoder.
  - Owner review: architecture/ownership/state-scope/failure-policy approval is
    complete. Implementation-demo acceptance remains pending, so Slice 5.6
    stays **In Progress** and Slice 5.7 remains gated.
  - Follow-ups: present installed-handle identity, acknowledgement-only and
    multi-change delivery, fixed role order, mixed failure/no-short-circuit,
    canonical/observability independence, retained-handle isolation, explicit
    gap, and no-backend/runtime evidence for owner acceptance.

- 2026-08-28 — Slice 5.6 — Implemented
  - Change: retained implementation commit `2905c7a791` without production-code
    changes and marked Slice 5.6 **Implemented** after the required owner demo
    acceptance.
  - Decisions: none. Accepted
    [`ADR-0031`](adr/ADR-0031-phase5-committed-domain-sink-boundary.md) remains
    authoritative.
  - Verification: retained the fresh standalone MSVC 19.44 C++20 65-step build,
    all 16 contract executables, three golden-corpus checks, 13 focused sink
    scenarios, 98 repository-owned Python tests, staged provenance and legacy-
    exclusion checks, seven-file formatting check, isolated public-header
    compilation, JSON validation, target-boundary check, and staged-diff check
    recorded above. This status-only closeout changes no production or test
    source.
  - Owner review: implementation-demo evidence was explicitly accepted by the
    project owner in the 2026-08-28 working session.
  - Follow-ups: none for Slice 5.6. Slice 5.7 is now eligible; inspect its
    property/simulation scope against accepted Phase 5 decisions before adding
    test-only coverage.

- 2026-08-28 — Slice 5.7 — Implemented
  - Change: inspected ADR-0013/0017 deterministic facilities and accepted
    ADR-0026–0031 Phase 5 production contracts, then added a test-only seeded
    eight-client simulation through the real intake and reducer. Bounded random
    streams generate approved applied, stale, duplicate, gap, session-
    generation, binding, revision, and epoch cases. The oracle checks complete
    state reconstruction, exact per-player isolation, session progress/history,
    state-version/change contiguity, publication completeness, and checksum
    equality after every batch; exact input/outcome traces and final canonical
    bytes reproduce from the recorded seed.
  - Decisions: no new architecture, authority, state-scope, compatibility, or
    gameplay decision is introduced. The test uses the approved ADR-0017
    xoshiro256** labeled stream and ADR-0013 writer-order/checksum rules and adds
    no production harness, API, state, target dependency, or behavior.
  - Verification: implementation commit `917ecc3278` passes a fresh MSVC 19.44
    C++20 67-step aggregate build with 17 contract executables and three corpus
    checks. The four focused property contracts perform 28 deterministic
    simulation runs of 48 batches each and cover 16 randomized seeds, exact
    same-seed command/disposition/final-byte/checksum replay, different-seed
    trace separation, and an eight-seed disposition-coverage matrix. All 98
    repository Python policy tests pass. Staged provenance verification reports
    196 intentional differences and 54 dependency inputs; staged legacy
    exclusion reports 3,890 tracked paths, 59 CMake files, 44 compile commands,
    and 121 Ninja edges. The new C++ source passes `clang-format`; JSON and
    staged-diff checks pass. Schema, golden-vector, and fuzz verification are not
    applicable because the slice changes no wire schema or bounded decoder.
  - Owner review: no new decision approval was required because this slice is
    test-only and exercises accepted behavior. The project owner explicitly
    accepted the demonstrated implementation behavior in the 2026-08-28 working
    session.
  - Follow-ups: none for Slice 5.7. Phase 5 remains **In Progress** until its
    hosted manual matrix and remaining exit-gate evidence pass; that work is
    intentionally deferred to a fresh session at the project owner's request.

- 2026-08-28 — Phase 5 exit gate — In Progress
  - Change: confirmed implementation closeout commit `006a3ce7d4` contains all
    implemented and owner-demo-accepted Phase 5 slices, corrected Slice 5.7's
    chronological note heading to match its tracker and README status, and
    prepared the status-only phase-completion candidate. Phase 5 remains **In
    Progress** and Phase 6 remains **Not Started**.
  - Decisions: none. No architecture, authority, state-scope, compatibility, or
    gameplay-behavior decision changed, and accepted ADR-0026 through ADR-0031
    plus GDR-0012 remain authoritative.
  - Verification: from the Visual Studio 2022 v17.14.39/MSVC 19.44.35228 x64
    environment, a fresh `cmake --preset tes3mp-standalone --fresh` configure
    and `cmake --build --preset tes3mp-standalone --parallel 4` completed all 67
    build steps and passed all 17 C++ contract executables plus three checked-in
    golden-corpus checks. `python -m unittest discover -s scripts/tests -v`
    passed all 98 repository-owned tests. `python
    scripts/verify_vnext_baseline.py` passed with 196 intentional differences
    and 54 dependency inputs. `python
    scripts/verify_vnext_legacy_exclusion.py --build-dir
    build/tes3mp-standalone` passed over 3,890 tracked paths, 59 CMake files, 44
    compile commands, and 121 Ninja build edges. The provenance JSON parses, all
    local links across 38 vNext Markdown files resolve, and diff checks pass.
  - Owner review: the Phase 5 hosted matrix and explicit exit-gate approval are
    pending. This local candidate evidence does not authorize Phase 6 kickoff or
    production work.
  - Follow-ups: publish the exact phase-completion candidate, manually dispatch
    all six vNext hosted workflows against it, require every declared job
    including macOS x86-64 to pass, review and record retained artifact evidence,
    then obtain explicit owner exit approval before marking Phase 5
    **Implemented** or beginning Phase 6.

- 2026-08-28 — Phase 5 exit gate — In Progress
  - Change: published phase-completion candidate `076dd2224a1a855d7c85c836dcb224f80e70161e`
    and manually dispatched all six vNext workflows. The macOS arm64 baseline
    exposed that Xcode 16's libc++ rejects the otherwise C++20-valid
    `std::atomic<std::shared_ptr<...>>` specialization. Replaced that private
    representation with a plain shared pointer accessed only through the
    standard acquire-load and release-store atomic shared-pointer operations.
  - Decisions: none. ADR-0029 requires atomic replacement of one immutable
    latest-publication handle, not a particular standard-library class-template
    spelling. The repair preserves immutable shared ownership, release/acquire
    visibility, latest-only retention, reader isolation, publication ordering,
    and every accepted architecture/state-scope boundary.
  - Verification: macOS arm64 job `98906119957` in workflow run
    [`33188061566`](https://github.com/poisson-fish/TES3MP/actions/runs/33188061566)
    failed while compiling `server_command_reducer.cpp` because Apple libc++
    instantiated the primary trivially-copyable-only `std::atomic<T>` template
    for the shared pointer. The still-running Linux, Windows, macOS, and
    GameNetworkingSockets workflows were cancelled after the candidate became
    ineligible; completed FlatBuffers run
    [`33188063579`](https://github.com/poisson-fish/TES3MP/actions/runs/33188063579)
    and runtime-safety run
    [`33188068142`](https://github.com/poisson-fish/TES3MP/actions/runs/33188068142)
    passed but are diagnostic evidence only and will not be mixed with the
    replacement matrix. After the repair, a fresh MSVC 19.44 C++20 configure
    and 67-step build pass all 17 C++ contracts and three corpus checks; all 98
    repository-owned Python tests pass; both changed C++ files pass
    `clang-format 22.1.3 --dry-run --Werror`; and diff checks pass.
  - Owner review: no new architecture, authority, state-scope, compatibility,
    or gameplay decision arose. The full replacement hosted matrix and explicit
    Phase 5 exit approval remain pending.
  - Follow-ups: complete indexed provenance, legacy-exclusion, JSON, and link
    verification; publish the repair candidate; dispatch all six workflows
    again against that one exact commit; review all declared jobs and retained
    artifacts; then request explicit Phase 5 exit approval.

- 2026-08-28 — Phase 5 exit gate — Awaiting Owner Approval
  - Change: retained the portability repair as published commit
    `8e7ca8ff607f3b95ec8d348919f6c1233a11eb23`, manually dispatched the complete
    replacement hosted matrix against that exact commit, and reviewed every
    declared job and retained artifact. Phase 5 remains **In Progress** and
    Phase 6 remains **Not Started**.
  - Decisions: none. The repair and verification do not amend accepted
    ADR-0026 through ADR-0031 or GDR-0012 and introduce no architecture,
    authority, state-scope, compatibility, security, or gameplay-behavior
    choice.
  - Gate evidence: all six replacement-candidate workflows completed
    successfully: Linux baseline
    [`33189362869`](https://github.com/poisson-fish/TES3MP/actions/runs/33189362869),
    Windows baseline
    [`33189365326`](https://github.com/poisson-fish/TES3MP/actions/runs/33189365326),
    macOS baseline
    [`33189367352`](https://github.com/poisson-fish/TES3MP/actions/runs/33189367352),
    FlatBuffers proof
    [`33189369561`](https://github.com/poisson-fish/TES3MP/actions/runs/33189369561),
    GameNetworkingSockets proof
    [`33189371582`](https://github.com/poisson-fish/TES3MP/actions/runs/33189371582),
    and runtime safety
    [`33189373947`](https://github.com/poisson-fish/TES3MP/actions/runs/33189373947).
    All 17 jobs passed: Linux GCC 13 and Clang 18; Windows MSVC 2022 v143;
    macOS arm64 and x86-64 with Xcode 16; all five FlatBuffers proof jobs; all
    five GameNetworkingSockets proof jobs including Clang 18 ASan/UBSan; and
    both TES3MP Clang 18 ThreadSanitizer and ASan/UBSan/fuzz jobs.
  - Verification: all 17 evidence artifacts are retained, non-expired, and
    non-empty. Runtime-safety artifact `9693163253` (ASan/UBSan/fuzz) has
    SHA-256 `095b5785dfc7c3cf1a6589c26f1b4d106ac2959eff4545ef55162829312bdaeb`;
    artifact `9693103241` (ThreadSanitizer) has SHA-256
    `9d74a564b4fbb37c39934f9b6633b83a728d2f403d6156ee977fdeb3392b3137`.
    Local replacement-candidate verification passes all 98 repository-owned
    Python tests, the standalone aggregate target with all 17 C++ contract
    executables and three golden-corpus checks, baseline provenance for 196
    intentional differences and 54 dependency inputs, legacy exclusion over
    3,890 tracked paths, 59 CMake files, 44 compile commands, and 121 Ninja
    edges, JSON parsing, 118 local Markdown links across 38 files, and clean
    diff checks.
  - Owner review: the automated Phase 5 exit gate is green. Explicit owner exit
    approval remains pending, so Phase 5 stays **In Progress** and Phase 6
    remains **Not Started**.
  - Follow-ups: obtain explicit Phase 5 exit-gate approval, then mark Phase 5
    **Implemented** in a status-only closeout before holding the Phase 6 kickoff
    and presenting its unresolved transport/session decision packet.

- 2026-08-28 — Phase 5 — Implemented
  - Change: marked Phase 5 **Implemented** after every Slice 5.1–5.7 deliverable
    was implemented and demo-accepted, the exact replacement-candidate hosted
    matrix passed, and the project owner explicitly approved the phase exit
    gate. This status-only closeout changes no production or test source.
  - Decisions: none. ADR-0026 through ADR-0031 and GDR-0012 remain accepted
    without amendment.
  - Gate evidence: the server core retains one writer-confined canonical
    mutation path; commits are atomic, deterministic, revisioned, and published
    as immutable state plus ordered domain changes; and bounded post-commit sink
    failures cannot roll back, mutate, or deadlock canonical state. The complete
    17-job hosted evidence is recorded immediately above.
  - Verification: retained local verification covers all 98 repository-owned
    Python tests, all 17 C++ contract executables and three golden-corpus checks,
    provenance for 196 intentional differences and 54 dependency inputs,
    legacy exclusion over 3,890 paths, 59 CMake files, 44 compile commands, and
    121 Ninja edges, plus JSON, Markdown-link, and clean-diff checks. All six
    hosted workflows passed on
    `8e7ca8ff607f3b95ec8d348919f6c1233a11eb23` with all 17 artifacts retained.
  - Owner review: explicit Phase 5 exit-gate approval received from the project
    owner in the 2026-08-28 working session.
  - Follow-ups: none for Phase 5. Phase 6 is now eligible, but its kickoff and
    unresolved transport/session architecture, security, and resource-bound
    decisions require owner review before dependent production work begins.

## Phase 6 — Maintained transport and secure network session

[Back to the phase tracker](IMPLEMENTATION_PLAN.md#phase-6--maintained-transport-and-secure-network-session)

- The transport interface should expose product semantics rather than mimic the
  selected library one method at a time.
- Session resumption credentials must be short-lived/revocable according to the
  threat model and must never be persisted or logged in reusable plaintext form.
- Disconnect and rejection reasons need stable internal categories, with a
  separately sanitized user-facing message.
- Android support is a selection constraint, not a Phase 6 deliverable; retain a
  small compile proof if ADR-0005 uses Android portability as a deciding factor.

- 2026-08-28 — Phase 6 kickoff / Slice 6.1 — Not Started
  - Change: confirmed the accepted Phase 5 exit and inspected ADR-0005,
    ADR-0014, the pinned GameNetworkingSockets dependency proof/lock, current
    target enforcement, and the behavior-free transport anchor. Added proposed
    [`ADR-0032`](adr/ADR-0032-phase6-transport-adapter-and-lifecycle-boundary.md)
    with six owner-gated option sets. No Phase 6 production source, dependency
    integration, socket behavior, authority, state, or gameplay code changed.
  - Decisions: none accepted. The recommendation is a selected-library adapter
    target behind the unchanged transport abstraction; one verified proof/
    production provisioner; owned numeric IP endpoints; a command/value-event
    runtime with never-reused IDs; caller-confined explicit pumping; and an
    initial proof-bounded 1-listener/8-pending/8-established/128-event profile
    until Slice 6.4 reviews measured product capacities.
  - Verification: `python -m unittest discover -s scripts/tests -q` passes all
    98 repository-owned tests. Indexed baseline provenance accounts for 197
    intentional differences and verifies 54 dependency inputs; indexed legacy
    exclusion checks 3,891 tracked paths, 59 CMake files, 44 compile commands,
    and 121 Ninja edges. The provenance JSON parses, all 122 local Markdown
    links across 39 files resolve, ADR proposal/decision/owner-gate assertions
    pass, and indexed diff checks pass. Product builds, dependency provisioning,
    formatting, schema, corpus, and fuzz verification are not applicable because
    this kickoff packet changes documentation/provenance only.
  - Owner review: pending explicit Phase 6 kickoff and approval or amendment of
    ADR-0032 Decisions 1–6 and the proposed Slice 6.1 acceptance tests. Phase 6
    and Slice 6.1 remain **Not Started**; dependent production work is gated.
  - Follow-ups: present the decision packet and scenarios to the project owner.
    After approval, record the accepted options and implement only the Slice
    6.1 lifecycle adapter, focused limits, boundary checks, and tests.

- 2026-08-28 — Phase 6 kickoff / Slice 6.1 — Not Started
  - Change: recorded owner approval of ADR-0032 Option A for Decisions 1, 2, 4,
    5, and 6. Reopened Decision 3 after the owner required hostname/DNS
    resolution from the first connection slice using a host string and separate
    port. Expanded the proposal with bounded input, resolver-integration, and
    multi-address connection options. No production source, dependency lock,
    socket behavior, or gameplay behavior changed.
  - Decisions: the adapter target split, verified provisioning model,
    command/value-event lifecycle, caller-confined pump, and initial 1/8/8/128
    profile are approved. The prior numeric-only endpoint recommendation is not
    accepted. Recommend bounded ASCII host plus port, a disposable current
    c-ares selection proof behind the GNS adapter, and a bounded RFC 8305-style
    dual-stack connection race. Exact resolver dependency acceptance remains a
    separate owner gate after proof evidence.
  - Verification: all 98 repository-owned Python tests pass. Indexed baseline
    provenance accounts for 197 intentional differences and 54 dependency
    inputs; indexed legacy exclusion checks 3,891 tracked paths, 59 CMake files,
    44 compile commands, and 121 Ninja edges. The provenance JSON parses, all
    122 local Markdown links across 39 files resolve, partial-approval/DNS-
    proposal assertions pass, and indexed diff checks pass. Product builds,
    dependency proof/provisioning, formatting, schema, corpus, and fuzz checks
    are not applicable because no production, dependency-lock, schema, or test
    source changed.
  - Owner review: partial architecture approval is recorded. ADR-0032 remains
    **Proposed**; amended Decisions 3.1–3.3 and their expanded tests remain
    pending, so Phase 6 and Slice 6.1 stay **Not Started**.
  - Follow-ups: present the focused DNS packet. If its c-ares option is approved,
    implement only a disposable exact-pin/platform/security proof and return its
    evidence for owner dependency acceptance before Phase 6 production code.

- 2026-08-28 — Phase 6 kickoff / Slice 6.1 — Not Started
  - Change: recorded owner approval of ADR-0032 Option A for amended Decisions
    3.1–3.3 and their expanded tests. All six transport-lifecycle architecture
    decisions are approved; no production or dependency-lock source changed in
    this approval-only record.
  - Decisions: connections accept a bounded ASCII hostname/IP string and
    separate port; numeric input bypasses DNS; the approved research path is a
    caller-pumped, privately linked c-ares candidate; and bounded dual-stack
    selection follows the proposed eight-result/two-candidate/50 ms/250 ms
    Happy Eyeballs profile. This approval authorizes only the disposable
    resolver proof, not an exact production dependency.
  - Verification: all 98 repository-owned Python tests pass. Indexed baseline
    provenance accounts for 197 intentional differences and 54 dependency
    inputs; indexed legacy exclusion checks 3,891 tracked paths, 59 CMake files,
    44 compile commands, and 121 Ninja edges. The provenance JSON parses,
    approval/dependency-gate assertions pass, and indexed diff checks pass.
    Product builds, dependency proof/provisioning, formatting, schema, corpus,
    and fuzz checks are not applicable because this approval record changes
    documentation only.
  - Owner review: transport-lifecycle architecture approval is complete. Exact
    c-ares source/profile acceptance remains pending proof evidence, so ADR-0032
    stays **Proposed** and Phase 6/Slice 6.1 stay **Not Started**.
  - Follow-ups: implement and run the disposable exact-pin c-ares proof on the
    supported desktop compiler matrix, retain its evidence, and return the
    dependency profile to the owner before production integration.

- 2026-08-28 — Phase 6 resolver selection proof — In Progress
  - Change: implemented the owner-authorized disposable c-ares selection proof
    without changing any production target. Added an exact 1.34.8 source and
    license lock, fail-closed verified-download/safe-extraction runner, isolated
    static C/C++20 proof build, loopback DNS fixture, repository policy tests,
    retained JSON evidence, and a manual supported-desktop workflow.
  - Decisions: no new architecture, authority, state-scope, or gameplay choice.
    The proof instantiates only the approved research profile: host plus separate
    port, numeric bypass, caller-pumped `ares_process_fds`, no event thread or
    application cache, eight owned unique results, and failure/lifecycle
    categories. It does not select the dependency for production.
  - Verification: a clean runner-built MSVC 19.44.35228 static c-ares proof
    passes all 13 loopback scenarios and retains the exact archive, license,
    compiler, profile, and CTest evidence; the evidence JSON SHA-256 is
    `cd49ad780cd467969de9b14ec9cfc7179d4192b2cb8225caa87386f26b2bd615`.
    All 107 repository-owned Python tests pass. Indexed baseline provenance
    accounts for 204 intentional differences and 61 dependency inputs; indexed
    legacy exclusion checks 3,898 tracked paths, 60 CMake files, 44 compile
    commands, and 121 Ninja edges. The exact lock validates, the workflow passes
    actionlint 1.7.12, both Python files compile, all 125 local Markdown links
    across 40 files resolve, the provenance JSON parses, and indexed diff checks
    pass. Production builds, formatting, schema/corpus, and fuzz checks are not
    applicable to this isolated proof; Linux sanitizer and the other supported
    compilers remain hosted-matrix gates.
  - Owner review: exact dependency acceptance remains pending. ADR-0032 stays
    **Proposed**, Phase 6 and Slice 6.1 stay **Not Started**, and no production
    c-ares or GameNetworkingSockets adapter integration is authorized yet.
  - Follow-ups: publish an exact candidate, run and review the five-job hosted
    proof matrix, then return the exact pin/profile and retained evidence for
    owner dependency acceptance before starting Slice 6.1 production work.

- 2026-08-28 — Phase 6 resolver selection proof — Hosted evidence complete
  - Change: published exact proof candidate `6dcea1b4afe2da9ce6005ec80cdb897d98f6739d`
    and manually dispatched the dedicated
    [five-job c-ares proof matrix](https://github.com/poisson-fish/TES3MP/actions/runs/33202049447).
    No production dependency integration or Phase 6 source changed.
  - Decisions: none. The evidence supports the proposed exact c-ares 1.34.8
    static, caller-pumped profile but does not itself accept the dependency.
  - Verification: Windows x86-64 MSVC 19.44.35228, Linux x86-64 GCC 13.3,
    Linux x86-64 Clang 18.1.3 with ASan+UBSan, macOS arm64 AppleClang 16, and
    macOS x86-64 AppleClang 16 all passed the same 13 loopback scenarios and
    retained five artifacts. Artifact review found one identical lock SHA-256
    `0ad50cda94dcca45fbfe3a7e166862ef3a572f9362ee3ada487872a6d018fec1`,
    archive SHA-256
    `c222b6d681096f9444d2c4863d2c1174019e27cacca0a4a5c114d36dd7d7bf78`,
    MIT license SHA-256
    `460f5e768fda3752ca2169a95df062578a10fb126bfd65f3b9b1a1bed2f84807`,
    build profile, budgets, test list, and no-production-claim across all jobs.
  - Owner review: exact c-ares dependency acceptance remains pending. ADR-0032
    stays **Proposed**, and Phase 6/Slice 6.1 stay **Not Started**.
  - Follow-ups: present the exact pin, restricted profile, exclusions, and green
    retained evidence for owner acceptance. After acceptance, mark ADR-0032
    **Accepted** and begin only the approved Slice 6.1 production integration.

- 2026-08-28 — Slice 6.1 — In Progress
  - Change: recorded explicit owner acceptance of the exact c-ares 1.34.8
    dependency profile, marked ADR-0032 **Accepted**, and began the approved
    production lifecycle integration. Added the selected-library-free owned
    endpoint, limit, identity, command, and value-event interface; the private
    GameNetworkingSockets/c-ares adapter and factory; a fail-closed verified
    dependency-manifest provisioner; focused abstraction and real-loopback
    tests; and CMake target/boundary integration. Tightened pending-inbound
    cleanup, stopped-listener callback rejection, event-overflow propagation,
    runtime-failure behavior, and stable concurrent-runtime factory failure.
  - Decisions: all ADR-0032 Option A decisions are accepted without amendment.
    Later channel, authentication, queue-productization, telemetry, and fault-
    harness work remains owned by Slices 6.2–6.6.
  - Verification: the exact five-job dependency proof and retained-artifact
    consistency review remain green on candidate `6dcea1b4af`;
    `python scripts/provision_vnext_transport.py --output build/vnext-transport-dependencies/manifest-verification.json`;
    MSVC developer environment plus
    `cmake --build build/slice61-gns --config Debug --target tes3mp_transport_lifecycle_tests tes3mp_transport_gns_tests --parallel 2`;
    `build/slice61-gns/tes3mp_transport_lifecycle_tests.exe`;
    `build/slice61-gns/tes3mp_transport_gns_tests.exe`;
    MSVC developer environment plus
    `cmake --build build/tes3mp-standalone --config Debug --target tes3mp_protocol_tests_run --parallel 2`;
    `python -m unittest scripts.tests.test_tes3mp_target_boundaries` (9
    tests); and `python scripts/verify_vnext_baseline.py` all pass locally.
  - Owner review: exact dependency-profile approval and Slice 6.1 production
    authorization received in the 2026-08-28 working session. Implementation
    demo acceptance has not yet been requested; the slice remains **In
    Progress**.
  - Follow-ups: complete the accepted deterministic fake-resolver/candidate
    scheduling seam and same-seed DNS/Happy-Eyeballs traces; add the remaining
    cancellation, handle-reuse, counter-exhaustion, dependency-negative, and
    sanitizer/platform acceptance evidence; then present the Slice 6.1
    implementation demo. No Slice 6.2 channel behavior is authorized here.

- 2026-08-28 — Slice 6.1 deterministic lifecycle closure candidate — In Progress
  - Change: added the selected-library-free deterministic Happy Eyeballs seam
    used by the production adapter, including the approved 50 ms IPv6
    preference, 250 ms candidate stagger, eight-address/two-live-candidate
    limits, immediate failure fallback, cancellation, and single-winner
    finalization. Added monotonically checked callback generations and exact
    handle/generation bindings so delayed callbacks cannot target a replacement
    listener, attempt, or connection. Added same-seed fake-resolution traces,
    stale-handle and generation-exhaustion checks, public-factory isolation and
    negative manifest/profile checks, plus a manually dispatched supported
    desktop lifecycle workflow with ASan/UBSan and TSan jobs.
  - Decisions: no new architecture, authority, state-scope, or gameplay choice.
    The work implements ADR-0032's accepted deterministic resolver/race,
    never-reused private generation, fail-closed provisioning, and focused
    platform/safety acceptance requirements. It adds no payload channel,
    authentication/resumption, product queue, telemetry, or gameplay behavior.
  - Verification: MSVC 19.44 builds
    `tes3mp_transport_gns_schedule_tests` and `tes3mp_transport_gns_tests`; both
    focused executables pass with numeric and DNS encrypted loopback scenarios.
    The exact verified transport provisioner; all three Slice 6.1 lifecycle
    executables; the full `tes3mp_protocol_tests_run` target; all 111
    repository-owned Python tests; indexed provenance for 216 intentional
    differences and 61 dependency inputs; four-file `clang-format --dry-run
    --Werror`; Python compilation; JSON validation; all 180 local Markdown
    links across 41 files; and staged whitespace checks pass. The hosted
    lifecycle run remains pending on the final candidate.
    Initial hosted candidates exposed and repaired two workflow-evidence gaps:
    the TSan job now uses the policy-required Clang 18 instead of GCC, and every
    checkout fetches the historical OpenMW baseline needed by provenance
    verification. GNS 1.6.0's real loopback reports a third-party internal
    socket/table lock-order inversion under TSan; no suppression or disabled
    detector was added. The TSan job therefore builds the fully instrumented
    adapter and runs the selected-library-free lifecycle plus deterministic
    resolver/race/generation contracts, while real encrypted GNS loopback runs
    under GCC, Clang ASan/UBSan, MSVC, and both macOS profiles.
  - Owner review: the project owner explicitly requested that Slice 6.1 be
    closed and approved completion work in the 2026-08-28 working session.
    Implementation-demo acceptance remains contingent on the final exact local
    and hosted evidence matching the approved scenarios.
  - Follow-ups: publish the exact candidate, run and review the manual lifecycle
    matrix, record retained evidence, then mark only Slice 6.1 **Implemented**.
    Phase 6 remains **In Progress** and Slice 6.2 remains separately gated.

- 2026-08-28 — Slice 6.1 hosted lifecycle repair — In Progress
  - Change: diagnosed manual lifecycle run
    [`33208490001`](https://github.com/poisson-fish/TES3MP/actions/runs/33208490001).
    The Clang ASan/UBSan executable failed before transport initialization
    because instrumented Protobuf was linked to a separately rebuilt,
    uninstrumented Abseil implementation. The adapter now consumes the exact
    verified installed Abseil package that was built with Protobuf. The Windows
    job also exposed an independent Debug/dynamic-runtime versus verified
    RelWithDebInfo/static-runtime link mismatch; every single-config lifecycle
    job now pins `RelWithDebInfo`, and MSVC pins `MultiThreaded`.
  - Decisions: none. These repairs implement the already owner-approved
    coherent Protobuf/Abseil instrumentation and static MSVC dependency profile;
    they change no dependency identity, transport architecture, authority,
    state scope, security boundary, or gameplay behavior.
  - Verification: the repaired graph configures and builds all three lifecycle
    executables under local MSVC 19.44; numeric/DNS encrypted loopback,
    deterministic resolver/race/generation, and owned lifecycle tests pass. A
    new boundary regression rejects a second Abseil source build, and all 14
    focused boundary tests pass. Run `33208490001` retained green GCC 13,
    Clang 18 TSan, and macOS arm64 evidence but is not completion evidence.
  - Owner review: the existing Slice 6.1 completion approval applies; final
    acceptance remains contingent on a replacement exact matrix with every job
    green and retained artifacts consistent.
  - Follow-ups: publish the coherent-package/runtime repair, dispatch the full
    replacement lifecycle matrix, review all six retained artifacts, and only
    then mark Slice 6.1 **Implemented**.

- 2026-08-28 — Slice 6.1 sanitizer API-boundary repair — In Progress
  - Change: replacement run
    [`33210477375`](https://github.com/poisson-fish/TES3MP/actions/runs/33210477375)
    confirmed the coherent Protobuf/Abseil repair, then UBSan rejected the
    adapter's C++ virtual callback-registration call across
    GameNetworkingSockets' deliberately RTTI-free build boundary. The adapter
    now uses the selected library's public flat callback-registration wrapper
    for setup and teardown and fails initialization closed if setup fails.
  - Decisions: none. The flat API is part of the pinned public dependency
    surface and preserves ADR-0032's accepted callback and lifecycle behavior.
    No sanitizer check or report class is suppressed.
  - Verification: the correction builds with MSVC 19.44; real encrypted numeric
    and DNS loopback pass; all 14 focused boundary tests pass; and a regression
    rejects reintroducing the direct virtual callback call. Run `33210477375`
    has green GCC 13, Clang 18 TSan, and macOS arm64 jobs but cannot serve as
    completion evidence because its ASan/UBSan candidate predates this repair.
  - Owner review: the existing completion approval remains applicable; the
    change introduces no architecture, authority, state-scope, security, or
    gameplay decision.
  - Follow-ups: publish the flat-wrapper repair, dispatch and review another
    exact six-job matrix, compare retained artifacts, and only then close the
    slice.

- 2026-08-28 — Slice 6.1 complete flat-API boundary — In Progress
  - Change: exact replacement run
    [`33211614381`](https://github.com/poisson-fish/TES3MP/actions/runs/33211614381)
    passed MSVC, GCC 13, Clang 18 TSan, macOS arm64, and macOS x86-64. The
    callback flat-wrapper repair allowed ASan/UBSan to reach listener creation,
    where UBSan identified the same RTTI-free boundary on the first C++ virtual
    `ISteamNetworkingSockets` call. Every selected socket operation now uses
    the pinned library's public flat API, not only callback registration.
  - Decisions: none. This systematically applies the same public-API integration
    rule and changes no owned lifecycle result, limit, generation, resolver,
    racing, encryption, authority, state-scope, or gameplay contract. Sanitizer
    coverage remains fully enabled with no report suppression.
  - Verification: all non-ASan jobs in run `33211614381` are green and retained
    their artifacts. The ASan/UBSan job failed exactly at the first remaining
    direct virtual call; no project-owned memory error was reported before it.
    A focused regression now requires flat wrappers for all eight socket
    operations used by the adapter and rejects any `sockets()->` call.
  - Owner review: the existing Slice 6.1 completion approval remains applicable;
    this correction introduces no new decision requiring review.
  - Follow-ups: build and run the systematic flat-boundary correction locally,
    publish it, require one exact six-job green matrix, compare all retained
    artifacts, and only then mark Slice 6.1 **Implemented**.

- 2026-08-28 — Slice 6.1 coherent selected-library instrumentation — In Progress
  - Change: exact run
    [`33213646234`](https://github.com/poisson-fish/TES3MP/actions/runs/33213646234)
    passed MSVC, GCC 13, Clang 18 TSan, macOS arm64, and macOS x86-64. Its
    ASan/UBSan job passed every project-owned flat API boundary, then reported a
    mixed-instrumentation Protobuf vptr failure inside GNS encrypted connection
    setup: the verified Protobuf/Abseil pair was instrumented, but the production
    graph's GNS protobuf consumers were not. The production safety profile now
    enables GNS's own ASan/UBSan integration and applies the same narrow
    `function` exclusion accepted and proven in the dependency selection.
  - Decisions: none. Coherent GNS/Protobuf/Abseil instrumentation and the exact
    function-cast exclusion were explicitly owner-approved for Slice 2.3. No
    additional report type, dependency code, runtime path, or project source is
    excluded from sanitizers.
  - Verification: all five non-ASan jobs in run `33213646234` are green with
    retained artifacts. ASan/UBSan reached the selected library's unsigned-cert
    serialization path before reporting the instrumentation mismatch; no direct
    virtual call or project-owned memory error remains in its trace. Focused
    boundary regression now requires both upstream sanitizer switches and the
    exact approved function-only exclusion.
  - Owner review: existing Slice 6.1 approval applies because this reuses the
    accepted dependency safety profile without amendment.
  - Follow-ups: verify locally, publish, obtain a completely green exact matrix,
    compare all six retained artifacts, and then close Slice 6.1.

- 2026-08-28 — Slice 6.1 — Implemented
  - Change: closed the approved owned transport lifecycle slice on exact
    implementation candidate `36c1ac6617d75c8d8ef88d687901c9d73b25d0a0`.
    The final repair enables GNS's upstream ASan/UBSan integration in the
    production graph and applies only the function-cast exclusion already
    accepted and proven in Slice 2.3. Phase 6 remains **In Progress**.
  - Decisions: none. The implementation conforms to accepted
    [`ADR-0032`](adr/ADR-0032-phase6-transport-adapter-and-lifecycle-boundary.md)
    and does not add payload channels, authentication/resumption, product
    queues, telemetry, gameplay behavior, or another sanitizer exclusion.
  - Verification: all six jobs in exact manual lifecycle run
    [`33215346506`](https://github.com/poisson-fish/TES3MP/actions/runs/33215346506)
    passed: MSVC 2022 v143, GCC 13, Clang 18 ASan/UBSan, Clang 18 TSan, macOS
    arm64 Xcode 16, and macOS x86-64 Xcode 16. Retained artifact IDs are
    `9703672808`, `9703585359`, `9703530129`, `9703416747`, `9703364727`, and
    `9703355615`. All six manifests have identical schema, production profile,
    locks, build profile, and production claim; every manifest evidence hash
    matches its retained file. All retain GNS lock SHA-256
    `367fd5820e41b77c5857ccf1dff9b2d0698cebf8dba5385f81204f1dba64350c`
    and c-ares lock SHA-256
    `0ad50cda94dcca45fbfe3a7e166862ef3a572f9362ee3ada487872a6d018fec1`.
    The retained c-ares archive and license evidence match SHA-256
    `c222b6d681096f9444d2c4863d2c1174019e27cacca0a4a5c114d36dd7d7bf78`
    and `460f5e768fda3752ca2169a95df062578a10fb126bfd65f3b9b1a1bed2f84807`.
    Locally, all three lifecycle executables, all 112 repository Python tests,
    transport formatting, JSON parsing, indexed provenance, and staged diff
    checks pass. The real GNS loopback remains excluded only from the TSan run
    because of the documented dependency-internal lock inversion; the adapter
    is still instrumented there, no detector suppression was added, and real
    loopback passes the other five profiles.
  - Owner review: the project owner explicitly approved closing Slice 6.1 in
    the 2026-08-28 working session, contingent on the final exact evidence. The
    green matrix and retained artifacts satisfy that condition.
  - Follow-ups: Slice 6.2 remains **Not Started**. Present its channel-semantics
    behavior options and recommendation for owner approval before implementation.

- 2026-08-28 — Slice 6.2 channel delivery candidate — In Progress
  - Change: implementation `2f022fe4ad` adds accepted
    [`ADR-0033`](adr/ADR-0033-phase6-transport-channel-and-delivery-semantics.md),
    the owned `ReliableOrdered`/`LatestWins` channel mapping, whole-frame byte
    ceilings, typed send/receive results, caller-bounded atomic receive drains,
    and private two-lane GameNetworkingSockets delivery. The adapter configures
    equal priority/weight before exposing connections, maps reliable work to
    ordered delivery and snapshots to unreliable no-delay delivery, and rejects
    unknown, empty, oversized, stale, pending, or contradictory input without
    session or canonical mutation.
  - Decisions: the project owner approved ADR-0033 Option A for Decisions 1–3
    without amendment. The implementation adds no application retry queue,
    snapshot coalescer, product capacity, rate policy, slow-peer eviction,
    authentication/resumption, detailed telemetry, authority, state scope, or
    gameplay behavior; those remain with Slices 6.3–6.5 and later GDRs.
  - Verification: MSVC 19.44 developer environment plus
    `cmake --build build/slice61-gns --config Debug --target
    tes3mp_transport_lifecycle_tests tes3mp_transport_gns_schedule_tests
    tes3mp_transport_gns_tests --parallel 2`; all three focused executables pass,
    including exact channel/budget mapping, bidirectional encrypted loopback,
    24-message reliable ordering under injected loss/reordering, and a newest
    latest-wins sample passing a delayed maximum-size reliable fragment. MSVC
    plus `cmake --build build/tes3mp-standalone --config Debug --target
    tes3mp_protocol_tests_run --parallel 2` passes the complete standalone
    contract target. `python -m unittest discover -s scripts/tests` passes all
    112 repository-owned tests; the 14 focused target-boundary tests separately
    pass. `python scripts/verify_vnext_baseline.py --index` accounts for 217
    intentional differences and verifies all 61 dependency inputs.
    `clang-format --dry-run --Werror` passes all five changed C++ files, JSON
    parsing succeeds, and staged whitespace checks pass.
  - Owner review: architecture approval is recorded in ADR-0033. Implementation
    demo acceptance has not yet been requested, so Slice 6.2 remains **In
    Progress**.
  - Follow-ups: present the approved class mapping, exact boundary rejection,
    reliable fault ordering, snapshot bypass, and absence of queue/gameplay
    behavior for owner demo acceptance. The complete hosted platform/sanitizer
    matrix remains the Phase 6 exit gate, not a per-slice trigger.

- 2026-08-28 — Slice 6.2 — Implemented
  - Change: closed implementation `2f022fe4ad` and its evidence record
    `b073d28c09` without changing the approved production candidate. The owned
    two-channel boundary, private equal-scheduled GNS lanes, bounded atomic
    receive drain, and fail-closed delivery behavior remain exactly as accepted
    in ADR-0033. Phase 6 remains **In Progress**.
  - Decisions: none. This closure records owner acceptance of the already
    approved and demonstrated behavior; it adds no queue policy, authentication,
    resumption, telemetry, authority, state scope, or gameplay behavior.
  - Verification: the candidate evidence remains green: all three focused
    transport executables, the complete standalone TES3MP contract target, all
    112 repository-owned Python tests, all 14 focused target-boundary tests,
    indexed and committed provenance, five-file formatting, JSON parsing, and
    staged whitespace checks pass.
  - Owner review: the project owner explicitly approved the Slice 6.2
    implementation demo in the 2026-08-28 working session. The accepted demo
    covered exact class mapping and bounds, reliable ordering under loss and
    reordering, latest-wins passage around delayed reliable work, and the
    absence of application queues or gameplay behavior. Slice 6.2 is
    **Implemented**.
  - Follow-ups: Slice 6.3 remains **Not Started** and separately gated. The
    complete hosted platform/sanitizer matrix remains the Phase 6 exit gate.

- 2026-08-28 — Slice 6.3 decision review — Not Started
  - Change: inspected the accepted transport, session, authentication, identity,
    and canonical-session boundaries and added proposed
    [`ADR-0034`](adr/ADR-0034-phase6-credential-and-resumption-boundary.md).
    No production source, schema, dependency, authority, state, or gameplay
    behavior changed.
  - Decisions: none accepted. The proposal recommends existing target ownership
    with private verified OpenSSL crypto; three bounded credential messages;
    process-local initial principals plus minimal resume routing claims; a
    digest-keyed 256-record transactional token store with caller-selected
    1-to-120-second lifetime; and bounded global/source token buckets with no
    release defaults.
  - Verification: `python scripts/verify_vnext_baseline.py --index` passes with
    218 intentional differences and all 61 dependency inputs verified;
    `python -m unittest discover -s scripts/tests -q` passes all 112
    repository-owned tests; the provenance JSON parses; and staged whitespace
    checks pass. Product build, schema, codec, fuzz, C++ formatting, sanitizer,
    and loopback checks are not applicable because production work remains
    owner-gated.
  - Owner review: pending explicit approval or amendment of ADR-0034 Decisions
    1–5 and the proposed tests. Slice 6.3 remains **Not Started**.
  - Follow-ups: present the decision packet. After approval, record accepted
    options, amend ADR-0023/ADR-0014 only as required by the chosen options, and
    begin only the approved production implementation.

- 2026-08-28 — Slice 6.3 credential wire foundation — In Progress
  - Change: recorded owner approval of
    [`ADR-0034`](adr/ADR-0034-phase6-credential-and-resumption-boundary.md)
    Option A for all five decisions and tests, plus the narrow ADR-0014/ADR-0023
    refinements. Added move-only bounded password and exact-32-byte token values;
    verifier-first T3AQ request, T3AA accepted, and T3AR generic-rejection
    codecs; reliable session-control kind mapping; exact pinned generated
    headers; four golden corpus entries; fuzz registration; and matching safety/
    schema policy assertions. Authentication material implementation now belongs
    to the protocol target that owns these wire values. Slice 6.3 is **In
    Progress**; no provider, limiter, token store, OpenSSL adapter, composition,
    or new gameplay behavior landed.
  - Decisions: ADR-0034 Option A Decisions 1–5 and proposed tests are accepted
    without amendment. This artifact implements only their bounded wire/value
    portion. It adds no release token lifetime/rate default, player/entity
    identity, canonical attachment, reconnect grace/UX, or gameplay authority.
  - Verification: MSVC 19.44 developer environment plus
    `python scripts/run_vnext_flatbuffers_proof.py --update-generated` passes the
    exact FlatBuffers 25.12.19 regeneration comparison and isolated proof;
    `cmake --build build/tes3mp-standalone --config Debug --target
    tes3mp_protocol_tests_run --parallel 2` passes the complete standalone
    contract target including new exact bounds, malformed/truncation, full
    request-bit mutation, and corpus checks. The three existing lifecycle,
    deterministic schedule, and real encrypted GNS loopback executables rebuild
    and pass. `python -m unittest discover -s scripts/tests -q` passes all 112
    repository-owned tests. Indexed baseline provenance accounts for 230
    intentional differences and verifies 67 dependency inputs. Four-file
    `clang-format --dry-run --Werror`, JSON parsing, and staged whitespace checks
    pass.
  - Owner review: architecture/security/session-scope approval was received in
    the 2026-08-28 working session. Implementation-demo acceptance remains
    pending completion of the full Slice 6.3 scenarios.
  - Follow-ups: implement the approved provider-result amendment, constant-time
    optional join-password provider, bounded global/source limiter,
    transactional digest-keyed resume store, private verified OpenSSL services,
    transport admission scope, composition/redaction checks, and focused
    encrypted loopback. No full hosted matrix runs until the Phase 6 exit
    candidate.

- 2026-08-28 — Slice 6.3 admission result and resume store — In Progress
  - Change: this implementation candidate replaces the principal-only provider
    success with the approved move-only `AuthenticatedAdmission`, preserves the
    initial-join path, and adds take-once resume routing claims. The server core
    now owns a fixed 256-record, SHA-256-digest-only, in-memory token store with
    caller-supplied 1-to-120-second lifetime, checked expiry/generation,
    constant-time digest comparison, atomic consume-and-rotate, collision and
    crypto-failure rollback, concurrent replay serialization, expiry
    reclamation, and restart invalidation.
  - Decisions: implements ADR-0034 Decisions 1, 3, and 4 without amendment. It
    adds no lifetime default, player binding, canonical mutation, reconnect UX,
    persistence, gameplay authority, concrete crypto dependency, limiter, or
    transport composition.
  - Verification: MSVC 19.44 plus `cmake --build
    build/tes3mp-standalone --config Debug --target
    tes3mp_protocol_tests_run --parallel 2` passes the complete standalone
    contract target, including focused issue, rotation, replay, context,
    exact-expiry, capacity, generation/deadline overflow, failure rollback,
    concurrent duplicate, take-once, and restart tests. `python -m unittest
    discover -s scripts/tests -q` passes all 112 repository-owned tests.
    Eight-file `clang-format
    --dry-run --Werror` and `git diff --check` pass. Indexed baseline provenance
    accounts for 233 intentional differences and verifies all 67 dependency
    inputs.
  - Owner review: ADR approval applies. Full Slice 6.3 implementation-demo
    acceptance remains pending its remaining artifacts and scenarios.
  - Follow-ups: implement the approved optional password provider, global/source
    limiter, private verified OpenSSL service, transport admission scope,
    composition, redaction scan, and focused encrypted loopback. The hosted
    matrix remains the Phase 6 exit gate.

- 2026-08-28 — Slice 6.3 optional join-password provider — In Progress
  - Change: this implementation candidate adds the approved server-core
    optional join-password provider. It retains the configured secret in the
    existing move-only cleansing value, compares a fixed 258-byte length-tagged
    block for every open, exact, wrong, missing, and maximum-size submission,
    allocates checked never-reused process-local routing principals only after
    successful comparison, and returns a cancellable one-shot provider
    operation with closed rejection results.
  - Decisions: implements the password-provider portion of accepted ADR-0034
    Decisions 1 and 3 without amendment. It adds no account/player identity,
    authority, canonical mutation, password format, release policy, rate
    default, resume composition, transport coupling, or gameplay behavior.
  - Verification: MSVC 19.44 plus `cmake --build
    build/tes3mp-standalone --config Debug --target
    tes3mp_protocol_tests_run --parallel 2` passes the complete standalone
    contract target, including focused open/protected/denied/exact-maximum,
    fixed-comparison-work, unique-principal, cancellation, and one-shot checks.
    `python -m unittest discover -s scripts/tests -q` passes all 112
    repository-owned tests. Indexed baseline provenance accounts for 233
    intentional differences and verifies all 67 dependency inputs. Three-file
    `clang-format --dry-run --Werror` and `git diff --check` pass.
  - Owner review: accepted ADR approval applies. Full Slice 6.3 implementation
    demo acceptance remains pending completion of its remaining artifacts and
    scenarios.
  - Follow-ups: implement the approved global/source limiter, private verified
    OpenSSL service, transport admission scope, composition, redaction scan,
    and focused encrypted loopback. The hosted matrix remains the Phase 6 exit
    gate.

- 2026-08-28 — Slice 6.3 admission scope and rate limiter — In Progress
  - Change: this implementation candidate adds the approved exact 32-byte
    opaque `AdmissionScopeId` common value and a server-core global/source
    token-bucket limiter. Caller policy is validated at the accepted 1–16
    source burst, 1–256 global burst, and 100-millisecond–120-second refill
    bounds. The limiter uses a fixed 256-entry source table, injected monotonic
    time, bounded scans, no sleeps, no post-construction allocation, and
    serialized admission.
  - Decisions: after options review, the project owner approved the separate
    protocol-target-owned opaque scope value (Option A), recorded as a narrow
    ADR-0034 amendment. This preserves ADR-0014's target graph and exposes no
    address, byte accessor, formatting, ordering, release default, account,
    player identity, canonical state, authority, or gameplay behavior.
  - Verification: MSVC 19.44 plus `cmake --build
    build/tes3mp-standalone --config Debug --target
    tes3mp_protocol_tests_run --parallel 2` passes the complete standalone
    contract target. Focused contracts cover exact scope construction and
    opacity, every policy edge, exact source/global refill edges, repeated-scope
    bypass resistance, fixed-table saturation, clock regression, and concurrent
    same-scope attempts. `python -m unittest discover -s scripts/tests -q`
    passes all 112 repository-owned tests. Indexed baseline provenance accounts
    for 234 intentional differences and verifies all 67 dependency inputs.
    Four-file `clang-format --dry-run --Werror` and `git diff --check` pass.
  - Owner review: accepted ADR-0034 applies, and the project owner explicitly
    approved Option A for the newly surfaced scope-type placement. Full Slice
    6.3 implementation-demo acceptance remains pending remaining artifacts and
    scenarios.
  - Follow-ups: implement private verified OpenSSL services, transport-side
    process-keyed IPv4/IPv6 scope derivation, limiter/provider composition,
    redaction scan, and focused encrypted loopback. The hosted matrix remains
    the Phase 6 exit gate.

- 2026-08-28 — Slice 6.3 production credential crypto — In Progress
  - Change: this implementation candidate adds an owned production
    `CredentialCrypto` factory. Portable builds retain the injectable fake
    boundary; the real-network profile privately links exact verified OpenSSL
    3.5.8 and implements bounded `RAND_bytes`, SHA-256, `CRYPTO_memcmp`, and
    temporary-digest cleansing. The real-network CMake profile now selects its
    MSVC static runtime before creating any TES3MP target, closing a runtime
    mismatch exposed by the broader authentication link check.
  - Decisions: accepted ADR-0034 already selects the existing server-core target
    plus private verified OpenSSL. No OpenSSL type enters a public header, and
    this change adds no principal policy, authority, canonical state, release
    default, persistence, compatibility, or gameplay behavior.
  - Verification: `python -m unittest
    scripts.tests.test_tes3mp_target_boundaries` passes all 14 boundary tests.
    MSVC 19.44 plus `cmake --build build/tes3mp-standalone --target
    tes3mp_protocol_tests_run` passes the complete portable contract target.
    A fresh `build/slice63-crypto-gns` configure selects exact OpenSSL 3.5.8;
    `cmake --build build/slice63-crypto-gns --target
    tes3mp_credential_crypto_tests tes3mp_server_authentication_tests
    tes3mp_transport_gns_tests` and all three resulting executables pass.
    `python -m unittest discover -s scripts/tests -q` passes all 112
    repository-owned tests. Indexed baseline provenance accounts for 236
    intentional differences and verifies all 67 dependency inputs.
    Three-file `clang-format --dry-run --Werror` and `git diff --check` pass.
  - Owner review: accepted ADR-0034 applies. Full Slice 6.3 implementation-demo
    acceptance remains pending the remaining artifacts and scenarios.
  - Follow-ups: implement transport-side process-keyed IPv4/IPv6 scope
    derivation, limiter/provider composition, redaction scan, and focused
    encrypted loopback. The hosted matrix remains the Phase 6 exit gate.

- 2026-08-28 — Slice 6.3 transport admission-scope review — In Progress
  - Change: repository inspection found that accepted ADR-0034 defines the
    private IPv4/IPv6 source scope but neither its owned transport handoff nor
    exact process-keyed derivation. Added proposed
    [`ADR-0035`](adr/ADR-0035-phase6-transport-admission-scope-handoff-and-derivation.md)
    with focused options and tests. No production source, target, dependency,
    authority, state, or gameplay behavior changed.
  - Decisions: none accepted. Recommend an optional opaque scope carried
    atomically by only `ConnectionAccepted` events and HMAC-SHA-256 over a
    fixed domain/family tag plus canonical IPv4 address or IPv6 /64 using a
    fresh runtime key. Query and separate-event handoffs plus custom keyed-hash
    and retained-address-map derivations remain viable alternatives.
  - Verification: `python -m unittest discover -s scripts/tests -q` passes all
    112 repository-owned tests. `python scripts/verify_vnext_baseline.py
    --index` accounts for 237 intentional differences, verifies all 67
    dependency inputs, and passes indexed provenance. `git diff --check`
    passes. Product build, C++ formatting, loopback, and sanitizer checks are
    not applicable because dependent production remains gated.
  - Owner review: pending explicit approval or amendment of ADR-0035 Decisions
    1 and 2 and the proposed focused tests. Slice 6.3 remains **In Progress**.
  - Follow-ups: after approval, record the accepted options and implement only
    scope handoff/derivation plus focused contracts before composing the
    limiter/provider and encrypted authentication loopback.

- 2026-08-28 — Slice 6.3 transport admission scope — In Progress
  - Change: recorded owner approval of
    [`ADR-0035`](adr/ADR-0035-phase6-transport-admission-scope-handoff-and-derivation.md)
    Option A for both decisions and implemented its narrow boundary. Only
    inbound `ConnectionAccepted` now carries an opaque `AdmissionScopeId`; the
    private real adapter derives it with HMAC-SHA-256 from a fresh runtime key,
    family domain separation, and canonical IPv4 or IPv6 /64 bytes. Key,
    temporary digest, and construction failures cleanse or fail closed.
  - Decisions: ADR-0035 is accepted without amendment. This adds no raw address
    API, query/staleness path, account, player binding, canonical mutation,
    authority, release default, reconnect UX, or gameplay behavior.
  - Verification: the real-network MSVC build and focused deterministic scope,
    encrypted GNS loopback, credential crypto, and server authentication
    executables pass. The complete portable `tes3mp_protocol_tests_run` target
    and all 14 target-boundary tests pass. All 112 repository-owned Python
    tests pass. Indexed baseline provenance accounts for 237 intentional
    differences and verifies all 67 dependency inputs. Six-file
    `clang-format --dry-run --Werror` and staged whitespace checks pass.
  - Owner review: architecture approval received in the 2026-08-28 working
    session. Full Slice 6.3 implementation-demo acceptance remains pending its
    remaining composition, redaction, and authentication-loopback scenarios.
  - Follow-ups: compose the approved limiter/provider/session control path,
    complete canary redaction scans and encrypted join/resume loopback, then
    present the Slice 6.3 demo. The hosted matrix remains the Phase 6 exit gate.

- 2026-08-28 — Slice 6.3 authentication composition review — In Progress
  - Change: inspection after ADR-0035 implementation found that the Phase 4
    session API carries only untyped bytes and fixes session generation before
    a resume token is inspected. Added proposed
    [`ADR-0036`](adr/ADR-0036-phase6-authentication-composition-and-session-finalization.md)
    with typed input, shared-service, failure, resume-install, and initial-token
    finalization options. No production source, authority, state, or gameplay
    behavior changed.
  - Decisions: none accepted. Recommend one atomic typed submission to a shared
    server-core service, exactly one global/source gate before join/resume
    routing, state-machine-owned resume claim installation, and bind-then-issue
    initial finalization with no tokenless success fallback.
  - Verification: `python -m unittest discover -s scripts/tests -q` passes all
    112 repository-owned tests. Indexed baseline provenance accounts for 238
    intentional differences and verifies all 67 dependency inputs. `git diff
    --check` passes. Product build, C++ formatting, loopback, and sanitizer
    checks are not applicable because dependent production remains gated.
  - Owner review: pending explicit approval or amendment of ADR-0036 Decisions
    1–3 and the proposed tests. Slice 6.3 remains **In Progress**.
  - Follow-ups: after approval, record the accepted options and implement only
    the approved shared composition/finalization before redaction and complete
    encrypted authentication loopback closure.

- 2026-08-30 — Slice 6.3 authentication composition approval — In Progress
  - Change: the project owner approved ADR-0036 Option A for Decisions 1–3 and
    its proposed focused tests. The decision record and live tracker now record
    that approval; production composition is unblocked.
  - Decisions: one atomic typed submission, one process-wide bounded routing
    service, and state-machine-owned resume installation with bind-then-issue
    initial finalization and no tokenless success fallback.
  - Verification: documentation-only approval recording; production evidence
    follows with the implementation.
  - Owner review: explicit approval received in the 2026-08-30 implementation
    discussion.
  - Follow-ups: implement and verify the accepted boundary, then complete
    credential redaction and encrypted authentication loopback closure.

- 2026-08-30 — Slice 6.3 authentication composition implementation — In Progress
  - Change: implemented ADR-0036's move-only typed submission, process-wide
    shared authentication service, exactly-once global/source gate, closed
    join/resume routing, deferred cancellable resume consumption,
    state-machine-owned resumed identity/generation installation, take-once
    accepted response, and fail-closed bind-then-issue initial finalization.
    Phase 4 deterministic test composition now supplies the typed boundary.
  - Tests: added shared-service gate/routing/cancel-before-consume coverage and
    initial bind/issue success, take-once, and issue-failure closure coverage.
  - Verification: standalone Debug configure and complete build pass in
    `/tmp/tes3mp-auth-compose`; `cmake --build /tmp/tes3mp-auth-compose --target
    tes3mp_protocol_tests_run -j2` passes the complete local C++ contract set;
    `python -m unittest discover -s scripts/tests -q` passes all 112 tests;
    `python scripts/verify_vnext_baseline.py` verifies 238 intentional
    differences and 67 dependency inputs; `git diff --check` passes.
    `clang-format --dry-run --Werror` was unavailable because no clang-format
    executable is installed in this environment.
  - Owner review: ADR approval applies. Full Slice 6.3 implementation-demo
    acceptance remains pending redaction and encrypted loopback closure.
  - Follow-ups: implement credential/source redaction checks and the complete
    encrypted authentication loopback; hosted matrix remains the Phase 6 exit
    gate.

- 2026-08-30 — Slice 6.3 credential/source redaction contracts — In Progress
  - Change: added compile-time public-boundary checks proving that join material,
    resume tokens, authentication requests/results, composed submissions, and
    opaque admission scopes have no stream insertion surface; raw credentials
    additionally have no equality, `bytes()`, or `data()` access surface. This
    adds no production behavior, authority, state, or gameplay decision.
  - Verification: focused server-authentication build and executable pass in
    `/tmp/tes3mp-auth-compose`; all 112 repository-owned Python tests pass;
    `python scripts/verify_vnext_baseline.py` verifies 238 intentional
    differences and 67 dependency inputs; `git diff --check` passes.
  - Owner review: accepted ADR-0034/0036 redaction requirements apply. Full
    Slice 6.3 implementation-demo acceptance remains pending encrypted
    authentication loopback closure.
  - Follow-ups: add the complete encrypted protected-join and single-use resume
    loopback, then present the Slice 6.3 demo. The hosted matrix remains the
    Phase 6 exit gate.

- 2026-08-31 — Slice 6.3 encrypted authentication loopback — In Progress
  - Change: composed the real encrypted GameNetworkingSockets loopback with the
    production OpenSSL credential provider and approved shared authentication
    service. The focused scenario sends a protected join through the reliable
    channel, issues and returns a resume token, resumes while preserving the
    principal/session and advancing generation, rotates the token, then replays
    the exact consumed wire request and observes generic denial.
  - Decisions: none; this is test-only closure of the accepted ADR-0034,
    ADR-0035, and ADR-0036 scenarios and changes no production architecture,
    authority, state scope, security policy, or gameplay behavior.
  - Verification: the existing MSVC real-network profile reconfigured against
    exact OpenSSL 3.5.8; `cmake --build build/slice63-crypto-gns --target
    tes3mp_transport_gns_tests` passes and
    `build/slice63-crypto-gns/tes3mp_transport_gns_tests.exe` passes. All 112
    repository-owned Python tests pass. Baseline provenance verifies 238
    intentional differences and 67 dependency inputs. One-file
    `clang-format --dry-run --Werror` and `git diff --check` pass.
  - Owner review: accepted decisions apply. Full Slice 6.3 implementation-demo
    acceptance remains pending.
  - Follow-ups: present the Slice 6.3 protected-join, resume, replay-denial, and
    redaction evidence for owner acceptance. The hosted matrix remains the
    Phase 6 exit gate.

- 2026-08-31 — Slice 6.3 provenance verification repair — In Progress
  - Change: refreshed the dependency-input hash for
    `components/tes3mp/CMakeLists.txt` after the encrypted authentication
    loopback added the approved server-core test linkage.
  - Decisions: none; this is a mechanical correction to fail-closed provenance
    evidence and changes no production architecture, authority, state scope,
    security policy, or gameplay behavior.
  - Verification: the exact focused transport test executable passes; all 112
    repository-owned Python tests pass; `python
    scripts/verify_vnext_baseline.py` verifies 238 intentional differences and
    67 dependency inputs; and `git diff --check` passes.
  - Owner review: Slice 6.3 implementation-demo acceptance remains pending.
  - Follow-ups: present the Slice 6.3 demo evidence for owner acceptance; the
    hosted matrix remains the Phase 6 exit gate.

- 2026-08-31 — Slice 6.3 implementation demo — Implemented
  - Change: marked Slice 6.3 **Implemented** after the required owner review of
    the protected-join, single-use resume/rotation, replay-denial, redaction,
    and verification evidence.
  - Decisions: no decision changed; accepted ADR-0034, ADR-0035, and ADR-0036
    remain controlling.
  - Verification: the focused real encrypted transport executable passes; all
    112 repository-owned Python tests pass; baseline provenance verifies 238
    intentional differences and 67 dependency inputs; and `git diff --check`
    passes.
  - Owner review: the project owner explicitly accepted the Slice 6.3
    implementation demo on 2026-08-31.
  - Follow-ups: Slice 6.4 remains **Not Started** pending its architecture
    decision review. The hosted matrix remains the Phase 6 exit gate.

- 2026-08-31 — Slice 6.4 queue/backpressure decision and implementation — In Progress
  - Change: accepted ADR-0037 and began the owned per-connection bounded
    reliable FIFO/latest-wins slot, deterministic rate/refill, fair pump, and
    sustained reliable-block eviction implementation with focused fake-runtime
    contracts.
  - Decisions: the project owner approved Option A for queue ownership,
    scheduling, rate/overload, and slow-peer eviction, plus the proposed hard
    ceilings and acceptance tests, on 2026-08-31.
  - Verification: MSVC builds and passes
    `tes3mp_transport_queue_tests`, `tes3mp_transport_lifecycle_tests`,
    `tes3mp_transport_gns_schedule_tests`, `tes3mp_transport_gns_tests`, and
    `tes3mp_server_authentication_tests` against the exact OpenSSL 3.5.8 real-
    transport profile. All 112 repository-owned Python tests pass. Three-file
    `clang-format --dry-run --Werror` and `git diff --check` pass. Indexed
    baseline provenance verifies 240 intentional differences and all 67
    dependency inputs.
  - Owner review: architecture approval recorded; implementation-demo
    acceptance remains pending.
  - Follow-ups: finish and verify the bounded implementation, then present the
    Slice 6.4 demo. Slice 6.5 remains separately gated.

- 2026-08-31 — Slice 6.4 implementation demo — Implemented
  - Change: marked Slice 6.4 **Implemented** after owner review of bounded
    reliable retention, newest-only sampled replacement, fair scheduling,
    deterministic refill, hard capacity enforcement, and isolated sustained-
    block eviction.
  - Decisions: no decision changed; accepted ADR-0037 remains controlling.
  - Verification: implementation `da8d139ce3`; five focused transport and
    authentication executables, all 112 repository-owned Python tests,
    formatting, diff, and indexed provenance gates pass as recorded above.
  - Owner review: the project owner explicitly accepted the Slice 6.4
    implementation demo on 2026-08-31.
  - Follow-ups: Slice 6.5 remains **Not Started** pending its telemetry and
    stable-reason architecture decision review. The hosted matrix remains the
    Phase 6 exit gate.

- 2026-08-31 — Slice 6.5 telemetry/reason decision and foundation — In Progress
  - Change: accepted ADR-0038 and began the selected-library-free stable reason
    and closed transport telemetry vocabulary.
  - Decisions: the owner approved Option A for all five decisions, including
    honest pending/unacknowledged retransmission-pressure gauges.
  - Verification: MSVC builds and passes the focused lifecycle, bounded queue,
    and real encrypted GNS transport executables. All 112 repository-owned
    Python tests pass. Three-file formatting and `git diff --check` pass;
    indexed provenance verifies 241 intentional differences and 67 dependency
    inputs.
  - Owner review: architecture approval recorded; implementation demo remains.
  - Follow-ups: compose bounded queue/runtime emission and private GNS lane
    sampling, then present the complete Slice 6.5 demo.

- 2026-09-01 — Slice 6.5 telemetry composition — In Progress
  - Change: composed ADR-0038's injected non-blocking sink with the bounded
    outbound queue and private real-transport adapter. Exact cumulative queue
    submitted, admitted, coalesced, rejected, blocked, and evicted observations
    and exact adapter-received observations saturate; queue gauges are sampled after mutation; and each
    explicit real-transport poll samples the two public GNS lane pending and
    reliable-unacknowledged byte fields without claiming an exact retransmit
    count. The default factory and queue remain source-compatible through a
    null sink.
  - Decisions: no new architecture, authority, state-scope, or gameplay choice;
    implementation follows accepted ADR-0038. GNS's latest-wins lane reports
    zero unacknowledged bytes because that lane is explicitly unreliable.
  - Verification: MSVC 19.44 Ninja builds and the focused
    `tes3mp_transport_queue_tests`, `tes3mp_transport_lifecycle_tests`, and
    exact-dependency `tes3mp_transport_gns_tests` executables pass. All 112
    repository-owned Python tests pass in 47.294 seconds. `python
    scripts/verify_vnext_baseline.py` verifies 241 intentional differences and
    all 67 dependency inputs. Six-file clang-format and `git diff --check`
    pass.
  - Owner review: ADR-0038 architecture approval applies; implementation-demo
    acceptance remains pending, so Slice 6.5 stays **In Progress**.
  - Follow-ups: present exact counter/coalescing, dropped-sink isolation,
    slow-peer reason, and GNS lane-pressure evidence for owner acceptance; the
    hosted matrix remains the Phase 6 exit gate.

- 2026-09-01 — Slice 6.5 implementation demo — Implemented
  - Change: marked Slice 6.5 **Implemented** after owner review of the closed
    stable-reason mapping, saturated exact application counters, queue gauges,
    dropped-sink isolation, slow-peer eviction reason, and honest public GNS
    per-lane pressure sampling.
  - Decisions: no decision changed; accepted ADR-0038 remains controlling.
  - Verification: implementation `b4c4201394`; focused queue, lifecycle, and
    exact-dependency encrypted GNS executables, all 112 repository-owned Python
    tests, six-file formatting, diff, and indexed provenance gates pass as
    recorded above.
  - Owner review: the project owner explicitly accepted the Slice 6.5
    implementation demo on 2026-09-01.
  - Follow-ups: Slice 6.6 is the next eligible Phase 6 slice. The complete
    hosted matrix remains the Phase 6 exit gate.

- 2026-09-01 — Slice 6.6 deterministic real-transport fault boundary — In Progress
  - Change: accepted
    [`ADR-0039`](adr/ADR-0039-phase6-deterministic-real-transport-fault-boundary.md)
    and began composing the existing repository-owned seeded/manual-clock fault
    scheduler above the owned real transport. The focused encrypted localhost
    scenario sends deterministic duplicated/reordered work in both directions
    and across both approved delivery classes; the existing GNS-native loss and
    reordering scenario remains supplemental below-adapter coverage.
  - Decisions: the project owner approved Option A without amendment. No GNS
    fault type or control enters an owned production or test-support API, and no
    production transport, authority, state-scope, or gameplay behavior changes.
  - Verification: MSVC 19.44 RelWithDebInfo builds and passes
    `tes3mp_fault_injection_tests`, `tes3mp_transport_gns_tests`,
    `tes3mp_transport_queue_tests`, and `tes3mp_transport_lifecycle_tests` from
    `build/slice63-crypto-gns`. All 112 repository-owned Python tests pass in
    43.566 seconds, including target-boundary and legacy-exclusion checks.
    Clang-format, `git diff --check`, JSON parsing, and indexed baseline
    provenance pass with 242 intentional differences and all 67 dependency
    declaration inputs verified.
  - Owner review: ADR-0039 architecture approval is recorded. Implementation
    demo acceptance remains pending, so Slice 6.6 stays **In Progress**.
  - Follow-ups: complete verification and present the deterministic transcript,
    encrypted real-socket crossing, bounds, and supplemental native-fault
    evidence. The hosted matrix remains the Phase 6 exit gate.

- 2026-09-01 — Slice 6.6 implementation demo — Implemented
  - Change: marked Slice 6.6 **Implemented** after owner review of the
    repository-owned deterministic transcript crossing real encrypted sockets
    in both directions and both channels, plus the supplemental native GNS
    loss/reordering coverage.
  - Decisions: no decision changed; accepted ADR-0039 remains controlling.
  - Verification: implementation `9ab4b6e9ed`; focused fault, encrypted GNS,
    queue, and lifecycle executables, all 112 repository-owned Python tests,
    formatting, diff, JSON, target-boundary, legacy-exclusion, and indexed
    provenance gates pass as recorded above.
  - Owner review: the project owner explicitly accepted the Slice 6.6
    implementation demo on 2026-09-01.
  - Follow-ups: Slice 6.7 is the next eligible Phase 6 slice. The complete
    hosted supported-platform and sanitizer matrix remains the Phase 6 exit
    gate.

- 2026-09-01 — Slice 6.7 supported-platform matrix coverage — In Progress
  - Change: expanded the manual transport lifecycle workflow from Slice 6.1's
    lifecycle/scheduling/loopback subset to the complete Phase 6 lifecycle,
    bounded-queue, authentication, scheduling, encrypted-loopback, and
    deterministic real-transport fault contracts on Linux, Windows, macOS
    arm64, and macOS x86-64. Added a repository policy test for the matrix.
  - Decisions: no new architecture, authority, state-scope, or gameplay choice;
    this applies the existing manual phase-exit and supported-platform rules.
  - Verification: the focused workflow-policy test passes, all 113 repository-
    owned Python tests pass in 43.957 seconds, `python
    scripts/verify_vnext_baseline.py --index` verifies 243 intentional
    differences and all 67 dependency inputs, and `git diff --cached --check`
    passes.
  - Owner review: hosted evidence and Phase 6 exit-gate approval remain pending;
    Slice 6.7 and Phase 6 stay **In Progress**.
  - Follow-ups: verify locally, commit the phase-completion candidate, then
    manually dispatch and review the complete hosted transport and runtime-
    safety matrix before marking Slice 6.7 or Phase 6 **Implemented**.

- 2026-09-01 — Slice 6.7 first hosted candidate — In Progress
  - Change: published candidate `4327655963` and manually dispatched the
    complete transport and runtime-safety matrices. Clang 18 rejected the
    defaulted `constexpr` equality for `AdmissionScopeId` because its private
    default constructor was not `constexpr`; made that constructor `constexpr`
    so the existing literal-type and equality contract is portable.
  - Decisions: no architecture, authority, state-scope, security, or gameplay
    decision changed; this is a compiler-portability repair.
  - Verification: runtime-safety run
    [33538310839](https://github.com/poisson-fish/TES3MP/actions/runs/33538310839)
    failed both Clang 18 jobs at the same compile diagnostic. Focused workflow
    and target-boundary Python tests, `python scripts/verify_vnext_baseline.py`,
    and `git diff --check` pass after the repair. Exact Clang and full matrix
    evidence remain pending on the repaired candidate.
  - Owner review: Phase 6 exit-gate approval remains pending; Slice 6.7 and
    Phase 6 stay **In Progress**.
  - Follow-ups: commit and dispatch both manual matrices on the repaired exact
    candidate, then review all jobs and retained evidence.

- 2026-09-01 — Slice 6.7 second hosted candidate — In Progress
  - Change: candidate `b2372d4d9f` passed the Clang 18 compile point, then the
    runtime-safety policy rejected a stale expected source name. Replaced it
    with the complete current non-GNS production/test source set and added the
    authentication fuzzer to the ASan/UBSan instrumentation audit.
  - Decisions: no product decision changed; this restores the accepted rule
    that every owned source built by the safety presets is instrumented.
  - Verification: runtime-safety run
    [33538589016](https://github.com/poisson-fish/TES3MP/actions/runs/33538589016)
    built all targets under both Clang profiles, then failed only the stale
    `server_core/authentication.cpp` audit. All nine runtime-safety policy unit
    tests and `git diff --check` pass after repair.
  - Owner review: Phase 6 exit-gate approval remains pending; Slice 6.7 and
    Phase 6 stay **In Progress**.
  - Follow-ups: run indexed provenance, commit, and redispatch both exact
    matrices; review all jobs and retained evidence.

- 2026-09-01 — Slice 6.7 repaired hosted candidate — In Progress
  - Change: published exact candidate `f46fa65efe` and redispatched both manual
    phase-exit workflows.
  - Decisions: none; accepted Phase 6 contracts remain controlling.
  - Verification: runtime-safety run
    [33538919258](https://github.com/poisson-fish/TES3MP/actions/runs/33538919258)
    passes Clang 18 TSan and Clang 18 ASan+UBSan with all bounded fuzz smokes;
    retained evidence uploads pass. Transport run
    [33538915714](https://github.com/poisson-fish/TES3MP/actions/runs/33538915714)
    is still in progress across all six supported compiler/platform jobs.
  - Owner review: transport evidence and Phase 6 exit-gate approval remain
    pending; Slice 6.7 and Phase 6 stay **In Progress**.
  - Follow-ups: review the completed six-job transport result and artifacts,
    then present the combined exact-candidate gate evidence for owner approval.

- 2026-09-01 — Slice 6.7 Linux transport portability repair — In Progress
  - Change: transport run `33538915714` passed Windows and both macOS jobs but
    failed all three Linux jobs at the same GNS flat-API output-parameter type
    mismatch. Changed the adapter-local result variable from `std::int64_t` to
    GNS's required `int64` alias; width and result interpretation are unchanged.
  - Decisions: no architecture, authority, state-scope, security, or gameplay
    decision changed; this is an ABI-exact LP64 portability repair.
  - Verification: the GNS and c-ares proofs passed before the Linux compile
    failure. Exact repaired-candidate matrices remain pending.
  - Owner review: Phase 6 remains **In Progress** until the repaired exact
    candidate is green and the owner accepts its exit evidence.
  - Follow-ups: verify, commit, push, and rerun both manual phase-exit workflows.

- 2026-09-01 — Slice 6.7 / Phase 6 exit gate — Implemented
  - Change: exact candidate `731bdec782` passes the complete hosted Phase 6
    matrix; marked Slice 6.7, Phase 6, and the program tracker row
    **Implemented**.
  - Decisions: no accepted ADR changed. Phase 7 remains gated by its kickoff
    and owner-approved GDR-0001 behavior packet.
  - Verification: transport run
    [33553049641](https://github.com/poisson-fish/TES3MP/actions/runs/33553049641)
    passes Windows MSVC, Linux GCC, Linux Clang ASan+UBSan, Linux Clang TSan,
    macOS arm64, and macOS x86-64. Runtime-safety run
    [33553053380](https://github.com/poisson-fish/TES3MP/actions/runs/33553053380)
    passes Clang 18 TSan and ASan+UBSan with bounded fuzz smokes.
  - Owner review: the project owner approved completion and the Phase 6 exit
    gate on 2026-09-01 after both exact-candidate runs completed green.
  - Follow-ups: Phase 7 kickoff and GDR-0001 decision packet; no Phase 7
    production implementation is authorized before approval.

## Phase 7 — Headless end-to-end multiplayer slice

[Back to the phase tracker](IMPLEMENTATION_PLAN.md#phase-7--headless-end-to-end-multiplayer-slice)

- 2026-09-02 — Slice 7.7 implementation demo and Phase 7 exit — Implemented
  - Change: advanced Slice 7.7 and Phase 7 to **Implemented** after review of
    the corrected complete evidence; Phase 8 Slice 8.1 is now eligible.
  - Decisions: no architecture, authority, state-scope, or gameplay behavior
    changed. Accepted ADR-0046 and the ADR-0044 two-pass clarification govern
    the demonstrated implementation.
  - Verification: accepted evidence includes the seeded 10,000-tick adverse
    matrix, 32-cycle reconnect proof, bounded queue drain, 60-second two-client
    real-process soak, matching canonical views, bounded RSS, the same-tick
    regression, focused MSVC tests, all 117 Python tests, baseline provenance,
    legacy exclusion, and diff checks.
  - Owner review: the project owner accepted Option A, the Slice 7.7
    implementation demo, and the Phase 7 exit gate on 2026-09-02.
  - Follow-ups: inventory the exact Phase 8.1 OpenMW hook surface and obtain
    owner approval before engine or adapter implementation.

- 2026-09-02 — Slice 7.7 same-tick publication correction — In Progress
  - Change: implementation `e01967ee8e` makes the server application complete
    connection, lifecycle, and command work before pumping outbound queues.
    Latest-wins coalescing now prevents intermediate same-tick views from
    reaching an observer.
  - Decisions: owner approved Option A on 2026-09-02. ADR-0044 records the
    two-pass pump clarification. Client contradiction rejection, protocol,
    authority, state scope, and gameplay behavior remain unchanged.
  - Verification: the deterministic two-connection regression proves the first
    observer receives one final two-player snapshot rather than two different
    same-tick snapshots. MSVC 19.51 RelWithDebInfo builds and passes
    `tes3mp_server_app_tests_run`; the optimized full lifecycle process demo
    passes after reproducing `ContradictorySameTick` before the fix. All 117
    Python tests, baseline provenance with 290 intentional differences and 69
    dependency inputs, legacy exclusion, and `git diff --check` pass.
  - Owner review: correction design approved; Slice 7.7 implementation-demo and
    Phase 7 exit-gate approval remain pending.
  - Follow-ups: owner reviews the corrected complete evidence; then Slice 7.7
    and Phase 7 may advance only through their required review gates.

- 2026-09-02 — Slice 7.7 real-process soak — In Progress
  - Change: implementation `f30b55c80b` adds two bounded idle soak-client
    modes, a portable Windows/Linux/macOS RSS sampler, equal-window sustained
    growth check, exact 60-second process driver, matching canonical-view
    comparison, queue-cap/zero-drain checks, and focused RSS contracts.
  - Decisions: project owner approved ADR-0046 Option A for sampled RSS and the
    reference-window observed-range rule. No authority, state scope, or gameplay
    behavior changed. Idle clients exercise sustained snapshot receipt; the
    existing deterministic matrix separately proves motion and convergence.
  - Verification: MSVC 19.51 RelWithDebInfo builds the server and headless
    client and passes `tes3mp_server_app_tests_run`. The real server/two-client
    soak passes 60 seconds with 61 samples per process, matching canonical
    views, lower final RSS medians, reliable queue high water 2 messages/232
    bytes, latest-wins high water 1 message/372 bytes, and zero final queues.
    All 117 Python tests, baseline/legacy verification, and diff checks pass.
  - Owner review: implementation demo and Phase 7 exit-gate approval remain
    pending; Slice 7.7 stays **In Progress**.
  - Follow-ups: owner reviews the recorded soak evidence; then Slice 7.7 and
    Phase 7 may advance only through their required review gates.

- 2026-09-02 — Slice 7.7 real-process queue drain — In Progress
  - Change: implementation `7adf261bf5` wires a constant-memory transport
    telemetry sink into the server process and makes the process driver verify
    credential-free reliable/latest queue high-water and final zero values.
  - Decisions: project owner approved Option A before implementation. This uses
    existing transport telemetry and accepted ADR-0046 caps; no authority,
    state-scope, or gameplay behavior changed.
  - Verification: MSVC 19.51 Debug `tes3mp_server_app_tests_run` and Release
    server/client builds pass. The real server/two-client driver reports reliable
    high-water 2 messages/232 bytes, latest-wins high-water 1 message/372 bytes,
    final zero queues, 32 reconnect cycles, and the complete prior lifecycle
    proof. All 115 repository Python tests and `git diff --check` pass.
  - Owner review: Slice 7.7 remains **In Progress**; implementation demo remains
    required after the soak proof.
  - Follow-ups: implement the approved 60-second real-process soak.

- 2026-09-02 — Slice 7.7 bounded reconnect loop — In Progress
  - Change: implementation `d91f21be44` makes the real-process driver run a
    separate 32-cycle disconnect/resume
    proof, paced within the existing authentication refill bound, while retaining
    the short observer, expiration, and fresh-identity lifecycle scenario.
  - Decisions: implements accepted ADR-0046's approved threshold and real-process
    layer without changing architecture, authority, state scope, gameplay behavior,
    authentication policy, or credentials/evidence handling.
  - Verification: MSVC 19.51 Release headless client build and server-app contracts,
    all 115 repository Python tests, legacy exclusion, baseline provenance, and
    `git diff --check` pass. The real server/two-client driver reports 32 reconnect
    cycles with stable session/player/entity identity, monotonic generation, and
    preserved revision and acknowledgement progress, followed by the existing
    lifecycle proof.
  - Owner review: Slice 7.7 remains **In Progress**; implementation demo remains
    required after the remaining proofs.
  - Follow-ups: implement the queue-drain process proof and 60-second real-process soak.

- 2026-09-02 — Slice 7.7 deterministic adverse matrix — In Progress
  - Change: implementation `3484483c17` runs every accepted named profile for
    10,000 manual-clock ticks, including the approved 500 ms stall, exact seeded
    replay, a changed-seed control, queue high-water checks, and zero drain.
  - Decisions: implements accepted ADR-0046 without changing architecture,
    authority, state scope, gameplay behavior, profiles, or thresholds.
  - Verification: MSVC 19.51 Debug `tes3mp_protocol_tests_run`, all 115
    repository Python tests, legacy exclusion, baseline provenance, and
    `git diff --check` pass. Matrix contracts prove reliable unique ordered
    application and sampled-state latest-wins convergence.
  - Owner review: Slice 7.7 remains **In Progress**; implementation demo remains
    required after the remaining proofs.
  - Follow-ups: implement the bounded 32-cycle reconnect loop, queue-drain
    process proof, and 60-second real-process soak.

- 2026-09-02 — Slice 7.7 profile and threshold catalog — In Progress
  - Change: added accepted ADR-0046 and a typed test-support catalog for the
    approved seed, named message-class profiles, fault bounds, 32 reconnect
    cycles, 10,000 deterministic ticks, and 60-second process soak.
  - Decisions: owner approved layered-matrix Option A and all proposed initial
    thresholds. Reliable-operation profiles do not inject application-message
    loss; selected-library encrypted tests retain packet-loss responsibility.
  - Verification: MSVC 19.51 Debug `tes3mp_fault_injection_tests` passes exact
    profile, seed, duration, count, and existing queue-cap contracts.
  - Owner review: architecture and thresholds approved on 2026-09-02; Slice 7.7
    implementation demo remains required before **Implemented**.
  - Follow-ups: implement the deterministic matrix, bounded reconnect loop,
    queue drain proof, and real-process soak.

- 2026-09-02 — Slice 7.6 implementation demo — Implemented
  - Change: accepted implementation `9c75892eb1`; Slice 7.6 advanced to
    **Implemented**.
  - Decisions: no accepted ADR/GDR decision changed.
  - Verification: owner reviewed the real two-client disconnect hiding,
    same-identity resume with revision and acknowledgement preservation,
    exact expiration rejection, fresh identity creation, and simultaneous-close
    atomic batch evidence.
  - Owner review: implementation-demo acceptance received on 2026-09-02.
  - Follow-ups: Slice 7.7 is next and remains separately decision-gated.

- 2026-09-02 — Slice 7.6 real lifecycle proof — In Progress
  - Change: implementation `9c75892eb1` adds the credential-free in-memory
    lifecycle client flow, preserves disconnect-time canonical command progress,
    batches same-pump disconnects into one canonical transaction, tolerates
    already-cleaned transport close events, and extends the real two-client
    process proof through resume, expiration, and fresh creation.
  - Decisions: owner approved ADR-0045 process-proof Option A and same-pump
    disconnect Option A. One client process owns sequential single-connection
    facades and in-memory tokens; simultaneous closes commit as one batch and
    notify surviving peers atomically. No authority, state-scope, or gameplay
    behavior changed.
  - Verification: MSVC 19.51 Debug `tes3mp_server_app_tests_run`,
    `tes3mp_server_lifecycle_tests`, and `tes3mp_protocol_tests_run` pass; the
    batch contract proves cancel/commit atomicity and two hidden records. Release
    server/client builds and `scripts/run_phase7_join_demo.py` pass with distinct
    players, simultaneous movement, converged views, stale-view rejection,
    hidden grace, same identity/generation resume, revision and acknowledgement
    preservation, expired-token rejection, and fresh identity creation. All 115
    repository Python tests, baseline provenance with 284 intentional
    differences and 69 dependency inputs, and `git diff --check` pass.
  - Owner review: automated implementation evidence is ready; Slice 7.6 remains
    **In Progress** until the project owner accepts the implementation demo.
  - Follow-ups: owner demo acceptance, then mark Slice 7.6 **Implemented** and
    begin separately gated Slice 7.7.

- 2026-09-02 — Slice 7.6 expiration pumping and output — In Progress
  - Change: the server application now drains every due hidden lifecycle record
    in deterministic coordinator order, atomically admits projected peer
    observation output, and commits clean canonical expiration. Exact-deadline
    expiration runs before message intake in the pump.
  - Decisions: implements accepted ADR-0045 Option A and its monotonic deadline
    ordering. No architecture, authority, state-scope, or gameplay behavior
    changed.
  - Verification: MSVC 19.51 Debug `tes3mp_server_app_tests_run`,
    `tes3mp_server_lifecycle_tests`, and `tes3mp_protocol_tests_run` pass.
    Focused app coverage proves no early expiration, exact-deadline expiration,
    hidden-record removal, player removal, and canonical `Expired` publication.
    All 115 repository Python tests pass.
  - Owner review: Slice 7.6 implementation demo remains required before
    **Implemented**.
  - Follow-ups: add the real two-client disconnect/resume/expiration proof.

- 2026-09-02 — Slice 7.6 prepared resume composition — In Progress
  - Change: resume authentication now returns a prepared token transaction;
    server composition prepares canonical resume, atomically admits the rotated
    credential, complete targeted snapshot, initial observation set, and peer
    observations, then commits token and lifecycle state. Token commit retains a
    bounded rollback window until canonical commit succeeds.
  - Decisions: owner approved ADR-0045 clarification Option A for prepared
    authentication ownership and A1 for reversible cross-transaction commit
    coordination. No authority, state-scope, or gameplay behavior changed.
  - Verification: MSVC 19.51 Debug `tes3mp_server_app_tests_run` and
    `tes3mp_protocol_tests_run` pass; focused `tes3mp_server_authentication_tests`
    passes with commit rollback, old-token
    recovery, retry, finalization, and rotated-token acceptance. All 115 Python
    tests pass. Staged baseline verification passes with 284 intentional
    differences and 69 dependency inputs; staged legacy exclusion passes across
    3,977 tracked paths, 62 CMake files, 1,254 compile commands, and 1,971 Ninja
    edges; the Release `tes3mp_server` executable builds; staged `git diff
    --check` passes.
  - Owner review: architecture clarifications approved; Slice 7.6 implementation
    demo remains required before **Implemented**.
  - Follow-ups: add expiration pumping/output and the real two-client
    disconnect/resume/expiration proof.

- 2026-09-02 — Slice 7.6 live disconnect composition — In Progress
  - Change: wired successful joins into the lifecycle coordinator and composed
    transport close through prepared canonical hide, complete peer observation
    admission, lifecycle commit, and connection teardown. Empty peer delivery
    sets are valid atomic admissions. Repaired the provenance manifest omission
    for the already-landed root vNext landing page.
  - Decisions: implements accepted ADR-0045 Option A; no architecture,
    authority, state-scope, or gameplay behavior changed.
  - Verification: fresh MSVC 19.51 standalone configure; Debug
    `tes3mp_server_app_tests_run` and `tes3mp_server_lifecycle_tests` pass; all
    115 repository Python tests pass. Staged baseline verification passes with
    283 intentional differences and 69 dependency inputs; staged legacy
    exclusion passes across 3,976 tracked paths, 62 CMake files, 65 compile
    commands, and 188 Ninja edges; staged `git diff --check` passes.
    The app contract proves a live joined session becomes one hidden bounded
    lifecycle record and disappears from active canonical state on close.
  - Owner review: ADR approval is recorded; implementation demo remains
    required before Slice 7.6 can become **Implemented**.
  - Follow-ups: compose prepared resume authentication/token/output commit,
    expiration pumping/output, and the real two-client process proof.

- 2026-09-02 — Slice 7.6 canonical lifecycle transactions — In Progress
  - Change: added the engine-independent bounded lifecycle coordinator and
    reducer prepare/commit transactions for disconnect hiding, same-identity
    resume with checked generation, and deterministic expiration; lifecycle
    changes now appear in immutable canonical publications.
  - Decisions: implements accepted ADR-0045 Option A. No architecture,
    authority, state-scope, or gameplay decision changed. Also repaired the
    provenance manifest omission for accepted ADR-0045.
  - Verification: fresh MSVC 19.51 Debug
    `tes3mp_server_lifecycle_tests` passes; MSVC 19.51 Release
    `tes3mp_protocol_tests_run` passes. All 115 repository Python tests pass.
    Staged baseline verification passes with 282 intentional differences and
    69 dependency inputs; staged legacy exclusion passes across 3,976 tracked
    paths, 62 CMake files, 1,254 compile commands, and 1,971 Ninja edges;
    staged `git diff --check` passes.
  - Owner review: ADR approval is recorded; implementation demo remains
    required before Slice 7.6 can become **Implemented**.
  - Follow-ups: compose token rotation, authentication, complete resume
    snapshot/observations, connection events, expiration pumping, and the real
    two-client disconnect/resume/expiration proof.

- 2026-09-02 — Slice 7.6 prepared resume-token rotation — In Progress
  - Change: accepted ADR-0045 and added bounded prepare/commit/cancel rotation
    to the production resume-token store. Cancellation preserves the old token;
    commit atomically replaces it; duplicate and stale preparations fail closed.
  - Decisions: project owner approved Option A for server-core lifecycle
    ownership, hidden grace records, failure-atomic lifecycle/token/output
    composition, and monotonic expiration ordering.
  - Verification: MSVC 19.51 build of `tes3mp_server_authentication_tests`
    passes, and `tes3mp_protocol_tests_run` passes. All 115 repository Python
    tests, baseline provenance verification with 278 intentional differences
    and 69 dependency inputs, legacy exclusion across 3,972 tracked paths, 62
    CMake files, 1,254 compile commands, and 1,971 Ninja edges, and staged
    `git diff --check` pass. Focused coverage proves
    reservation exclusion, cancellation recovery, commit rotation, old-token
    rejection, replacement-token acceptance, and stale-operation rejection.
  - Owner review: architecture approval received on 2026-09-02; implementation
    demo remains required before Slice 7.6 can become **Implemented**.
  - Follow-ups: add canonical hidden lifecycle transactions, compose live
    disconnect/resume/expiry delivery, extend the headless client/process proof,
    and run applicable verification.

- 2026-09-02 — Slice 7.5 legacy-exclusion verification repair — Implemented
  - Change: removed the ambiguous `tes3mp-server` product target spelling from
    the legacy-token denylist while retaining archived path and legacy
    dependency rejection; added allow/reject regression contracts.
  - Decisions: target names are not provenance. Archived source paths and
    RakNet/CrabNet/CoreScripts/packet-processor inputs remain fail closed.
  - Verification: all 115 repository Python tests pass. Staged legacy exclusion
    passes across 3,972 tracked paths, 62 CMake files, 62 compile commands, and
    182 Ninja edges. Staged baseline verification passes with 278 intentional
    differences and 69 dependency inputs; staged diff checks pass.
  - Owner review: Slice 7.5 implementation-demo acceptance received before this
    verification repair.
  - Follow-ups: none for Slice 7.5.

- 2026-09-02 — Slice 7.5 owner implementation-demo acceptance — Implemented
  - Change: accepted implementation `f8b1acee4b`; Slice 7.5 advanced to
    **Implemented**.
  - Decisions: no accepted ADR/GDR decision changed.
  - Verification: owner reviewed the real two-client movement, convergence,
    stale-view rejection, and focused atomic-overflow evidence.
  - Owner review: implementation-demo acceptance received on 2026-09-02.
  - Follow-ups: Slice 7.6 is next and remains **Not Started**.

- 2026-09-02 — Slice 7.5 real movement proof — In Progress
  - Change: composed decoded `PlayerMotionIntent` through live server intake and
    extended the real two-client process proof with both clients authoring
    motion, complete-view convergence, and stale-view rejection.
  - Decisions: implements accepted ADR-0044; no architecture, authority,
    state-scope, or gameplay behavior decision changed.
  - Verification: MSVC 19.51 Release `tes3mp_server_app_tests_run`,
    `tes3mp_protocol_tests_run`, and `tes3mp_headless_client_tests_run` pass.
    `python scripts/run_phase7_join_demo.py --server
    build/slice74-runtime/tes3mp-server/tes3mp_server.exe --client
    build/slice74-runtime/tes3mp-headless-client/tes3mp_headless_client.exe`
    reports two distinct identities, simultaneous movement, converged views,
    and stale-view rejection. All 114 repository Python tests pass; staged
    baseline verification passes with 278 intentional differences and 69
    dependency inputs; staged diff checks pass. The legacy-exclusion checker
    currently rejects the active vNext `tes3mp-server` CMake target name as a
    legacy token, so that check is not green for this candidate.
  - Owner review: implementation-demo acceptance remains pending.
  - Follow-ups: repair or clarify the legacy-exclusion target-name rule; owner
    reviews the movement proof and focused atomic-overflow contract; then Slice
    7.5 may advance to **Implemented**.

- 2026-09-02 — Slice 7.5 movement tick composition — In Progress
  - Change: added accepted ADR-0044; implemented one prepared command/integration transaction, checked position arithmetic, revisioned spatial-tick publication, targeted complete same-cell views, and atomic multi-target output admission.
  - Decisions: owner approved Option A for one failure-atomic command/integration/view tick and Option A for targeted complete same-cell latest-wins views sequenced by `ServerTick`.
  - Verification: fresh MSVC 19.51 `tes3mp_server_app_tests_run`, `tes3mp_protocol_tests_run`, and `tes3mp_headless_client_tests_run` pass; focused contracts cover checked integration, revision advance, complete view projection, and whole-tick overflow rollback. All 114 repository Python tests, baseline provenance verification with 277 intentional differences and 69 dependency inputs, legacy exclusion, and `git diff --check` pass.
  - Owner review: explicit A/A approval received on 2026-09-02.
  - Follow-ups: extend the real two-client process proof with simultaneous movement, convergence, stale-view rejection, and overflow failure; obtain owner demo acceptance.

- 2026-09-02 — Slice 7.4 owner implementation-demo acceptance — Implemented
  - Change: accepted implementation `bd35f2d60c`; Slice 7.4 advanced to
    **Implemented** in the live tracker.
  - Decisions: no accepted ADR/GDR decision changed.
  - Verification: owner reviewed the recorded real two-client
    interior/exterior/interior proof after focused C++ targets, all 114 Python
    tests, baseline provenance verification, and diff checks passed.
  - Owner review: implementation-demo acceptance received on 2026-09-02.
  - Follow-ups: none for Slice 7.4. Slice 7.5 remains **Not Started**.

- 2026-09-02 — Slice 7.4 runtime composition and real fixture proof — In Progress
  - Change: composed joins and fixture transitions through one canonical reducer
    writer; added typed join publication, move-only reducer preparation, bounded
    atomic multi-target delivery, live transition dispatch, client lifecycle
    intake, and the real two-client interior/exterior/interior proof.
  - Decisions: owner approved server-app writer ownership, reducer
    prepare/commit, one shared join/transition writer, typed `SessionJoined`
    changes, and bounded multi-frame atomic delivery; ADR-0043 records them.
  - Verification: fresh MSVC 19.51 `tes3mp_protocol_tests_run`,
    `tes3mp_server_app_tests_run`, and `tes3mp_headless_client_tests_run` pass.
    The verified-manifest Release real-process build and
    `python scripts/run_phase7_join_demo.py --server
    build/slice74-runtime/tes3mp-server/tes3mp_server.exe --client
    build/slice74-runtime/tes3mp-headless-client/tes3mp_headless_client.exe`
    pass with distinct identities and explicit leave/enter convergence. All 114
    repository Python tests, baseline verification with 277 intentional
    differences and 69 dependency inputs, and `git diff --check` pass.
  - Owner review: all architecture/state-scope clarifications approved before
    implementation; implementation-demo acceptance remains pending.
  - Follow-ups: owner demo acceptance; then mark Slice 7.4 **Implemented**.

- 2026-09-01 — Slice 7.4 client observation application clarification — Approved
  - Change: recorded the owner-approved contract in ADR-0043 and implemented
    bounded atomic reliable-observation application in the reusable client
    session and headless wrapper.
  - Decisions: owner approved Option A; wrong-target, stale, and contradictory
    batches fail atomically, while identical duplicates are harmless.
  - Verification: fresh MSVC 19.51 `tes3mp_headless_client_tests_run` passes,
    including target/generation/tick, duplicate, contradiction, bounded-state,
    and failure-atomic contracts; all 114 repository Python tests,
    `python scripts/verify_vnext_baseline.py`, and `git diff --check` pass.
  - Owner review: explicit Option A approval received on 2026-09-01.
  - Follow-ups: runtime composition and the real two-client fixture-transition
    demo remain.

- 2026-09-01 — Slice 7.4 observation projection and admission — In Progress
  - Change: added bounded deterministic before/after canonical-state projection
    of per-session enter/leave batches and complete same-cell views, plus one
    atomic reliable/latest queue-admission operation per affected target.
  - Decisions: implements accepted ADR-0043 and its observation-composition
    clarification; no new architecture, authority, state-scope, or gameplay
    behavior decision was made.
  - Verification: MSVC 19.51 `tes3mp_server_app_tests_run` passes; focused
    contracts cover two affected targets, exact leave sets, complete targeted
    views, unchanged-state no-op, paired frame order, and capacity failure
    admitting neither frame. Repository Python, provenance, and diff checks are
    recorded in the finishing commit.
  - Owner review: Slice 7.4 implementation demo remains pending.
  - Follow-ups: compose projection after the canonical transition, apply both
    frame types in the headless client, and run the real two-client demo.

- 2026-09-01 — Slice 7.4 reliable observation wire root — In Progress
  - Change: added a distinct bounded server-to-client reliable observation-batch
    schema, owned codec, frame kind, generated header, and focused contracts.
  - Decisions: owner approved Option A: keep server lifecycle observations out
    of the client-command `ReliableOperation` root; ADR-0043 records the gate.
  - Verification: fresh MSVC 19.51 `tes3mp_protocol_tests_run` and all 114
    repository-owned Python tests pass. The pinned header was generated by
    repository `flatc 25.12.19`; a full proof rerun was blocked by its
    pre-existing cached MSVC 19.44 compiler path paired with the installed 19.51
    standard library. Staged baseline verification and diff checks pass.
  - Owner review: wire-root architecture approved; Slice 7.4 demo remains pending.
  - Follow-ups: deterministic projection, atomic observation/view admission,
    client application, and real two-client demo.

- 2026-09-01 — Slice 7.4 observation composition clarification — Approved
  - Change: recorded the owner-approved reliable observation-batch and targeted
    complete-view atomic queue-admission boundary in ADR-0043.
  - Decisions: owner approved Option A. Each affected target gets one bounded
    reliable typed enter/leave batch paired atomically with its latest view;
    encoding or capacity failure admits neither frame.
  - Verification: documentation-only gate; `git diff --cached --check` passed.
  - Owner review: explicit Option A approval received on 2026-09-01.
  - Follow-ups: implement the typed observation codec, deterministic projection,
    atomic pair admission, client application, and real two-client demo.

- 2026-09-01 — Slice 7.4 fixture-transition protocol — In Progress
  - Change: implementation `9380315cd3`; extended the existing bounded reliable-operation union with an
    owned typed fixture-cell transition and exact interior/exterior cell codec;
    regenerated the committed header with pinned `flatc 25.12.19`.
  - Decisions: implements accepted ADR-0043 Decision 1; no new architecture,
    authority, state-scope, or gameplay decision was made.
  - Verification: MSVC 19.44 `tes3mp_protocol_tests_run` passed, including
    both fixed fixture round trips; `python
    scripts/run_vnext_flatbuffers_proof.py`, all 114 Python tests, `python
    scripts/verify_vnext_baseline.py`, and `git diff --check` passed.
  - Owner review: implementation demo remains pending; Slice 7.4 remains
    **In Progress**.
  - Follow-ups: decide the exact approved observation protocol/composition
    shape with the owner before implementing atomic projection and delivery,
    then run the required real two-client demo.

- 2026-09-01 — Slice 7.4 fixture-transition reducer — In Progress
  - Change: added the typed fixture-cell transition command payload and composed
    it through existing bounded intake and apply-once canonical reduction.
    Known interior `7` and exterior worldspace `8` grid `0,0` transitions
    atomically replace cell/revision and acknowledgement; unknown and same-cell
    requests finalize without spatial mutation.
  - Decisions: implements the accepted ADR-0043 reducer and fixture decisions;
    no architecture, authority, state-scope, or gameplay decision changed.
  - Verification: fresh MSVC 19.51 standalone build target
    `tes3mp_protocol_tests_run` passed, including the focused reducer executable;
    all 114 Python tests, `python scripts/verify_vnext_baseline.py`, and
    `git diff --check` passed.
  - Owner review: implementation demo not yet requested; Slice 7.4 remains
    **In Progress**.
  - Follow-ups: add bounded protocol exchange, explicit enter/leave observation
    changes, targeted complete views, failure-atomic delivery, and the required
    real two-client demo.

- 2026-09-01 — Slice 7.4 architecture gate — Approved; implementation not started
  - Change: added accepted ADR-0043; no Slice 7.4 production artifact landed.
  - Decisions: owner approved Option A for the reliable apply-once reducer
    command, explicit reliable enter/leave plus targeted complete views, fixed
    interior `7` and exterior worldspace `8` grid `0,0` fixtures, and one atomic
    writer commit/publication boundary.
  - Verification: documentation-only change; `git diff --cached --check` and
    staged baseline verification apply before commit.
  - Owner review: explicit A/A/A/A approval received on 2026-09-01.
  - Follow-ups: begin Slice 7.4 implementation in the next session; move it to
    **In Progress** when the first non-disposable artifact lands.

- 2026-09-01 — Slice 7.3 owner implementation-demo acceptance — Implemented
  - Change: accepted implementation `39c6c72095`; Slice 7.3 advanced to
    **Implemented** in the live tracker.
  - Decisions: no accepted decision changed.
  - Verification: the owner reviewed the recorded real process proof after all
    focused targets, 114 Python tests, baseline verification, and diff checks
    passed.
  - Owner review: implementation-demo acceptance received on 2026-09-01.
  - Follow-ups: Slice 7.4 is next and remains separately architecture-gated.

- 2026-09-01 — Slice 7.3 real client process and identity proof — In Progress
  - Change: implementation `39c6c72095`; added the thin
    `tes3mp_headless_client` production process, added
    canonical player ownership to bounded spatial snapshot entries, and added a
    real server/two-client process demo with a preceding rejected credential.
  - Decisions: owner approved Option A for thin client-process composition and
    Option A for player identity in the initial spatial snapshot entry. ADR-0041
    and ADR-0042 record both clarifications.
  - Verification: MSVC 19.44 builds `tes3mp_server`,
    `tes3mp_headless_client`, `tes3mp_protocol_exchange_tests`, and
    `tes3mp_authenticated_join_tests`; focused codec and join contracts pass.
    Full `tes3mp_protocol_tests_run`, `tes3mp_server_app_tests_run`, and
    `tes3mp_headless_client_tests_run` targets pass. All 114 repository-owned
    Python tests pass; staged baseline verification passes with 272 intentional
    differences and 67 dependency inputs; pinned `flatc 25.12.19` regenerated
    the committed snapshot header; `git diff --cached --check` passes.
    `python scripts/run_phase7_join_demo.py --server
    build/slice63-crypto-gns/tes3mp-server/tes3mp_server.exe --client
    build/slice63-crypto-gns/tes3mp-headless-client/tes3mp_headless_client.exe`
    passes after one rejected credential and reports session/player/entity IDs
    `1/1/1` and `2/2/2`, proving no identity was consumed by the failure.
  - Owner review: architecture and state-scope approvals received before
    implementation; implementation-demo acceptance remains pending.
  - Follow-ups: owner implementation-demo review; then mark Slice 7.3
    **Implemented** and begin separately gated Slice 7.4.

- 2026-09-01 — Slice 7.3 live transport pump and production composition — In Progress
  - Change: wired accepted/closed events, bounded receive/dispatch, pending
    authentication progress, and outbound queue pumping through the thin server
    application; production `main` now owns and lends the approved dependency
    bundle and fixed Phase 7 proof profile.
  - Decisions: owner approved Option A for production ownership and Option A for
    protocol 1.0.0, empty optional capabilities, 4/32 one-second authentication
    buckets, eight connections, and grace-mapped token/session lifetimes.
  - Verification: MSVC 19.44 builds `tes3mp_server`; focused
    `tes3mp_server_app_tests_run` passes live accept/hello/send and malformed-lane
    fail-closed cleanup plus exact proof-profile boundary assertions;
    `tes3mp_headless_client_tests_run` passes. All 114 repository-owned Python
    tests pass; baseline verification passes with 269 intentional differences
    and 67 dependency inputs; `git diff --check` passes.
  - Owner review: architecture and security/profile approvals received before
    dependent production implementation; process-demo acceptance remains pending.
  - Follow-ups: add/compose a real scripted-client process entry point and run
    the required two-client authentication, distinct-identity, complete-snapshot,
    and injected-failure demo.

- 2026-09-01 — Slice 7.3 bounded live frame dispatch — In Progress
  - Change: implementation `d366a5078a`; the app-local connection coordinator
    now validates and dispatches
    bounded reliable handshake/authentication frames through its owned session,
    admission scope, resume context, atomic join composition, initial session
    binding, and outbound queue.
  - Decisions: owner approved Option A on 2026-09-01. The coordinator owns
    per-connection dispatch; `ServerApplication` remains the thin pump.
  - Verification: MSVC 19.44 builds and runs
    `tes3mp_server_app_tests_run`; focused contracts cover hello response,
    authenticated atomic join, preissued binding, ordered authentication plus
    latest snapshot output, and wrong-lane fail-closed rejection. All 114
    repository-owned Python tests pass; staged baseline verification passes
    with 268 intentional differences and 67 dependency inputs; `git diff
    --cached --check` passes.
  - Owner review: architecture approval received before implementation; Slice
    7.3 process-demo acceptance remains pending.
  - Follow-ups: wire transport accepted/closed/receive events and queue pumping
    through `ServerApplication`, then run the real two-client process demo.

- 2026-09-01 — Slice 7.3 resume-context derivation — In Progress
  - Change: added the server-app helper that hashes the canonical negotiated
    `ServerHello` and fixed versioned Phase 7 fixture-content identifier into
    the typed resume-token context; failures return no context.
  - Decisions: owner approved Option A on 2026-09-01 before implementation;
    [`ADR-0042`](adr/ADR-0042-phase7-authenticated-join-and-identity-allocation.md)
    records the clarification.
  - Verification: MSVC 19.44 focused `tes3mp_server_app_tests_run` passes;
    contracts prove deterministic same-context output, protocol-change
    sensitivity, exact fixture identifier input, and fail-closed digest failure.
    All 114 repository-owned Python tests, baseline provenance verification
    with 266 intentional differences and 67 dependency inputs, and
    `git diff --check` pass.
  - Owner review: derivation decision approved; Slice 7.3 demo acceptance
    remains pending live frame pumping and the two-client process flow.
  - Follow-ups: feed the derived context through bounded live authentication,
    join composition, queue pumping, and the required two-client demo.

- 2026-09-01 — Slice 7.3 preissued session binding — In Progress
  - Change: authenticated join composition now returns the committed bounded
    identity result, and an established server session can record that
    already-issued initial session identifier exactly once without issuing a
    second resume credential.
  - Decisions: owner approved preissued-session binding Option A on 2026-09-01
    after live wiring exposed the double-issuance conflict. Wrong-stage and
    duplicate bindings fail closed.
  - Verification: MSVC 19.44 builds and runs `tes3mp_session_state_tests` and
    `tes3mp_server_app_tests`; all 114 repository-owned Python tests pass;
    baseline provenance passes with 266 intentional differences and 67
    dependency inputs; `git diff --check` passes.
  - Owner review: architecture option approved before implementation; process
    demo acceptance remains pending.
  - Follow-ups: dispatch bounded live frames, invoke the committed binding, pump
    outbound queues, and run the two-client process demo.

- 2026-09-01 — Slice 7.3 live-session composition — In Progress
  - Change: added the bounded app-local connection coordinator skeleton owning
    accepted server sessions, admission scopes, and outbound queue lifetime.
  - Decisions: owner approved Option A on 2026-09-01; transport lifecycle state
    remains outside `server_core` and `ServerApplication` remains a thin pump.
  - Verification: MSVC 19.44 focused `tes3mp_server_app_tests_run` passes;
    contracts cover accepted ownership, duplicate and capacity rejection,
    session state, admission-scope retention, and queue/session cleanup. All 114
    repository-owned Python tests and `git diff --check` pass.
  - Owner review: architecture option approved before implementation; process
    demo acceptance remains pending.
  - Follow-ups: dispatch bounded handshake/authentication frames, invoke atomic
    join composition, pump outbound queues, and run the two-client process demo.

- 2026-09-01 — Slice 7.3 atomic transport admission — In Progress
  - Change: added domain-neutral atomic two-frame admission to each bounded
    outbound connection queue and bound join authentication/snapshot responses
    to it through the server-app adapter. Also corrected the stale GDR-0001
    register status to **Implemented**.
  - Decisions: the project owner approved Option A on 2026-09-01. Pair
    admission stages both lanes before commit; capacity or unknown-connection
    failure admits neither and the join coordinator cancels without canonical
    mutation.
  - Verification: MSVC 19.44 builds and runs
    `tes3mp_transport_queue_tests` and `tes3mp_server_app_tests`; focused tests
    prove successful two-lane admission, second-lane capacity rejection without
    a partial latest frame, unknown-connection rejection, and zero canonical
    bindings after rejected transport admission. All 114 repository-owned
    Python tests pass. Baseline provenance passes with 264 intentional
    differences and 67 dependency inputs; `git diff --check` passes.
  - Owner review: architecture Option A approved before implementation. Full
    Slice 7.3 process-demo acceptance remains pending.
  - Follow-ups: drive negotiation and authentication from live per-connection
    server sessions, pump admitted queues, and run the real two-client demo.

- 2026-09-01 — Slice 7.3 response composition — In Progress
  - Change: added the app-owned authenticated-join composition that prepares
    canonical state, issues the initial resume token, encodes both protocol
    frames, atomically enqueues the response pair, and commits last.
  - Decisions: implements accepted ADR-0042 composition Option A. The response
    queue owns all-or-neither admission; no app-owned canonical mutation or
    rollback path was added.
  - Verification: MSVC 19.44 focused `tes3mp_server_app_tests_run` passes with
    two distinct joins plus token and queue rejection proving zero canonical
    bindings before a later successful retry. `tes3mp_protocol_tests_run` and
    all 114 repository-owned Python tests pass; `git diff --check` passes.
  - Owner review: accepted architecture controls this work. Slice demo
    acceptance remains pending real transport wiring and the two-client process
    flow.
  - Follow-ups: bind the atomic response queue to per-connection bounded
    transport queues, drive it from authenticated session completion, and run
    the required real two-client demo.

- 2026-09-01 — Slice 7.3 atomic composition boundary — In Progress
  - Change: added a bounded coordinator-owned prepare/commit/cancel join
    transaction. Preparation validates and constructs the complete candidate
    state and targeted snapshot without canonical mutation; only an exact live
    preparation can commit, and cancellation leaves identities reusable.
  - Decisions: the project owner approved Option A on 2026-09-01: prepare in
    `server_core`, issue the token and encode/enqueue all responses in
    composition, then commit without app-owned mutation or rollback.
  - Verification: MSVC 19.44 focused
    `tes3mp_authenticated_join_tests_run` passes; the complete
    `tes3mp_protocol_tests_run` target passes; all 114 repository-owned Python
    tests pass; staged baseline verification and `git diff --check` pass.
  - Owner review: architecture clarification approved before implementation.
    Slice implementation demo acceptance remains pending the real two-client
    authentication/token/transport flow.
  - Follow-ups: compose token issuance, frame encoding, bounded transport
    enqueue/send, and commit; inject token/encoding/first-send/second-send
    failures and demonstrate no orphan canonical state.

- 2026-09-01 — Slice 7.3 — In Progress
  - Change: added accepted
    [`ADR-0042`](adr/ADR-0042-phase7-authenticated-join-and-identity-allocation.md)
    and began the authenticated atomic-join slice.
  - Decisions: owner approved A/A/A/A/A/A: server-core coordinator, checked
    monotonic process-lifetime identities, atomic player/session install, one
    live binding per principal, fixed authoritative spawn plus complete targeted
    snapshot, and fail-closed rejection without partial canonical state.
  - Verification: fresh standalone MSVC 19.44 compilation and
    `tes3mp_authenticated_join_tests_run` pass. Focused contracts cover two
    distinct atomic joins, fixed zero-velocity fixture spawn, complete targeted
    initial snapshots, duplicate-principal rejection, final-valid identity then
    exhaustion, capacity exhaustion, and unchanged state after rejection.
    Adjacent canonical-state, server-authentication, and session-state test
    executables pass. All 114 repository-owned Python tests pass; baseline
    provenance verification passes with 258 intentional differences and 67
    dependency inputs. `git diff --check` passes.
  - Owner review: architecture, authority, state-scope, and fixture behavior
    approval received on 2026-09-01 before production implementation.
  - Follow-ups: compose the approved boundary with real authentication, token
    finalization, and transport, then run and demonstrate the two-client Slice
    7.3 flow; the slice remains **In Progress** until that evidence and owner
    demo acceptance pass.

- 2026-09-01 — Slice 7.2 — Implemented
  - Change: added accepted
    [`ADR-0041`](adr/ADR-0041-phase7-headless-client-and-script-driver.md)
    and began the headless façade/script-driver slice.
  - Decisions: owner approved A/A/A/A/A/A: caller-pumped production façade,
    borrowed runtime and one connection, bounded typed polling, test-support
    driver, fixed-capacity typed scripts, and credential-free bounded NDJSON.
  - Verification: fresh standalone MSVC 19.44 compilation and
    `tes3mp_headless_client_tests_run` pass. Focused contracts cover duplicate
    start rejection, matching encrypted establishment, typed session action,
    fixed script capacity, bounded credential-free NDJSON, close, and
    fail-closed transport polling. The existing complete protocol target and
    isolated target-boundary build remain green; staged baseline verification
    passes with 258 intentional differences and 67 dependency inputs.
  - Owner review: architecture approval received before implementation. The
    owner accepted the connection-establishment, typed session/snapshot seam,
    identical script replay, capacity rejection, bounded timeline, and
    transport-failure closure demo after candidate `a4e983f5f0`.
  - Follow-ups: none for Slice 7.2. Slice 7.3 is next and remains separately
    authority/state-scope/gameplay gated.

- 2026-09-01 — Slice 7.1 — Implemented
  - Change: added accepted
    [`ADR-0040`](adr/ADR-0040-phase7-server-composition-and-configuration.md)
    and began the bounded dedicated-server composition/configuration slice.
  - Decisions: the owner approved A/A/A/A/A/A: app-owned thin composition,
    single caller thread, strict closed `key = value` configuration, five-key
    initial scope, file-only bounded secret, and transactional startup/orderly
    shutdown.
  - Verification: fresh standalone MSVC 19.44 configure/build and the complete
    `tes3mp_protocol_tests_run` plus focused `tes3mp_server_app_tests_run`
    targets pass. The exact verified GNS/OpenSSL 3.5.8 dependency tree builds
    the production `tes3mp_server` executable; a real loopback run binds
    `127.0.0.1:25565`, reports `server started`, handles Ctrl-C, and reports
    `server stopped`. Focused contracts cover bounded parsing, secret loading,
    injected listener/poll failure, and exact `LPSX` lifecycle order. Full
    Python suite passes all 114 tests, including 24 focused target-boundary and
    runtime-safety policy tests. Staged baseline verification passes with 252
    intentional differences and 67 dependency inputs; local Markdown links,
    JSON parsing, status assertions, and `git diff --cached --check` pass.
  - Owner review: architecture approval received on 2026-09-01. The owner
    accepted the valid startup/stop, invalid configuration/secret, injected
    listener-failure, and ordered-shutdown implementation demo after candidate
    `d85d3d07f4`.
  - Follow-ups: none for Slice 7.1. Slice 7.2 is next and remains separately
    architecture-gated.

- 2026-09-01 — Slice 7.0 — Implemented
  - Change: added accepted
    [`GDR-0001`](gdr/GDR-0001-phase7-headless-vertical-slice-behavior.md) and
    advanced Phase 7 to **In Progress** and Slice 7.0 to **Implemented**.
  - Decisions: the project owner approved Option A for all six decisions:
    server fixture spawn/snapshot readiness, validated fixture-cell requests,
    same-ready-cell visibility, checked fixed-tick provisional integration,
    newer-snapshot replacement without prediction, and hidden bounded-grace
    resume with live-duplicate rejection and fresh creation after expiry.
  - Verification: all 114 repository-owned Python tests pass; staged baseline
    verification passes with 244 intentional differences and 67 dependency
    inputs; JSON parsing, local Markdown links, record/status assertions, and
    `git diff --cached --check` pass.
  - Owner review: explicit A/A/A/A/A/A approval received on 2026-09-01,
    including the named acceptance scenarios and demo boundary.
  - Follow-ups: Slice 7.1 is next. GDR-0002 through GDR-0004 retain later
    lifecycle, cell/interest/resync, and production movement decisions.

- 2026-09-01 — Phase 7 kickoff — Approved; implementation not started
  - Change: the project owner approved the Phase 7 kickoff after the accepted
    Phase 6 exit. No Phase 7 production artifact or behavior decision landed.
  - Decisions: kickoff approval covers the declared headless two-client outcome,
    slice order, dependencies, and risks. GDR-0001 authority, state-scope,
    contention, reconnect, and named acceptance scenarios remain explicitly
    pending for a fresh session.
  - Verification: documentation-only record; the Phase 6 exact-candidate gates
    and repository verification recorded above remain green.
  - Owner review: Phase 7 kickoff approved on 2026-09-01. This is not approval
    of GDR-0001 or authorization for dependent production implementation.
  - Follow-ups: prepare and review the GDR-0001 decision packet in a fresh
    session. Phase 7 and Slice 7.0 remain **Not Started** until that approval is
    recorded.

- Keep the initial world fixture deliberately tiny: player identity, session,
  canonical cell, root transform, velocity, revision, and acknowledgement state.
- Fake-client scripts should emit a machine-readable timeline so CI failures can
  be replayed from seed and command trace.
- Define quantitative pass thresholds for queue depth, correction/convergence,
  resume time, and soak duration in the test configuration rather than prose-only
  expectations.

## Phase 8 — OpenMW desktop vertical slice

### 2026-09-03 — Phase 8 C-R1 implementation accepted and exit gate closed

- Status: **Implemented**. Slices 8.2–8.7 and Phase 8 are complete; Phase 9 is
  next but remains **Not Started**.
- Owner review: the project owner explicitly accepted C-R1 implementation
  `93690354d1` and its retained real-content evidence on 2026-09-03.
- Exit decision: the accepted desktop flow covers the engine-specific boundary;
  the same engine-independent protocol, transport, and client-session runtime
  already passes the approved deterministic adverse-network matrix from Phase
  7, and OpenMW adds no alternate network/session path. Independent target
  boundary tests still build without OpenMW, while P8-001 through P8-004 remain
  fully documented and registry-verified. All three Phase 8 exit conditions are
  therefore satisfied without waiving or changing a threshold.
- Evidence: implementation `93690354d1`; `c-r1-final-5` two-client movement and
  interior/exterior leave-return; 32/32 resumes; two successful 60-second soak
  clients; unchanged RSS-window rule; reliable/latest queue high water within
  cap and zero final depth; 4/4 focused engine tests; adapter contracts; 137/137
  repository-owned Python tests; 342-entry staged provenance with 69 dependency
  inputs; 4,021-path staged legacy exclusion; and clean staged diff/registry
  checks.
- Boundaries retained: server authority, client-local replica lifetime, C-R1's
  default-deny subsystem matrix, memory-only credentials, platform-neutral
  protocol/session targets, and no legacy source remain unchanged.
- Follow-up: begin Phase 9 Slice 9.1 with read-only PC OpenMW-VR maintenance
  research and a complete ADR-0008 owner decision packet. Do not select a fork,
  create the worktree/patch target, change production code, or choose VR
  subsystem behavior before owner approval.

### 2026-09-03 — C-R1 replicated actor implemented and locally verified

- Status: **In Progress**; the approved implementation and complete local
  evidence pass, while owner acceptance remains.
- Change: replaced the transient renderer proxy with the first-class C-R1
  replicated-actor role. The renderer now owns a separate 255-entry collection,
  a non-interactive visibility mask, read-only deterministic NPC appearance,
  passive local idle animation, and typed create/update/lifecycle results. The
  desktop provider owns one RAII handle per `EntityId`, rejects contradictory
  same-revision state, treats field-identical re-timestamped observations as
  idempotent, advances animation from the injected monotonic clock, and uses
  capacity-sized allocation-free reconcile scratch space. The old NPC custom-
  data initializer delta and transient proxy are removed.
- Boundaries: no protocol, canonical state, authority, normal `CellStore` /
  `WorldModel` registration, gameplay scene insertion, mechanics, collision,
  navigation, Lua/scripts, persistence, interaction, sound, gameplay particles,
  stats, spells, AI, gameplay inventory, generated reference identity, or game
  PRNG was added. Internal typed diagnostics map to the existing sanitized
  connection statuses.
- Verification: the MSVC 19.51 RelWithDebInfo `openmw.exe`, adapter test target,
  and `openmw-tests` target build. `ReplicatedActor.*` passes 4/4; the adapter
  executable passes; replicated-actor source contracts pass 6/6; desktop harness
  contracts pass 7/7; all 137 repository-owned Python tests pass. Patch-registry
  verification, staged provenance with 342 intentional differences and 69
  dependency inputs, staged legacy exclusion across 4,021 paths / 62 CMake
  files / 1,254 compile commands / 1,971 Ninja edges, and staged diff hygiene
  pass. The retained `c-r1-final-5` run passes both real-content
  cell flows, 32/32 resumes, both 60-second soak clients, the unchanged RSS-
  window rule, reliable high water 3 messages / 324 bytes, latest high water 1
  message / 396 bytes, and zero final queue depth. Final RSS medians are
  536,868,864 and 540,196,864 client bytes and 13,791,232 server bytes.
- Findings: sequential proof phases must drain the server's three-second
  disconnect grace before reusing one server. Re-timestamping an unchanged
  entity revision is projection metadata, not a new motion sample; field-exact
  validation preserves contradiction rejection. Per-view heap churn in desired-
  set reconciliation was removed after retained RSS samples exposed an 84 KiB
  allocator drift against an unusually narrow 16 KiB reference window.
- Owner review: C-R1 architecture was approved on 2026-09-03. Implementation
  acceptance and any Phase 8 status advance remain pending.
- Follow-up: review the C-R1 diff and retained evidence. If accepted, record
  owner acceptance and decide whether the shared demonstration closes Slices
  8.2–8.7 and the Phase 8 exit gate; otherwise amend only the reviewed surface.

### 2026-09-03 — Remote actor owner decision packet prepared

- Status: **In Progress**; bounded architecture research is complete and owner
  approval remains required before production implementation.
- Decisions: none. The
  [owner decision packet](REMOTE_ACTOR_OWNER_DECISION_PACKET.md) compares the
  current renderer proxy, a normal actor with exclusions, and a first-class
  ephemeral replicated actor across authority, identity, lifetime,
  registration, rendering, animation, mechanics, physics, scripts,
  persistence, interaction, errors, migration, VR, and operations. It
  recommends package C-R1: a default-deny replica with rendering and passive
  renderer-local idle animation only.
- Findings: OpenMW's normal active-scene insertion fans a registered pointer
  into rendering, mechanics, water, particles, physics/navigation, and Lua.
  The current proxy bypasses that fan-out, but full NPC initialization registers
  nested inventory pointers and consumes the world PRNG; renderer insertion
  also installs inventory listeners and exposes a normal `PtrHolder` to focus
  and activation rays. Its `NpcAnimation` is not owned by a mechanics
  `CharacterController`, so normal animation advancement is not supplied by the
  nominal renderer-only path.
- Verification: read-only current-source inspection at `c507eb01fd`, pinned
  OpenMW 0.51.0 baseline confirmation at `f4bec41444`, read-only
  `tes3mp-0.8.1-archive` inspection, licensed `Morrowind.esm` `player` record
  inspection with the existing `esmtool`, `git diff --check`, documentation
  link/status review, JSON parse and sorted 332-entry provenance check, 9/9
  focused baseline/patch-registry unit tests, and patch-registry verification.
  Baseline provenance names the new research document. No production source,
  accepted record, patch registry, plan status, or subsystem behavior changed.
- Owner review: packet prepared for review; approval pending.
- Follow-up: owner chooses or amends the packet. Only after explicit approval,
  amend ADR-0007/ADR-0050/GDR-0013, replace P8-004, implement the approved exact
  patch surface, and run all named real-content and negative-registration gates.

### 2026-09-03 — C-R1 remote actor package approved

- Status: **In Progress**; the architecture gate is cleared, while replacement
  implementation and complete evidence remain.
- Owner decision: the project owner explicitly approved package C-R1. The
  authoritative observed set owns lifetime; the adapter owns one opaque RAII
  handle per `EntityId`; the renderer owns a separate capacity-bounded replica
  collection. Rendering and passive renderer-local neutral idle are the only
  Phase 8 subsystem participation.
- Exclusions: no `CellStore`, `WorldModel`, scene, mechanics, physics,
  navigation, Lua/scripts, persistence, interaction, sound, gameplay particles,
  gameplay stats/inventory/AI/spells, generated reference identity, or gameplay
  PRNG. A distinct visibility role must remain outside focus/activation and
  intersection traversal while participating in the approved main-camera,
  shadow, reflection, and ToggleWorld paths.
- Patch policy: ADR-0007, ADR-0050, GDR-0013, the plan, and P8-004 are amended
  only to the exact packet surface. Any discovered need outside that surface
  returns to owner review.
- Follow-up: implement the C-R1 replacement, migrate the desktop provider, run
  all typed-error, lifecycle, negative-registration, build, registry,
  provenance, and real-content two-client gates, and keep Phase 9 gated.

[Back to the phase tracker](IMPLEMENTATION_PLAN.md#phase-8--openmw-desktop-vertical-slice)

### 2026-09-03 — Replicated-remote-actor research approved

- Status: **In Progress**; no replacement architecture or production code is
  approved yet.
- Decision: the owner selected focused Option B research. The next work designs
  a first-class, protocol-agnostic replicated remote actor and compares it with
  the current renderer-only proxy and legacy-style normal actor. ADR-0007,
  ADR-0050, and GDR-0013 record the reopened boundary and retained constraints.
- Scope: map identity, lifetime, engine registration, rendering, animation,
  mechanics, physics, scripts, persistence, interaction, error reporting, and
  real-content testing. Preserve server authority, main-thread application,
  client-local replica state, engine-independent protocol/session targets, and
  the patch registry.
- Gate: present concrete alternatives and a recommendation before changing the
  actor role, subsystem participation, gameplay behavior, or production hooks.
  Do not copy legacy code or reproduce scattered `isDedicatedPlayer` checks.
- Verification: documentation-only approval annotation; prior archive/current-
  source findings and green 128-test repository evidence remain current.
- Follow-up: perform the bounded OpenMW 0.51 subsystem-boundary audit, draft the
  replacement decision packet, and request owner approval before implementation.

[Back to the phase tracker](IMPLEMENTATION_PLAN.md#phase-8--openmw-desktop-vertical-slice)

### 2026-09-03 — Remote-replica architecture review reopened

- Status: **In Progress**; production correction is paused at an owner decision
  gate. ADR-0050, GDR-0013, and P8-004 require review before more remote-avatar
  implementation.
- Legacy finding: archived TES3MP 0.8.1 represented each remote player as a
  real dynamically created NPC reference placed in the world. It reused normal
  movement, animation, equipment, spells, death, collision, cell movement, and
  map-marker facilities while overwriting state from network data. Containing
  local simulation then required `DedicatedPlayer` checks across 16 OpenMW
  engine files spanning classes, mechanics, combat, spellcasting, physics,
  scripts, inventory, and world movement.
- Current finding: vNext avoided that cross-cutting model by specifying a
  renderer-only transient NPC. The implementation nevertheless constructs an
  `MWWorld::ManualRef`, initializes NPC stats, spells, race powers, AI packages,
  inventory and auto-equipment, consumes the engine PRNG, and attaches an
  inventory listener before rendering. The real-content failure is collapsed
  to one generic presentation error, and current automated tests do not create
  the concrete provider against real content.
- Decision gate: choose between retaining the renderer-only proxy, restoring a
  legacy-style normal actor with exclusions, or designing a first-class
  ephemeral replicated-actor role with explicit subsystem participation. The
  last option is recommended for research, but no role model, subsystem scope,
  or behavior has been selected.
- Verification: read-only `git show`/`git grep` inspection of the permanent
  `tes3mp-0.8.1-archive` tag and current OpenMW 0.51 source; the archive contains
  16 engine files that directly classify dedicated players. No legacy packet,
  processor, or implementation was copied.
- Follow-up: owner selects review scope. Then inventory OpenMW 0.51 registration
  boundaries and approve authority, lifecycle, subsystem-participation, and
  compatibility contracts before rewriting decisions or production code.

[Back to the phase tracker](IMPLEMENTATION_PLAN.md#phase-8--openmw-desktop-vertical-slice)

### 2026-09-03 — Slice 8.7 first content-backed run

- Status: **In Progress**; the flow fails before bounded evidence completes.
- Finding: licensed Steam content appeared at the default path after the prior
  search. The first process attempt exposed missing runtime DLL search paths;
  using the pinned dependency `bin` directories on `PATH` makes the executable
  smoke test pass. The next attempt loaded `Morrowind.esm` but lacked
  `Morrowind.bsa`, so remote meshes were unavailable. The runner now accepts
  explicit repeatable `--fallback-archive` values and its focused contract
  covers `Morrowind.bsa`. With that archive mounted, both clients reach
  `Seyda Neen, Census and Excise Office`, then the adapter closes with the
  sanitized `remote player presentation failed` reason while using approved
  avatar record `player`.
- Decisions: none beyond the already approved mapping. No alternate NPC record,
  presentation fallback, or broader diagnostic surface was chosen.
- Verification: `openmw.exe --version` passes with pinned dependency paths;
  `python -m unittest scripts.tests.test_phase8_desktop_harness -v` passes 4/4;
  real two-client attempts reproduce the missing-archive failure and then the
  post-load remote-presentation failure. No completion artifact was emitted.
- Follow-up: owner review is required before trying a different avatar record or
  changing presentation/diagnostic behavior. After approval, isolate the
  failure, rerun flow/reconnect/soak, and retain credential-free evidence.

[Back to the phase tracker](IMPLEMENTATION_PLAN.md#phase-8--openmw-desktop-vertical-slice)

### 2026-09-03 — Slice 8.7 demo mapping approval

- Status: **In Progress**; licensed content, process evidence, and owner demo
  acceptance remain.
- Decision: the owner approved Option A: interior cell
  `Seyda Neen, Census and Excise Office`, exterior worldspace `sys::default`,
  and remote avatar NPC `player`. GDR-0013 records the exact mapping and review
  trigger. No production behavior, architecture, authority, or state changed.
- Verification: an exact-name read-only search across local C:, D:, F:, and G:
  drives found no `Morrowind.esm`; therefore the records and content-backed flow
  could not be validated and no demo claim was made.
- Follow-up: provide a licensed Morrowind `Data Files` path, validate the three
  approved records, and run the complete desktop harness. Do not substitute
  different content records without owner review.

[Back to the phase tracker](IMPLEMENTATION_PLAN.md#phase-8--openmw-desktop-vertical-slice)

### 2026-09-03 — Slice 8.7 content-backed demo preflight

- Status: **In Progress**; the content-backed harness and owner demo remain.
- Finding: the current RelWithDebInfo `openmw.exe` is present and the matching
  `tes3mp_server.exe` rebuild passes. The focused desktop-harness contracts pass
  all four tests. Local Steam libraries and the checked conventional install
  paths contain no Morrowind installation or `Morrowind.esm`, so the licensed
  content-backed process run cannot begin on this machine yet.
- Decisions: none. Accepted ADR-0053 and GDR-0013 through GDR-0016 remain
  unchanged; no architecture, authority, state scope, gameplay behavior, or
  production source changed.
- Verification: MSVC 19.51 RelWithDebInfo `tes3mp_server` rebuild passed in
  `build/slice82-openmw-full`; `python -m unittest
  scripts.tests.test_phase8_desktop_harness -v` passed 4/4 tests; both required
  executables exist in the same configuration directory.
- Follow-up: provide licensed Morrowind data plus the approved interior,
  worldspace, and fixture-avatar record identifiers, run
  `scripts/run_phase8_desktop_demo.py`, and obtain owner acceptance. Phase 9
  remains gated until the Phase 8 exit gate passes.

[Back to the phase tracker](IMPLEMENTATION_PLAN.md#phase-8--openmw-desktop-vertical-slice)

### 2026-09-03 — Slice 8.7 approved reconnect automation implementation

- Status: **In Progress**; content-backed harness run and owner demo remain.
- Decisions: owner approved recommended Option A. ADR-0053 places bounded
  reconnect supervision and test-only automation in the app-local adapter;
  GDR-0016 requires immediate remote clearing, multiplayer-input suppression,
  and a complete-snapshot continuity barrier before resumed presentation.
- Change: the coordinator now retains one rotating memory-only token and its
  original deadline, owns one sequential runtime attempt, retries failed
  pre-authentication connects no faster than once per second, and never replays
  a submitted token or silently performs a fresh join. Resume validates session,
  player, entity, generation, entity revision, and acknowledgement continuity.
  A `BUILD_TESTING`-only typed OpenMW driver and external process harness cover
  two-client movement/cell flow, 32 resumes, 60-second soak, client/server RSS,
  bounded queue drain, and credential-free evidence. No new OpenMW hook,
  protocol, canonical state, authority, or persistence was added.
- Verification: focused MSVC 19.51 Debug adapter/reconnect and token-recovery
  contracts, full protocol aggregate, headless-client contracts, all 128
  repository-owned Python tests, patch-registry verification, staged baseline
  provenance with 331 intentional differences and 69 dependency inputs,
  staged legacy exclusion over 4,018 paths, 62 CMake files, 1,254 compile
  commands, and 1,971 Ninja edges, diff hygiene, and networking-enabled
  RelWithDebInfo `openmw.exe` links with `BUILD_TESTING` on and off pass.
- Commit: `4735938383` (`Implement Phase 8 desktop reconnect automation`).
- Follow-up: run the content-backed desktop harness with licensed game data and
  obtain owner acceptance before marking Slice 8.7 Implemented or closing the
  Phase 8 exit gate.

[Back to the phase tracker](IMPLEMENTATION_PLAN.md#phase-8--openmw-desktop-vertical-slice)

### 2026-09-03 — Slice 8.7 decision-gate research

- Status: **Not Started**; no production implementation began.
- Finding: the desktop coordinator owns one transport/runtime attempt, does not
  retain the server-issued resume token, and closes permanently after disconnect.
  Presentation clears correctly, but there is no resume attempt or resumed
  complete-snapshot barrier. The repository also has no two-process OpenMW
  driver; existing adapter contracts and Phase 7 process proofs do not satisfy
  the real-desktop automation gate.
- Decision gate: the owner must approve reconnect/runtime ownership, bounded
  retry and visible presentation behavior, and a test-only typed desktop driver
  plus external artifact-capturing harness. These affect architecture and
  player-visible disconnect behavior. The approved Phase 7 lifecycle remains
  server-authoritative and resume credentials remain memory-only.
- Proposed records: ADR-0053 for desktop reconnect and automation composition;
  GDR-0016 for provisional disconnect/resume presentation. No new OpenMW hook,
  protocol schema, canonical state, authority, or persistence is proposed.
- Verification: focused source and accepted-decision inspection, all 124
  repository-owned Python tests, patch-registry verification, and diff hygiene
  pass. Prior production verification remains unchanged pending owner approval.

[Back to the phase tracker](IMPLEMENTATION_PLAN.md#phase-8--openmw-desktop-vertical-slice)

### 2026-09-03 — Slice 8.6 approved remote smoothing implementation

- Status: **In Progress**; owner two-client desktop demo remains.
- Decisions: owner approved the recommended Option A package. ADR-0052 records
  provider-owned bounded buffering and adapter-owned typed metrics; GDR-0015
  records provisional two-tick interpolation, three-tick extrapolation, bounded
  correction, and hard-snap behavior.
- Change: added a pure four-sample per-entity remote-motion buffer driven each
  frame by the injected monotonic clock. It interpolates position, caps
  authoritative-velocity extrapolation at 100 ms, holds afterward, blends
  corrections through 16 OpenMW units over two ticks, and hard-snaps larger or
  discontinuous state. Desktop presentation owns and clears buffers with remote
  renderer handles. Closed adapter-local metrics report snapshot age, depth,
  extrapolation, correction distance, and hard snaps without identity fields.
- Verification: focused MSVC 19.51 Debug adapter/smoother contracts, the full
  protocol aggregate, headless-client contracts, all 124 repository-owned
  Python tests, patch-registry verification, staged baseline provenance with
  325 intentional differences and 69 dependency inputs, staged legacy
  exclusion, diff hygiene, and the networking-enabled RelWithDebInfo
  `openmw.exe` build/link pass.
- Commit: `ceb8e11d1f` (`Implement Phase 8 remote movement smoothing`).
- Follow-up: run and obtain owner acceptance for the content-backed two-client
  desktop smoothing/convergence demo before marking Slice 8.6 Implemented.

[Back to the phase tracker](IMPLEMENTATION_PLAN.md#phase-8--openmw-desktop-vertical-slice)

### 2026-09-03 — Slice 8.6 decision-gate research

- Status: **Not Started**; no production implementation began.
- Finding: remote avatars currently move only when a newer authoritative
  snapshot is applied. The presentation provider owns the renderer-only avatar
  handles, but its contract has no per-frame clock input or bounded sample
  history. The existing avatar `update` seam is sufficient; no deeper OpenMW
  hook appears necessary.
- Decision gate: the owner must approve smoothing ownership, client-local state
  scope, fixture delay/extrapolation/correction thresholds, discontinuity and
  reset behavior, and metric ownership. These are presentation/gameplay and
  architecture choices; Phase 12 still owns production locomotion tuning.
- Verification: source and accepted-decision inspection, all 124
  repository-owned Python tests, patch-registry verification, and diff hygiene
  pass. The prior Slice 8.5 automated evidence remains current.

[Back to the phase tracker](IMPLEMENTATION_PLAN.md#phase-8--openmw-desktop-vertical-slice)

### 2026-09-03 — Slice 8.5 approved movement implementation

- Status: **In Progress**; owner two-client desktop demo remains.
- Decisions: owner approved all recommended Option A choices. ADR-0051 records
  provisional spatial-intent concurrency and observed-revision semantics;
  GDR-0014 records deterministic planar mapping and exact same-cell correction.
- Change: desktop action state now maps through normalized yaw-rotated input to
  4,096 quanta/tick with explicit ties-to-even rounding. The coordinator bounds
  motion to one pending command plus one latest desired intent. Ordered motion
  and fixture-transition commands retain session, generation, entity, epoch,
  identity, sequence, and domain validation without impossible exact revision
  equality. Newer same-cell snapshots hard-correct local position through the
  existing position-only OpenMW move API. No engine hook was added. P8-004
  registry coverage and previously stale provenance entries/hashes were also
  repaired.
- Verification: focused reducer/property and movement/coordinator contracts,
  full protocol aggregate, headless-client and adapter contracts, all 124
  repository-owned Python tests, patch-registry verification, staged baseline
  provenance with 321 intentional differences and 69 dependency inputs, diff
  hygiene, and the full networking-enabled RelWithDebInfo `openmw.exe` link
  pass.
- Commit: `c4b9cb1cec` (`Implement Phase 8 desktop movement`).
- Follow-up: run and obtain owner acceptance for the content-backed two-client
  desktop movement/convergence demo before marking Slice 8.5 Implemented.

[Back to the phase tracker](IMPLEMENTATION_PLAN.md#phase-8--openmw-desktop-vertical-slice)

### 2026-09-03 — Slice 8.5 decision-gate research

- Status: **Not Started**; no production implementation began.
- Finding: the existing adapter can read desktop action state and use the
  existing same-cell player move API without a deeper OpenMW hook. However,
  each non-zero server movement tick advances the entity revision while every
  motion command requires the client's exact observed revision. With more than
  one tick of network delay, a changed or zero/stop intent can therefore remain
  stale and fail to converge.
- Decision gate: the owner must approve command concurrency/precondition
  behavior, desktop control-to-velocity mapping, and authoritative local
  correction behavior. GDR-0001/GDR-0012 do not authorize OpenMW engine use or
  correction tuning, and Phase 12 still owns production locomotion.
- Verification: source/decision inspection only; the prior Slice 8.4 automated
  evidence remains current and no test claim changed.

[Back to the phase tracker](IMPLEMENTATION_PLAN.md#phase-8--openmw-desktop-vertical-slice)

### 2026-09-03 — Slice 8.4 approved package and P8-004 implementation

- Status: **In Progress**; owner two-client demo remains.
- Decisions: owner approved the breaking package recorded by ADR-0050,
  GDR-0013, ADR-0042/ADR-0047 amendments, and registry entry P8-004.
- Change: snapshots now carry explicit target player/entity identity; runtime
  queue calls return command-sequence receipts; the coordinator captures cell
  events before correction and bounds pending/deferred transitions; fixture
  content mapping is explicit; remote avatars reconcile through an opaque
  renderer-only transient NPC seam with typed failure cleanup.
- Verification: the full protocol aggregate, headless-client contracts, adapter
  contracts, patch registry, all 124 repository Python tests, and full
  RelWithDebInfo `openmw.exe` build/link pass. Diff hygiene passes. The
  content-backed two-client owner demo remains.

[Back to the phase tracker](IMPLEMENTATION_PLAN.md#phase-8--openmw-desktop-vertical-slice)

- 2026-09-03 — Slice 8.3 visible runtime status — Verified
  - Change: the executable status provider now presents each closed sanitized
    runtime-failure category through a normal transient OpenMW message box and
    retains the matching error log. UI failure is contained by the provider's
    `noexcept` boundary. Startup validation still fails before engine launch.
  - Decisions: the owner approved Option A on 2026-09-03. The existing
    main-thread status-provider path owns presentation; no new engine hook,
    status queue, authority, state scope, or gameplay behavior was added.
  - Verification: the focused visible/sanitized-source contract passes; all 124
    repository-owned Python tests and patch-registry verification pass; the
    networking-enabled MSVC 19.51 RelWithDebInfo build recompiles `main.cpp` and
    links `build/slice82-openmw-full/RelWithDebInfo/openmw.exe`; diff checks pass.
  - Owner review: implementation demo acceptance remains pending, so Slices 8.2
    and 8.3 remain **In Progress**.
  - Follow-ups: present the two-client desktop implementation demo.

- 2026-09-02 — Slice 8.3 desktop connection composition — In Progress
  - Change: added explicit-disabled OpenMW configuration, separate validated
    host/port, required 1–60,000 ms timeout, bounded password-file loading,
    GNS-backed runtime startup, executable coordinator attachment, and closed
    sanitized startup/runtime status categories. Empty providers intentionally
    defer cell presentation and movement input to Slices 8.4 and 8.5.
  - Decisions: ADR-0049 records owner-approved Option A for enablement,
    endpoint, credentials, errors, and timeout. P8-002 is now registered.
  - Verification: focused Debug adapter contracts, all 120 repository Python
    tests, patch-registry verification, and RelWithDebInfo OpenMW without the
    real-network profile pass. The real-network build corrected a Windows
    lean-header/GNS timer declaration conflict, then confirmed the approved
    `/MD` policy requires an `/MD` OpenMW dependency bundle; the currently
    installed `/MT` bundle cannot link and is not accepted as passing evidence.
  - Follow-ups: provision and verify the approved `/MD` OpenMW dependency
    bundle, finish the real-network executable proof, add visible OpenMW status
    presentation, and obtain the Slice 8.2/8.3 implementation demo acceptance.

- 2026-09-02 — Slice 8.2 complete client orchestrator correction — Verified
  - Change: implemented owner-approved breaking Option D. The reusable runtime
    now owns handshake/authentication progression, initial binding,
    snapshot/observation application, command envelope sequencing, bounded
    queues, and failure closure. The headless proof caller no longer assembles
    protocol/session transitions. The concrete OpenMW coordinator core owns
    transport/clock/runtime lifetimes and uses the approved frame order.
  - Decisions: ADR-0047 records the amendment. Endpoint, credential acquisition,
    OpenMW enablement, and UI-error policy remain Slice 8.3 decisions.
  - Verification: fresh MSVC 19.51 Debug headless-runtime and adapter contracts
    pass; the RelWithDebInfo OpenMW adapter compiles; the real two-client proof
    passes movement, convergence, stale-view rejection, 32 reconnects,
    expiration/fresh identity, and zero queue drain. All 120 Python tests,
    staged baseline provenance with 303 intentional differences and 69 inputs,
    staged legacy exclusion, patch-registry verification, and diff checks pass.
  - Owner review: architecture approved before implementation; implementation
    demo acceptance remains pending.
  - Follow-ups: configuration-gated executable composition after Slice 8.3
    endpoint/authentication/UI decisions, then the desktop demo.

- 2026-09-02 — Slice 8.2 headless runtime caller migration — Verified
  - Change: migrated the headless executable from direct transport polling and
    frame decoding to the reusable `ClientSessionRuntime` typed inbound drain,
    bounded queue, and outbound flush. Empty pre-connect flush is an idle
    success; queued work without a connection remains a failure.
  - Decisions: no new decision. This implements the owner-approved ADR-0047
    runtime boundary and preserves its correct-then-command order.
  - Verification: fresh MSVC 19.51 Debug focused and full protocol contracts,
    the GNS headless/server build, all 120 repository-owned Python tests, and
    `git diff --check` pass. The real-process lifecycle proof reaches the known
    ADR-0048 gate and rejects a different same-tick snapshot as expected.
  - Follow-ups: complete the accepted ADR-0048 domain/wire migration, rerun the
    real-process proofs, then compose the concrete OpenMW coordinator.

- 2026-09-02 — Slice 8.2 canonical revision separation — Verified
  - Change: implemented ADR-0048 with a distinct one-per-authoritative-commit
    `CanonicalRevision`, retained per-change canonical state versions and fixed
    `ServerTick` pacing, migrated command/snapshot/observation domain and wire
    fields, and regenerated pinned FlatBuffers headers.
  - Decisions: owner approved the breaking A3 design: `ServerTick` paces
    simulation; `CanonicalRevision` orders every authoritative commit,
    publication, and observed command base.
  - Verification: MSVC 19.51 Debug full protocol, server-app, headless-runtime,
    and OpenMW adapter contracts pass; two joins in one simulation tick receive
    increasing revisions and a multi-change command batch advances one revision.
    Pinned FlatBuffers generation/proof, all 120 Python tests, RelWithDebInfo
    `openmw`, and the real two-client 32-cycle lifecycle/convergence/queue-drain
    proof pass.
  - Follow-ups: compose the concrete OpenMW coordinator within approved ADR-0047.

- 2026-09-02 — Slice 8.2 full-build blocker correction — Verified
  - Change: replaced the stale full-engine build directory with a fresh MSVC
    configuration using the pinned Windows dependency bundle. No dependency or
    bundle-lock change was needed.
  - Decisions: owner approved replacing and relocking the complete dependency
    bundle if needed. Inspection and fresh configuration proved that the current
    bundle already supplies Bullet 3.25 with double precision, so rebuilding an
    identical bundle would add no value.
  - Verification: fresh configure reports `Bullet uses double precision`; the
    MSVC 19.51 RelWithDebInfo `openmw` target compiles and links to `openmw.exe`.
  - Follow-ups: none for Bullet. Continue Slice 8.2 caller composition.

- 2026-09-02 — Slice 8.2 reusable runtime boundary — In Progress
  - Change: added the owner-approved `ClientSessionRuntime`. It owns the reusable
    session and bounded outbound queue, separates typed inbound drain from
    encode/queue/outbound flush, validates frame/channel pairing, and closes on
    protocol or transport failure. Headless and OpenMW caller migration remain.
  - Decisions: owner approved new owning runtime Option A; expanding the
    headless-only class and a borrowing pump helper were rejected.
  - Verification: fresh MSVC 19.51 Debug `tes3mp_headless_client_tests`, full
    `tes3mp_protocol_tests_run`, and `openmw_tes3mp_adapter_tests_run` pass. All
    120 repository-owned Python tests and `git diff --check` pass. Focused tests
    prove typed decode, queue-before-flush order, transport-channel selection,
    protocol rejection, and failure closure.
  - Owner review: runtime boundary approved before implementation. Slice 8.2
    demo acceptance remains pending.
  - Follow-ups: migrate the headless executable, compose the concrete OpenMW
    coordinator, and finish lifecycle/frame-order failure contracts.

- 2026-09-02 — Slice 8.2 provider and engine-seam foundation — In Progress
  - Change: added separate domain-typed input/presentation provider contracts,
    the optional single-owner engine coordinator seam, exact post-input frame
    call, ordered teardown, focused contracts, and the first machine-checked
    OpenMW semantic patch registry.
  - Decisions: owner approved the reusable client-session runtime Option A.
    This foundation adds no packet handling to the adapter and no gameplay
    mapping; runtime composition remains in Slice 8.2.
  - Verification: focused MSVC 19.51 adapter contracts, all 120 repository-owned
    Python tests, patch-registry schema and exact frame/shutdown-order contracts,
    staged provenance with 299 intentional differences and 69 dependency inputs,
    staged legacy exclusion, and diff checks pass. A later fresh full-engine
    configuration and RelWithDebInfo `openmw` build pass with the same pinned
    dependency bundle and confirm double-precision Bullet.
  - Owner review: provider split, frame order, and reusable runtime architecture
    are approved. Implementation-demo acceptance remains pending.
  - Follow-ups: implement the reusable typed client-session runtime, concrete
    coordinator composition, and remaining lifecycle/failure contracts.

- 2026-09-02 — Slice 8.2 frame order — Approved; implementation gated
  - Change: recorded the approved correct-then-command frame order in ADR-0047.
    Repository inspection found the reusable headless session exposes one
    combined `pump()` and cannot express separate inbound and outbound passes.
  - Decisions: owner approved bounded inbound drain, authoritative apply,
    semantic input sample, command queue, then bounded outbound flush. No code
    chooses the newly exposed session-pump seam yet.
  - Verification: direct inspection of `headless_client_session.hpp/.cpp`
    confirms `pump()` owns combined transport polling and event handling.
  - Owner review: exact frame order and reusable runtime Option A approved on
    2026-09-02.
  - Follow-ups: implement the approved reusable runtime without adapter packet
    ownership.

- 2026-09-02 — Slice 8.2 provider boundary — In Progress
  - Change: added ADR-0047 and advanced Slice 8.2 to **In Progress**; production
    code remains paused at the exact frame-order gate.
  - Decisions: owner approved two narrow borrowed semantic-input and
    presentation providers with coordinator-owned session lifecycle. Combined
    facade and loose callbacks were rejected.
  - Verification: all 117 repository-owned Python tests, documentation links,
    ADR register status, and diff checks pass.
  - Owner review: provider architecture Option A approved on 2026-09-02. Exact
    drain/presentation/input ordering remains pending because it affects
    correction and command behavior.
  - Follow-ups: approve frame order and reusable pump seam, then implement named
    lifecycle/provider contracts and the approved P8-001 through P8-003 seams.

- 2026-09-02 — Slice 8.1 exact-hook inventory — Implemented
  - Change: amended accepted ADR-0007 with exact P8-001 through P8-003 engine,
    executable-composition, and target-wiring seams plus their needs, evidence,
    dispositions, and removal conditions.
  - Decisions: owner approved Option A: executable-created concrete adapter,
    engine-owned narrow coordinator, one optional main-thread call immediately
    after input update and before Lua/world mutation, and coordinator teardown
    before OpenMW dependencies. No deeper gameplay or presentation hook is
    authorized.
  - Verification: the inventory maps every approved non-adapter path to tests
    and patch policy; all 117 repository-owned Python tests and diff checks pass.
  - Owner review: explicit Option A and exact-inventory approval received on
    2026-09-02.
  - Follow-ups: Slice 8.2 implements only this approved lifecycle/frame boundary
    and separately reviews provider-interface choices before production code.

- 2026-09-02 — Slice 8.1 exact-hook research — In Progress
  - Change: inspected only the accepted ADR-0007 boundaries, existing adapter
    anchor/target, OpenMW target composition, `Engine` lifetime, and frame order.
    Prepared the exact hook packet for owner review; no engine code changed.
  - Decisions: pending owner choice. The recommended minimum is executable-side
    concrete adapter composition, one engine-owned narrow coordinator interface,
    one optional main-thread call immediately after input update and before Lua
    or world mutation, and coordinator destruction before OpenMW subsystems.
  - Verification: repository inspection confirms the adapter currently contains
    only an anchor, depends inward on `openmw-lib` and client-session, and is not
    linked into `openmw`; `Engine::frame` provides the required post-input slot.
    All 117 repository-owned Python tests and `git diff --check` pass for the
    documentation transition.
  - Owner review: exact path/call-site approval remains required by ADR-0007
    before implementation or patch-registry entries land.
  - Follow-ups: present options and acceptance checks; after approval, amend
    ADR-0007 with the exact inventory and complete Slice 8.1 evidence.

- 2026-09-03 — Slice 8.3 Windows `/MD` dependency and executable-link gate — Verified
  - Change: rebuilt the exact locked OpenSSL 3.5.8, Protobuf 33.4/Abseil
    20250512.1, GameNetworkingSockets 1.6.0, and c-ares 1.34.8 inputs with the
    dynamic MSVC runtime, regenerated the verified transport manifest, and
    rebuilt the networking-enabled OpenMW target. Corrected two stale recipe
    overrides (`protobuf_MSVC_STATIC_RUNTIME` and `CARES_MSVC_STATIC_RUNTIME`)
    plus the fail-closed component lock hashes.
  - Decisions: none. This implements ADR-0049's already approved one-`/MD`
    Windows network runtime policy; dependency identities and restricted
    surfaces are unchanged.
  - Verification: both exact dependency proofs pass under MSVC 19.51; direct
    object inspection reports `MD_DynamicRelease`/`MSVCRT`; the full
    RelWithDebInfo real-network build links
    `build/slice82-openmw-full/RelWithDebInfo/openmw.exe`; focused OpenMW adapter,
    GNS transport, and credential-crypto contracts pass. Twenty-three proof
    runner tests, the lock-hash synchronization regression, all 123
    repository-owned Python tests, patch-registry verification, and diff checks
    pass.
  - Owner review: the dependency/runtime executable-link gate is closed. Slice
    8.2/8.3 owner-demo acceptance and visible status UI remain separate gates.
  - Follow-ups: complete the visible status UI and present the owner demo.

- The adapter translates between OpenMW state/events and owned domain types. It
  must not become a second canonical simulation or contain packet handling.
- Rendering and local input continue if the network stalls; network callbacks
  must not mutate OpenMW presentation state from arbitrary threads.
- Remote player representation may be minimal in this phase. Correct lifecycle,
  cell placement, and root motion take priority over animation fidelity.
- Keep client-session integration reusable so PC VR and a potential Quest client
  compose the same state machine rather than fork it.

## Phase 9 — PC VR interoperability gate

[Back to the phase tracker](IMPLEMENTATION_PLAN.md#phase-9--pc-vr-interoperability-gate)

- 2026-09-03 — Slice 9.1 — **In Progress**
  - Change: completed read-only maintained-fork research and prepared the full
    proposed [ADR-0008](adr/ADR-0008-pc-vr-fork-worktree-maintenance-policy.md)
    owner decision packet. No remote, branch, worktree, registry schema,
    dependency lock, merge, or production code was changed.
  - Decisions: pending owner review. The packet recommends immutable OpenMW-VR
    tag `openmw-vr-0.51-rc1` at `56a8e01390507375c9c2f2593e1c09e0df88c505`,
    a same-repository `vnext-vr` merge-overlay branch in a sibling worktree, and
    the retained vNext dependency locks plus an exact OpenXR-SDK commit lock.
  - Verification: `git status --short --branch`; `git worktree list
    --porcelain`; exact `git ls-remote` queries against OpenMW, OpenMW-VR, and
    Khronos OpenXR-SDK; GitLab project/ref/merge-base/compare/pipeline APIs;
    GitHub Actions run/job/artifact APIs; SHA-256 verification of the exact VR
    source archive; SHA-512 verification of the fork `m1.0` Windows bundle;
    SHA-256 verification of OpenXR-SDK commit
    `1ca7bec6b531185530c9b4f1e7a50e1fd55e7641`; and local VR/desktop path-
    collision comparison. The exact VR tag passed upstream GitHub Ubuntu and
    Windows 2022 jobs; its GitLab pipeline had passing Linux and Windows MSBuild
    jobs but failing Windows Ninja jobs. The VR tree overlaps 10 registered P8
    paths, all named in ADR-0008. Local relative-link and trailing-whitespace
    checks, `git diff --check`, `python scripts/verify_openmw_patch_registry.py`,
    and `python -m unittest scripts.tests.test_openmw_patch_registry` pass.
  - Owner review: requested for ADR-0008 Decisions 1 through 3. No approval is
    inferred from this packet.
  - Follow-ups: after explicit approval, create and verify only the approved
    Slice 9.1 remote/branch/worktree, provenance, registry, and baseline proof.

- 2026-09-03 — Slice 9.1 ADR-0008 package approved
  - Change: owner approved the proposed maintenance package; implementation has
    not yet been accepted.
  - Decisions: ADR-0008 Decision 1 Option A, Decision 2 Option A, and Decision 3
    Option A are approved exactly as presented.
  - Verification: approval recorded in ADR-0008; no implementation evidence yet.
  - Owner review: explicit `approved` response in the 2026-09-03 working session.
  - Follow-ups: create the approved target, stop on any new architecture or
    behavior choice, and run the proposed Slice 9.1 acceptance gates.

- 2026-09-03 — Slice 9.1 implementation candidate — **In Progress**
  - Change: added read-only remote `openmw-vr-upstream` with push URL disabled;
    created same-repository sibling worktree `TES3MP-vr` on `vnext-vr`; merged
    exact tag commit `56a8e01390507375c9c2f2593e1c09e0df88c505` in two-parent
    commit `0e8e85e0b59cd746018b0124754cf18db87b4c20`; and recorded provenance,
    all 10 P8 overlap paths, all six merge conflicts, complete non-adapter
    OpenMW delta ownership, and machine verification in
    `c590e81cc5b5ce3a3175b1d42c680e8f599f2c96`.
  - Decisions: owner approved merge-composition Option A after the shared
    `main.cpp` conflict exposed a target-link boundary: desktop composition is
    excluded under `OPENMW_VR` only until Slice 9.2 adds the approved real VR
    composition. No authority, canonical state, protocol, pose, input,
    locomotion, interaction, or gameplay behavior was selected.
  - Dependency proof: OpenXR-SDK is pinned to commit
    `1ca7bec6b531185530c9b4f1e7a50e1fd55e7641`, archive SHA-256
    `afc4c7c59dc0e427f03fc655e84d4394eb2d6070630924a63e547e4055ab816d`,
    and Apache-2.0 license SHA-256
    `cfc7749b96f63bd31c3c42b5c471bf756814053e847c10f3eb003417bc523d30`.
    The repository-owned preparer verifies before extraction, fails closed,
    retries safely, and passed a second `--offline` cache-reuse run.
  - Build verification: fresh Visual Studio 18 2026 / MSVC 19.51 / Windows SDK
    10.0.26100 RelWithDebInfo configuration and build from the verified local
    OpenXR source linked `openmw_vr.exe` (77,351,424 bytes, SHA-256
    `283e8974ddee1c696381315f02138b624873896a2f7f28653f3a4dae4324bc1c`).
    The only compiler warning was inherited fork C4244 in
    `mwvr/vrinputmanager.cpp`. Imported upstream whitespace remains upstream
    history; all new local staged diffs passed `git diff --check`.
  - Regression verification: VR verifier plus six acquisition/target tests
    pass at `ff04803ec222d169343b019b2bbc87d8658af282`, including proof that a
    failed non-mutating merge rehearsal leaves `vnext-vr` unchanged. On
    authoritative desktop `vnext`, patch-registry verification and all
    137 repository-owned Python tests pass in 50.977 seconds, including legacy
    exclusion. Desktop does not depend on the VR remote, worktree, or cache.
  - Owner review: implementation evidence is ready; Slice 9.1 remains **In
    Progress** pending explicit owner acceptance.
  - Follow-ups: accept Slice 9.1, then present Slice 9.2 architecture/build
    options before adding VR multiplayer composition.

- 2026-09-03 — Slice 9.1 owner implementation acceptance — **Implemented**
  - Change: recorded owner acceptance of the verified PC VR maintenance target,
    reproducible build proof, and failed-update rehearsal safety evidence.
  - Decisions: none. This closure accepts only the ADR-0008 maintenance and
    temporary composition boundaries already approved. It adds no authority,
    canonical state, protocol, pose, input, locomotion, interaction, or gameplay
    behavior.
  - Verification: current `vnext` and `vnext-vr` worktrees are clean; the desktop
    patch registry and its three focused tests pass; the VR target verifier and
    its four focused tests pass; the read-only VR remote still has push disabled;
    and current desktop commit `9c81e62380` is present in `vnext-vr` history.
  - Owner review: explicit Slice 9.1 implementation acceptance received on
    2026-09-03.
  - Follow-ups: present Slice 9.2 architecture/build options and obtain owner
    approval before adding VR multiplayer composition.

- 2026-09-03 — Slice 9.2 dual-engine composition proposal — **Not Started**
  - Change: inspected the accepted VR target's executable, adapter, provider,
    connection, engine-hook, and CMake seams; added proposed
    [ADR-0054](adr/ADR-0054-phase9-dual-engine-adapter-composition.md). No
    production code or build target changed.
  - Decisions: pending owner review. Option A is recommended: one shared
    adapter/composition target, separate desktop and future VR provider targets,
    and fail-closed explicit VR enable until Slice 9.4 supplies approved
    providers. Options B and C respectively use preprocessor-selected monolithic
    composition or two complete adapter libraries.
  - Finding: the VR tree already compiles the shared adapter and accepted P8-001
    engine hook, but only desktop links concrete composition. Reusing
    `DesktopSemanticInput` in VR would implicitly select locomotion behavior and
    is therefore outside Slice 9.2.
  - Verification: repository inspection only; `git diff --check` and relative
    document-link validation apply to this proposal. Slice 9.2 stays **Not
    Started** because documentation alone is not its named deliverable.
  - Owner review: requested for ADR-0054 Option A, B, or C and the proposed
    acceptance tests. No approval is inferred.
  - Follow-ups: after explicit approval, record the decision and implement only
    the selected target/composition boundary on `vnext`, merge that commit into
    `vnext-vr`, and run both supported build gates.

- 2026-09-03 — Slice 9.2 ADR-0054 Option A approved — **In Progress**
  - Change: recorded owner approval of the dual-engine target/composition
    boundary and proposed acceptance tests. Production implementation begins
    only after this gate record.
  - Decisions: one shared adapter/composition target, separate desktop and
    future VR provider targets, and fail-closed VR enable until approved Slice
    9.4 providers exist.
  - Verification: documentation-only gate; `git diff --check` and relative-link
    validation pass.
  - Owner review: explicit `a approved` response on 2026-09-03.
  - Follow-ups: implement on `vnext`, merge the shared commit into `vnext-vr`,
    and run desktop/VR build and boundary gates without adding gameplay behavior.

- Head and hand transforms are presentation snapshots associated with a player
  root and authority epoch. They are not persisted as durable world state.
- Physical reach validation uses the authoritative root plus declared limits;
  raw tracked coordinates are never accepted as authoritative interaction proof.
- OpenMW-VR stays outside the multiplayer core. Fork-specific code belongs in the
  provider/hook layer and maintained patch target.

## Phase 10 — Player lifecycle and content identity

[Back to the phase tracker](IMPLEMENTATION_PLAN.md#phase-10--player-lifecycle-and-content-identity)

- Content identity must not use local file paths. Canonical record references need
  stable manifest context and explicit collision handling.
- Moderation primitives belong in canonical server commands now; the secure
  administrative interface that invokes them arrives in Phase 21.
- Avoid persisting transport connection identifiers or secrets as player identity.

## Phase 11 — Canonical cells, interest, and resynchronization

[Back to the phase tracker](IMPLEMENTATION_PLAN.md#phase-11--canonical-cells-interest-and-resynchronization)

- Snapshot completion needs an explicit baseline revision/tick. Do not infer
  completion from a quiet connection.
- Interest changes are server decisions. Clients may suggest view context only
  through validated, bounded commands.
- Resync should be scoped to the smallest sound state domain, with a full session
  snapshot as the bounded fallback.

## Phase 12 — Production movement, animation, and pose replication

[Back to the phase tracker](IMPLEMENTATION_PLAN.md#phase-12--production-movement-animation-and-pose-replication)

- Snapshot payloads include simulation tick, sequence, transform, velocity, and
  authority epoch. They are latest-wins and must not use reliable ordered queues.
- Measure snapshot age, correction distance, hard snaps, jitter-buffer depth, and
  extrapolation time before tuning rates or compression.
- Teleport/cell-transition correction is a distinct explicit state change, not an
  extreme ordinary movement snapshot.

## Phase 13 — Actor lifecycle, AI state, and authority handoff

[Back to the phase tracker](IMPLEMENTATION_PLAN.md#phase-13--actor-lifecycle-ai-state-and-authority-handoff)

- Prefer server-owned actor simulation. Client delegation requires the explicit
  threat analysis and validation limits required by ADR-0006.
- A lease transfer commits a complete authoritative snapshot atomically before
  accepting deltas from the new epoch.
- Presentation animation is not permission to drive canonical AI or transform
  state outside the authority policy.

## Phase 14 — Interactive objects, locks, traps, and doors

[Back to the phase tracker](IMPLEMENTATION_PLAN.md#phase-14--interactive-objects-locks-traps-and-doors)

- Separate durable object state from presentation events such as sound or
  animation triggers. Replaying a snapshot must not replay one-shot effects.
- Use expected entity revision plus command ID for reliable interactions.
- VR reach is validated relative to the authoritative player root and declared
  limits, never solely from controller pose.

## Phase 15 — Inventory, equipment, and container transactions

[Back to the phase tracker](IMPLEMENTATION_PLAN.md#phase-15--inventory-equipment-and-container-transactions)

- Treat an inventory change as a domain transaction with one command ID and one
  commit result, not a sequence of independently reliable packet mutations.
- Explicitly define stack equivalence and metadata that prevents unsafe merging.
- Persistence later stores canonical ownership/revisions, not client UI layout or
  transport acknowledgement state unless needed for bounded idempotency recovery.

## Phase 16 — Combat, stats, magic, death, and resurrection

[Back to the phase tracker](IMPLEMENTATION_PLAN.md#phase-16--combat-stats-magic-death-and-resurrection)

- Keep command intent, canonical outcome, and presentation event separate. A hit
  visual is not evidence that canonical damage occurred.
- Combat validation must use server-known state and explicit lag-handling policy
  from ADR-0006; do not silently trust client timestamps.
- Add threat-model cases for rate abuse, forged targets, impossible reach, stale
  authority, resource bypass, and replayed commands.

## Phase 17 — Dialogue, journals, factions, and quests

[Back to the phase tracker](IMPLEMENTATION_PLAN.md#phase-17--dialogue-journals-factions-and-quests)

- This phase implements canonical state and commands, not the general-purpose
  server scripting runtime. It must still emit typed domain events for Phase 19.
- Dialogue presentation stays client-side while dialogue eligibility and durable
  consequences use validated canonical state.
- Record intentional differences from single-player behavior as product rules,
  not undocumented race outcomes.

## Phase 18 — Time, weather, and durable world state

[Back to the phase tracker](IMPLEMENTATION_PLAN.md#phase-18--time-weather-and-durable-world-state)

- Separate canonical transition parameters from client-local visual interpolation.
- Use typed keys/values or schema-defined records for globals; do not expose an
  unbounded arbitrary network dictionary.
- Server maintenance pause and no-player-online policy must be explicit because
  they affect later persistence/replay behavior.

## Phase 19 — Versioned server scripting

[Back to the phase tracker](IMPLEMENTATION_PLAN.md#phase-19--versioned-server-scripting)

- Reusing Lua as a language is allowed only if selected by ADR-0009; the legacy
  CoreScripts binding/API is never reused for compatibility.
- Events should be immutable snapshots or values with bounded lifetime. Commands
  are queued for a deterministic server tick rather than applied during callbacks.
- Define callback ordering and script-generated command ordering so replay does
  not depend on hash order, worker timing, or filesystem enumeration.

## Phase 20 — Transactional persistence and deterministic replay

[Back to the phase tracker](IMPLEMENTATION_PLAN.md#phase-20--transactional-persistence-and-deterministic-replay)

- Persist domain state/change records, not protocol payloads, OpenMW object
  pointers, renderer state, or transport-library objects.
- Define the exact durability acknowledgement point: clients/scripts/admin tools
  must not be told an operation is durable before the selected commit policy is
  satisfied.
- Replay inputs include negotiated rules/configuration, content manifest, script
  bundle/API versions, deterministic seed, and command ordering metadata.
- TES3MP 0.8.x save or persistence migration is explicitly out of scope.

## Phase 21 — Administration, moderation, and discovery

[Back to the phase tracker](IMPLEMENTATION_PLAN.md#phase-21--administration-moderation-and-discovery)

- Do not expose scripting/runtime internals as the administrative API.
- Health and metrics endpoints should separate public-safe status from privileged
  operational detail.
- The new config/admin/discovery formats have no TES3MP 0.8 compatibility goal.

## Phase 22 — Desktop and PC VR stabilization and release

[Back to the phase tracker](IMPLEMENTATION_PLAN.md#phase-22--desktop-and-pc-vr-stabilization-and-release)

- Quantitative budgets should be established from measured representative scenes
  before optimization work is accepted as complete.
- A baseline OpenMW update policy must state how upstream security/stability
  updates are evaluated without coupling protocol version to engine version.
- Retain minimized fuzz inputs, failed simulation seeds, replay traces, and soak
  metrics as regression assets when they expose a defect.

## Phase 23 — Meta Quest 3 feasibility decision

[Back to the phase tracker](IMPLEMENTATION_PLAN.md#phase-23--meta-quest-3-feasibility-decision)

- Community Quest experiments are research references, not dependencies or code
  templates. Re-evaluate their status/licenses only when this phase begins.
- Quest constraints may motivate general client optimizations, but must not add
  Android/OpenXR/headset concepts to protocol or canonical server state.
- Upstream generally useful OpenMW/OpenMW-VR changes when practical rather than
  accumulating an unbounded permanent platform fork.

## Phase 24 — Meta Quest 3 multiplayer port

[Back to the phase tracker](IMPLEMENTATION_PLAN.md#phase-24--meta-quest-3-multiplayer-port)

- This phase is conditional and remains **Not Started** unless ADR-0012 records a
  go decision. A no-go decision in Phase 23 does not block program completion.
- Reuse the established `vr_pose` capability and platform-neutral authoritative
  root/capsule. Quest is another provider/composition target, not another core.
- Device-specific work belongs in the platform/adapter layer and a bounded patch
  queue with upstream candidates tracked explicitly.
