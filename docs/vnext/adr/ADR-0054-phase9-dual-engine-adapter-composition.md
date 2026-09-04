# ADR-0054: Phase 9 dual-engine adapter composition

Status: **Accepted**

Date opened: 2026-09-03

Decision owner: project owner

Needed by: Phase 9, Slice 9.2

## Decision requested

Slice 9.2 must make desktop `openmw` and PC VR `openmw_vr` build against the
same adapter and client-session implementation without copying sources or
choosing VR input, locomotion, pose, interaction, or presentation behavior that
belongs to later slices.

The owner is asked to choose the executable/adapter target topology and the
safe VR runtime boundary before VR providers exist.

## Retained constraints

- ADR-0007 keeps one app-local adapter/session path, one post-input frame hook,
  an acyclic target graph, and fork/OpenXR types out of shared targets.
- ADR-0047 keeps the coordinator-owned runtime and two borrowed semantic input
  and presentation providers with correct-then-command frame order.
- ADR-0049 keeps multiplayer explicitly enabled, constructs nothing when
  disabled, and fails startup closed with sanitized status.
- ADR-0008 keeps shared multiplayer work as commits merged from `vnext`, not
  copied source, and preserves desktop as the product authority.
- C-R1 remote actors remain default-deny. No hook, canonical state, protocol,
  authority, state scope, or gameplay behavior changes in Slice 9.2.

## Current seam

The VR worktree already builds the shared `openmw_tes3mp_adapter` static target,
which links `tes3mp_client_session` and `openmw-lib`. Only desktop `openmw`
links that adapter. `main.cpp` excludes all concrete multiplayer composition
under `OPENMW_VR`, while the accepted P8-001 engine coordinator attachment and
frame call already compile in both executables.

The existing `DesktopSemanticInput` reads OpenMW action state. Reusing it in VR
would make the current fork's action mapping decide VR locomotion implicitly.
That is outside Slice 9.2 and remains reserved for Slice 9.4 approval.

## Selection criteria

The selected option must keep one coordinator/session implementation, preserve
the accepted lifetime and frame order, compile desktop and VR from shared
commits, keep fork types local, fail closed without approved providers, add no
engine hook, and leave a narrow provider seam for Slice 9.4.

## Representative scenarios

1. Desktop builds and runs with its accepted providers and behavior unchanged.
2. VR builds and links the same adapter/coordinator and client-session targets;
   no shared source is copied or compiled from a divergent implementation.
3. VR starts with multiplayer disabled and constructs no transport, runtime,
   coordinator, or providers.
4. A user requests multiplayer in VR before Slice 9.4 providers exist. Startup
   fails closed with a sanitized unsupported-provider result and sends no
   network traffic.
5. Slice 9.4 later supplies VR provider implementations without replacing the
   coordinator, session, connection, frame hook, or protocol path.
6. A fork/OpenXR type is introduced into a shared target. Boundary verification
   rejects the change.

## Option A — shared adapter/composition plus separate provider targets

Keep one `TES3MP::OpenMWAdapter` target for coordinator, connection, and shared
OpenMW conversion code. Move concrete desktop providers into a desktop-only
provider target and reserve a parallel VR-provider target for Slice 9.4. Both
executables link the same adapter and client-session lineage through one thin
composition API.

For Slice 9.2, VR multiplayer-disabled mode remains inert. Explicit VR enable
fails closed before transport/session creation because no approved VR providers
exist yet. Build and boundary tests exercise the shared composition with test
providers; production VR input and presentation wait for Slice 9.4.

Tradeoff: adds clear target seams and a temporary explicit unsupported result,
but prevents build wiring from selecting gameplay behavior and gives Slice 9.4
one narrow place to add fork-local providers.

## Option B — one monolithic adapter with `OPENMW_VR` source branches

Link the current adapter to both executables and select desktop or VR concrete
code with preprocessor branches inside the target.

Tradeoff: fewer CMake targets now, but fork conditionals spread into the shared
adapter, one compilation cannot prove both provider variants, and later drift
is easier. It weakens the separate-provider boundary accepted in ADR-0007.

## Option C — separate complete desktop and VR adapter libraries

Build two adapter libraries, each owning its own coordinator, connection, and
provider source set while both link `tes3mp_client_session`.

Tradeoff: strong compile isolation, but duplicates the adapter implementation
and permits lifecycle, ordering, reconnect, and failure behavior to diverge.
This conflicts with the Phase 9 outcome of one adapter/client-session path.

## Recommendation

Approve Option A. It preserves one coordinator/session implementation, keeps
fork-specific code at the provider edge, makes source identity testable, and
fails closed instead of choosing VR locomotion or presentation prematurely.

## Consequences and failure handling

- One additional provider-target boundary is maintained, but coordinator,
  connection, runtime, and session behavior have one implementation.
- Desktop composition remains available exactly as accepted. VR composition is
  structurally linked but cannot create a session until approved providers are
  supplied.
- Missing VR providers produce a bounded sanitized startup failure before any
  credential read, transport allocation, or network attempt.
- A build or dependency-graph failure blocks Slice 9.2; it is not permission to
  duplicate shared code or leak fork types into the session target.
- Any need for another engine hook, changed frame order, or provider behavior
  returns to owner review before implementation continues.

## Proposed acceptance tests

1. `desktop_and_vr_link_the_same_adapter_and_client_session_targets`
2. `dual_engine_composition_has_no_copied_multiplayer_sources`
3. `shared_targets_reject_openxr_vr_and_fork_headers`
4. `desktop_enabled_and_disabled_composition_remains_unchanged`
5. `vr_disabled_mode_constructs_no_multiplayer_objects`
6. `vr_enable_without_approved_providers_fails_before_network_creation`
7. `shared_test_providers_attach_and_preserve_correct_then_command_order`
8. `desktop_and_vr_target_graphs_remain_acyclic`
9. `p8_frame_and_shutdown_hooks_are_reused_without_new_engine_hooks`
10. `desktop_and_vr_supported_builds_link_from_the_same_shared_commit`
11. `desktop_regression_and_both_patch_registries_remain_green`

## Review and replacement triggers

Reopen this decision if the common target cannot link both executables, the VR
fork needs a different coordinator lifecycle or frame order, provider isolation
requires fork types in shared code, Slice 9.4 cannot attach providers through
the approved interfaces, or a maintained OpenMW plugin/composition facility
replaces the local executable seam.

## Owner approval

The project owner approved Option A and the proposed acceptance tests on
2026-09-03. This approval covers only the shared adapter/composition target,
separate provider targets, and fail-closed VR enable boundary described above.
It does not approve VR input, locomotion, pose, interaction, presentation, or
other gameplay behavior.
