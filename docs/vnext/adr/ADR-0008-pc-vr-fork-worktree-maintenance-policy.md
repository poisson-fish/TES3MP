# ADR-0008: PC VR fork, worktree, and patch maintenance policy

Status: **Accepted**

Date opened: 2026-09-03

Date approved: 2026-09-03

Decision owner: project owner

Needed by: Phase 9, Slice 9.1

## Decision requested

Phase 9 needs one reproducible PC OpenMW-VR source, one way to combine it with
the accepted desktop vNext tree, and one ownership rule for upstream VR changes,
desktop multiplayer patches, merge resolutions, and later VR-provider changes.

The owner is asked to choose:

1. the PC VR source revision;
2. the branch/worktree and update model; and
3. the dependency/provenance rule used for the first proof build.

No option changes server authority, canonical state, gameplay rules, protocol,
pose semantics, or VR locomotion behavior. Those remain behind later Phase 9
decision gates. Approval here authorizes only the maintenance target described
below.

## Retained constraints

Every option must preserve ADR-0002, ADR-0007, and the accepted Phase 8 desktop
baseline:

- Windows x86-64 is the required initial PC VR platform. Linux x86-64 remains a
  best-effort research target. macOS PC VR is outside the initial release scope.
- Desktop `vnext` remains the authoritative product branch and must keep passing
  the Phase 8 regression gate.
- Protocol, client-session, transport, server, and deterministic core targets
  remain free of OpenMW, OpenXR, renderer, SDL, and fork types.
- Desktop and PC VR compose the same client session and semantic providers.
  Fork-specific types stop in the OpenMW adapter/provider layer.
- The PC VR source and every added dependency use immutable commits and retained
  hashes/licenses. A moving branch or network fetch is not a release input.
- The VR checkout is not a submodule, nested repository, vendored source copy,
  CI prerequisite for desktop, or second source of multiplayer-core code.
- Existing P8-001 through P8-004 ownership and C-R1 default-deny replicated-
  actor behavior are preserved. Any behavioral conflict returns to the owner.
- No upstream contact or submission is authorized by this decision.

## Selection criteria

The selected package must:

1. pin a reproducible OpenMW 0.51-compatible VR source and all new inputs;
2. keep desktop `vnext` independent and preserve the Phase 8 gates;
3. keep shared multiplayer code in one commit lineage rather than copied trees;
4. distinguish upstream VR code, retained P8 patches, merge resolutions, and
   new P9 hooks with machine-checked ownership;
5. support a fresh Windows x86-64 build without trusting expired artifacts;
6. bound update, rollback, dependency, license, and security work; and
7. stop for owner review rather than resolve architecture or gameplay conflicts
   implicitly.

## Primary-source research as of 2026-09-03

### Maintained source candidates

