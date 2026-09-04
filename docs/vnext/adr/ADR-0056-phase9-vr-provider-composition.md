# ADR-0056: Phase 9 VR provider composition

Status: **Proposed**

Date opened: 2026-09-04

Decision owner: project owner

Needed by: Phase 9, Slice 9.4

## Decision requested

Slice 9.4 must replace the VR executable's fail-closed missing-provider path
without copying the coordinator/session or leaking OpenXR/fork types into shared
targets. The owner must choose the VR provider target, source-reuse, and
executable-composition boundary.

## Retained constraints

- ADR-0054 keeps one shared adapter/session and separate desktop/VR provider
  leaves.
- ADR-0008 requires new fork-local provider work to use a reviewed P9 registry
  entry. Desktop remains product authority.
- ADR-0047 keeps borrowed providers, coordinator ownership, post-input frame
  order, and teardown order unchanged.
- ADR-0055 leaves pose advertising, dispatch, and transport unavailable until
  Slice 9.5.
- C-R1 remote actors remain renderer-only and default-deny.
- No new engine hook, canonical state, protocol, authority, or gameplay system
  registration is permitted by this decision.

## Current seam

`openmw_vr` links the shared adapter but supplies empty providers, so explicit
multiplayer enable fails before credentials or networking. The fork already maps
controller bindings into the ordinary OpenMW movement actions. The accepted
desktop provider source reads those semantic actions and implements the accepted
canonical cell/correction/C-R1 presentation behavior; it already compiles
against the VR fork as part of prior link proofs.

## Option A — fork-local VR leaf over shared provider source (recommended)

On `vnext-vr`, add `TES3MP::OpenMWVrProviders` as a distinct static provider
leaf. Compile the existing shared provider source into that leaf and expose
fork-local `VrFixtureMapping`, `VrSemanticInput`, and `VrPresentation` names
through a thin alias/composition header. Do not copy or fork provider logic.

Wire `openmw_vr` to the VR leaf and compose the same bounded configuration,
status reporting, coordinator, and frame hook already used by desktop. Record
the exact VR-only CMake, main, and provider-header paths as P9-003. Shared core
targets and the desktop executable remain unchanged. A later slice may replace
the thin aliases with wrappers while retaining these public names and target.

Tradeoff: the common provider source is compiled once per executable target, but
source identity and separate dependency leaves remain machine-checkable.

## Option B — link the desktop provider target directly

Link `TES3MP::OpenMWDesktopProviders` to `openmw_vr` and instantiate its desktop
types directly.

Tradeoff: smallest build edit, but erases the separately approved VR leaf,
makes later fork-local pose work disturb the desktop target, and reopens
ADR-0054.

## Option C — write fork-aware providers now

Create new providers that read OpenXR/controller/tracking types and independently
reimplement cell, movement, correction, and remote presentation behavior.

Tradeoff: offers immediate VR customization, but duplicates accepted logic,
pulls Slice 9.5 pose/rate decisions forward, and increases divergence and trust
surface.

## Recommendation

Approve Option A. It closes the temporary unavailable-provider path with the
smallest fork-local composition, preserves separate provider leaves without
source duplication, and leaves all pose and gameplay choices at their gates.

## Proposed acceptance tests

1. `vr_provider_leaf_has_a_distinct_target_and_shared_source_identity`
2. `vr_provider_leaf_contains_no_copied_provider_implementation`
3. `shared_core_targets_reject_openxr_vr_and_fork_headers`
4. `openmw_vr_links_adapter_session_and_vr_provider_leaf`
5. `desktop_target_and_provider_composition_are_unchanged`
6. `vr_disabled_mode_constructs_no_multiplayer_objects`
7. `vr_enabled_mode_validates_configuration_before_network_creation`
8. `vr_status_and_provider_failures_remain_bounded_and_sanitized`
9. `existing_post_input_hook_and_teardown_order_are_reused`
10. `p9_003_registry_paths_tests_and_removal_rule_are_exact`
11. `desktop_and_vr_supported_builds_and_boundary_gates_pass`

## Consequences and failure handling

- The VR branch gains one fork-owned provider leaf and one registry entry.
- Missing/invalid providers or configuration still fail before network creation.
- Provider exceptions retain existing closed result categories and clear
  presentation.
- A need for a new hook, fork type in shared code, or divergent canonical
  presentation stops implementation for owner review.

## Review and replacement triggers

Reopen this ADR if the shared source cannot compile as both provider leaves,
Slice 9.5 cannot extend the retained VR names without a target cycle, VR status
needs a new engine hook, or OpenMW gains a supported plugin/provider facility.

## Owner approval

Pending. No option or acceptance test is approved yet.