The public OpenMW-VR project is
[`madsbuvi/openmw`](https://gitlab.com/madsbuvi/openmw). Its own
[versioning policy](https://gitlab.com/madsbuvi/openmw/-/blob/openmw-vr-0.51-rc1/docs/source/manuals/openmw-vr/versioning.rst)
says it has no independent schedule and makes no merge-timeline promise. The
document's stated base is stale, so refs and build evidence must be checked
directly rather than inferred from that page.

| Candidate | Exact observed ref | Evidence and consequence |
|---|---|---|
| 0.51 VR release candidate | tag `openmw-vr-0.51-rc1`, commit `56a8e01390507375c9c2f2593e1c09e0df88c505` | Newest tagged 0.51 VR line; immutable source identity and closest match to desktop OpenMW 0.51 |
| 0.51 VR branch | branch `openmw-vr-51`, currently the same commit `56a8e013...` | Same tree today, but the ref is mutable and gives no benefit over the tag as a pin |
| default VR branch | branch `openmw-vr`, commit `0f520f65c3e085369e66d6a90ce871e817d4533f` | Older line; the repository page still identifies it as OpenMW 0.50 and it predates the 0.51 VR tag |
| local VR port | desktop `openmw-0.51.0`, commit `f4bec41444214a7903bebd178389ca22ca13f646`, plus a new local port of VR changes | Maximum control, but makes this project owner of a large VR engine port before interoperability is proven |

The upstream OpenMW VR merge request remains explicitly
[WIP and not intended for merge](https://gitlab.com/OpenMW/openmw/-/merge_requests/251).
Upstream OpenMW therefore does not provide a maintained built-in VR target that
can replace the fork in this phase.

### Revision relationship and collision pressure

The merge base between desktop `openmw-0.51.0` and the recommended VR tag is
`e0efd3b114cf15b01aa6bd9b6c27a386161430c9` from 2026-05-05. From that base:

- desktop 0.51 final has 2 later commits and 7 changed paths;
- the VR tag has 197 commits and 341 changed paths; and
- a straight tree comparison against the desktop baseline reports 348 VR-side
  paths.

Fourteen of those paths also differ intentionally in vNext. Ten are current
registered OpenMW patches:

- `apps/openmw/CMakeLists.txt`;
- `apps/openmw/engine.cpp` and `apps/openmw/engine.hpp`;
- `apps/openmw/main.cpp`;
- `apps/openmw/mwrender/animation.cpp` and `animation.hpp`;
- `apps/openmw/mwrender/npcanimation.cpp` and `npcanimation.hpp`;
- `apps/openmw/mwrender/renderingmanager.cpp`; and
- `apps/openmw/mwrender/vismask.hpp`.

The remaining four overlaps are `.github/workflows/push.yml`,
`CI/install_debian_deps.sh`, top-level `CMakeLists.txt`, and `README.md`. This is
too much collision pressure for an untracked source copy or ad-hoc cherry-pick
workflow. Conflict resolution needs explicit ownership and repeatable evidence.

### Maintenance and build evidence

- The tagged commit was authored on 2026-05-23. The `openmw-vr-51` branch has
  not moved past it as of this review.
- The exact commit's
  [GitHub workflow](https://github.com/madsbuvi/openmw/actions/runs/26336246109)
  passed its Ubuntu and Windows Server 2022 jobs. Its retained artifacts expired
  on 2026-08-21, so this is maintenance signal, not reusable proof for vNext.
- The same commit's
  [GitLab pipeline](https://gitlab.com/madsbuvi/openmw/-/pipelines/2548200125)
  passed Linux jobs and Windows MSBuild groups but failed both Windows Ninja
  groups. vNext therefore needs its own fresh Windows proof.
- The fork remains GPL-3.0, matching the OpenMW codebase license.
- The exact GitLab source archive for the proposed VR pin has observed SHA-256
  `ac5fa7314cf54bbe123abe0c21b9dcf23cf31722ccc2a89b8b455511920253ea`.
- The fork fetches Khronos OpenXR-SDK tag `release-1.0.24` while configuring the
  VR target. That tag peels to commit
  `1ca7bec6b531185530c9b4f1e7a50e1fd55e7641`; the source is Apache-2.0. The
  exact commit archive observed in this review has SHA-256
  `afc4c7c59dc0e427f03fc655e84d4394eb2d6070630924a63e547e4055ab816d`.
- The fork's Windows `m1.0` dependency manifest hashes its binary archive with
  SHA-512
  `8fcb43afa8e07dc9a9d7177ed610f1d0c7fa023922c9f6031583b79eb784d66d54157641d7c52ccaacfda7ea14bc9c2607c8d27c86d4e18a60e18ec4f1a24c78`,
  but the manifest is read from a moving branch and the build still performs a
  network `FetchContent` of OpenXR by tag. That flow does not meet vNext's
  release-input policy unchanged.

## Representative scenarios

1. A developer builds desktop `vnext`. No VR remote, checkout, dependency, or
   tool is required, and the accepted Phase 8 behavior is unchanged.
2. A developer opens the PC VR worktree. Its branch records both the exact
   desktop product history and the exact upstream VR commit; no multiplayer
   source directory was copied.
3. The initial merge touches one of the ten registered P8 paths. The merge
   stops, the resolution names its owner and test, and any architecture or
   behavior change returns to the owner before it lands.
4. A shared client-session fix lands on desktop. The same commit reaches the VR
   integration branch through a recorded merge and is not reimplemented.
5. OpenMW-VR publishes a new tag. A disposable update rehearsal reports changed
   dependencies, overlapping owned paths, lost hooks, and test results before
   the maintained VR branch moves.
6. The public VR branch changes or disappears. The immutable commit, source URL,
   archive hash, license, and local branch history remain enough to reproduce
   the selected source.
7. The OpenXR loader or runtime returns missing-extension, session-loss, or
   non-finite tracking data. Later providers fail closed and no value crosses
   into shared state until bounded validation is implemented and approved.
8. A VR update changes movement, player-root, interaction, remote-actor, or
   activation behavior. The update is blocked for owner review; maintenance
   convenience cannot choose gameplay behavior.

## Decision 1: PC VR source revision

### Option A — pin `openmw-vr-0.51-rc1` at `56a8e013...` (recommended)

Use the immutable tag and full commit as the Phase 9 fork baseline. Record the
canonical GitLab source and GitHub mirror, but verify the commit identity rather
than trusting either branch name.

This is the closest public VR line to the accepted OpenMW 0.51 desktop baseline,
has a successful Windows 2022 build signal, and bounds the first integration.
It is still an RC with a weak release cadence and expired artifacts, so vNext
must reproduce the build and owns support for its selected pin.

### Option B — pin the current `openmw-vr-51` branch head by commit

Pin the observed branch head `56a8e013...` and create a local provenance tag.
The resulting source is byte-identical to Option A today. It avoids reliance on
the upstream tag label but invents a project-local release identity and provides
no technical advantage while the upstream tag exists.

### Option C — port VR directly onto clean desktop OpenMW 0.51

Treat the public fork only as research and rebuild the required VR engine delta
on `f4bec414...`. This offers full history and baseline control, but transfers
ownership of hundreds of engine paths and the OpenXR integration to TES3MP. It
would delay interoperability and expand the Phase 9 architecture surface before
the first product proof.

The older default `openmw-vr` branch is rejected as a candidate because a newer
0.51-specific immutable tag exists.

## Decision 2: branch and worktree maintenance

### Option A — same-repository merge-overlay branch and sibling worktree (recommended)

After approval only, add a read-only `openmw-vr-upstream` remote, fetch the
selected immutable tag, create persistent branch `vnext-vr` from `vnext`, and
attach that branch to a developer-local sibling worktree. Merge the selected VR
commit with `--no-ff` so both source lineages remain explicit.

`vnext` stays the first-parent product history and desktop authority. Shared
multiplayer changes reach `vnext-vr` by merging `vnext`; upstream VR updates
reach it only by merging an owner-reviewed immutable tag or commit. Published
`vnext-vr` history is not rebased or force-pushed.

Use a VR-target patch/provenance registry on `vnext-vr`. It treats the selected
VR commit as upstream, keeps P8 ownership for carried desktop hooks, assigns P9
IDs to merge resolutions and VR-only hooks, and excludes only reviewed adapter-
owned provider paths. The existing desktop registry remains anchored to
`f4bec414...` and is not weakened.

Benefits are shared commit identity, one Git object store, explicit ancestry,
normal bisectable commits, and no copied core. Costs are merge commits, a second
maintained branch, and deliberate conflict handling on the ten known P8 paths.

### Option B — external clone plus exported patch stack

Keep the VR checkout in a separate clone and apply generated patches from
`vnext` during setup or build. This makes the upstream baseline visually clean,
but creates another source of truth, weakens commit identity, and conflicts with
ADR-0007's approved normal-commit registry model. Selecting it reopens ADR-0007.

### Option C — import the VR delta into `vnext` behind build switches

Place desktop and VR engine code in one branch and select executables with CMake
options. This removes branch synchronization but mixes a 341-path fork delta
into the desktop baseline, expands disabled-mode risk, and makes upstream VR
updates large product-tree merges. It also obscures which project owns a path.

## Decision 3: dependency and provenance bootstrap

### Option A — retain vNext locks and add an exact OpenXR source pin (recommended)

Use the repository's existing OpenMW 0.51 dependency policy wherever the VR
tree is compatible. Replace the fork's tag-based network fetch with a lock for
OpenXR-SDK commit `1ca7bec6...`, the observed archive SHA-256, Apache-2.0 license,
and an offline/retry-safe repository-owned acquisition step. Capture the exact
Windows compiler, SDK, OpenXR loader, runtime, headset, driver, and dependency
versions in proof evidence.

This adds one audited dependency instead of adopting the fork's whole binary
bundle as authority. The first disposable build may use the fork bundle only to
diagnose compatibility; it cannot become release evidence until every input is
locked and licensed under the vNext rules.

### Option B — adopt the fork's `m1.0` Windows bundle

Pin its manifest commit, archive URL, SHA-512, package inventory, and all
licenses, then maintain that bundle as the VR target's dependency authority.
This may reduce initial build friction but creates a second desktop dependency
universe and duplicates audits already completed for OpenMW 0.51.

### Option C — use installed OpenXR and system packages

Discover the loader and dependencies from the developer machine. This is useful
as a best-effort Linux probe but cannot be the required Windows release path
because it does not reproduce exact inputs.

## Recommended package

Approve Decision 1 Option A, Decision 2 Option A, and Decision 3 Option A.

This package chooses the only current immutable 0.51 VR tag, preserves the
desktop branch and accepted patch policy, keeps shared multiplayer code as the
same commits, makes all known conflicts visible, and adds only the OpenXR input
that the VR target actually requires. It does not approve VR gameplay or pose
behavior.

## Proposed update and rebase procedure if approved

1. Poll refs only during an explicit maintenance review. Never consume a moving
   branch in CI or a release build.
2. Record the candidate full commit, tag, merge base, source/archive hashes,
   license, dependency changes, branch activity, and upstream CI signal.
3. Create a disposable rehearsal branch/worktree from current `vnext-vr` and
   merge the candidate with `--no-ff`. Do not rebase published history.
4. Generate a three-way collision report against both the prior VR pin and the
   current desktop baseline. Classify every overlap as upstream VR, retained P8,
   P9 integration, or unresolved.
5. Stop for owner review when a change affects architecture, authority, state
   scope, security, supported platforms, dependencies, user-visible behavior,
   VR locomotion/interaction, C-R1, or an approved hook contract.
6. Update the VR provenance and patch registry, run the required desktop and VR
   gates, and retain exact evidence.
7. Merge the tested update commit to `vnext-vr`. Delete only the disposable
   rehearsal worktree/branch. Never force-push the maintained target.
8. Keep the prior pin reachable until the replacement build and interoperability
   evidence pass and the owner accepts the update.

## Patch ownership if the recommended package is approved

| Change class | Owner | Required record |
|---|---|---|
| Source reachable from selected VR pin | OpenMW-VR upstream | VR provenance: source, commit, tag, tree/archive hash, license, observed CI |
| Shared engine-independent multiplayer commits | TES3MP shared components | Same commits merged from `vnext`; normal tests and desktop provenance |
| P8-001 through P8-004 carried into VR | Existing OpenMW adapter / replicated-actor owners | Existing IDs plus VR-target applicability and conflict evidence |
| Mechanical merge resolution | Phase 9 integration | P9 registry ID, exact paths, both parents, test, removal/update rule |
| New VR provider or semantic hook | OpenMW VR adapter | New P9 registry ID and prior owner approval; fork types remain local |
| OpenXR source and build wiring | PC VR platform target | Exact commit/hash/license, dependency lock, build and failure evidence |
| Unresolved behavioral conflict | Project owner | ADR/GDR amendment before production resolution lands |

## Proposed Slice 9.1 acceptance tests and evidence

Slice 9.1 remains **In Progress** until owner approval and all applicable checks
below pass:

1. `vr_source_pin_resolves_to_56a8e013_and_is_immutable`
2. `vr_worktree_uses_same_repository_without_submodule_or_nested_clone`
3. `vr_branch_records_desktop_and_vr_parent_lineage`
4. `vr_initial_merge_reports_all_ten_registered_p8_overlap_paths`
5. `vr_patch_registry_classifies_every_local_non_adapter_openmw_delta`
6. `shared_multiplayer_sources_are_commits_not_copied_directories`
7. `vr_dependency_lock_pins_openxr_commit_hash_and_license`
8. `vr_upstream_baseline_configures_and_builds_on_windows_x86_64`
9. `vr_update_rehearsal_does_not_move_the_maintained_target_on_failure`
10. `desktop_patch_registry_and_phase8_focused_regressions_remain_green`

Exact repository commands, compiler/dependency manifests, collision reports,
and hashes must be recorded in the Phase 9 implementation note. Slice 9.2 owns
the first shared adapter/client-session dual-engine build; Slice 9.7 owns the
desktop/VR process and hardware interoperability gate.

## Consequences and failure handling

- The project accepts a second maintained engine branch but not a second
  multiplayer implementation.
- A public fork tag is an input, not a support guarantee. vNext owns testing and
  packaging for the selected pin.
- Merge conflicts are expected because ten owned P8 paths overlap. A conflict is
  not permission to alter behavior.
- OpenXR runtime and tracking data are untrusted client-local inputs. Missing or
  invalid data must fail closed and cannot mutate canonical state.
- Linux may compile and run as research, but it does not become advertised or
  blocking support without reopening ADR-0002.
- If the exact fork cannot preserve the shared adapter/session boundary, stop
  Phase 9 and return to the owner rather than create a second adapter.

## Review and replacement triggers

Reopen this ADR when:

- OpenMW-VR publishes a new immutable release or rewrites/removes the selected
  source;
- upstream OpenMW gains a maintained PC VR target;
- the selected pin cannot build on required Windows hardware/runtime;
- a fork update changes dependencies, license, supported runtime, C-R1, input,
  player-root, interaction, rendering, or hook semantics;
- the merge-overlay branch cannot keep shared code as the same commits;
- the target needs a submodule, generated patch stack, force-push, or floating
  network input;
- Linux PC VR evidence is strong enough to consider support promotion; or
- a security issue requires replacing the OpenXR loader/runtime or fork pin.

## Owner approval

The project owner approved Option A for Decisions 1 through 3 on 2026-09-03:

1. pin OpenMW-VR tag `openmw-vr-0.51-rc1` at
   `56a8e01390507375c9c2f2593e1c09e0df88c505`;
2. maintain a same-repository `vnext-vr` merge-overlay branch in a local sibling
   worktree, without rebasing published history; and
3. retain vNext dependency locks and add an exact OpenXR-SDK commit/hash/license
   lock instead of adopting the fork bundle as dependency authority.

This approval authorizes the Slice 9.1 maintenance target and proof only. It
does not approve protocol, pose, locomotion, interaction, authority, state-
scope, or other gameplay behavior for later Phase 9 slices.

## Initial merge composition clarification

The initial merge showed that `apps/openmw/main.cpp` is shared by the desktop
and VR executables while only the desktop target links the Phase 8 adapter.
Before resolution, the owner reviewed:

- Option A (recommended): exclude desktop multiplayer composition under
  `OPENMW_VR` for Slice 9.1; Slice 9.2 adds the real shared-session VR
  composition.
- Option B: temporarily link desktop providers into the VR executable.
- Option C: retain the unresolved, unbuildable composition.

The owner approved Option A on 2026-09-03. This is a temporary build-boundary
choice only. It does not select VR authority, state scope, pose, input,
locomotion, interaction, or gameplay behavior.
