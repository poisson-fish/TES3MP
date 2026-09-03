# GDR-0016: Phase 8 disconnect and resume presentation

Status: **Accepted**

Date opened: 2026-09-03

Date approved: 2026-09-03

Decision owner: project owner

Needed by: Phase 8 Slice 8.7

## Decision

The project owner approved Option A on 2026-09-03.

On disconnect, remote avatars and their smoothing buffers disappear immediately
and the client reports that resume is in progress. OpenMW remains responsive,
but disconnected local actions produce no multiplayer commands. Any local
movement during the gap is speculative presentation and is replaced by the
first complete resumed authoritative snapshot.

The client does not show remote state or report success until that snapshot
passes identity, generation, entity-revision, and acknowledgement continuity
checks. Success reports resumed and rebuilds current same-cell presentation.
Expiry, authentication rejection, continuity failure, or an uncertain submitted
token reports a terminal sanitized failure and leaves multiplayer disconnected.
The client never silently creates a fresh identity.

## Scenarios and acceptance

- `disconnect_clears_remote_presentation_and_enters_bounded_resume`
- `disconnected_input_emits_no_multiplayer_command`
- `resume_waits_for_complete_snapshot_before_representing`
- `valid_resume_preserves_identity_revision_and_acknowledgements`
- `resumed_snapshot_replaces_speculative_local_position`
- `expired_or_uncertain_resume_stays_disconnected_without_fresh_join`
- `resume_credentials_are_absent_from_logs_and_evidence`
- two real desktop clients demonstrate leave/return, movement convergence,
  bounded smoothing, resume, and clean shutdown.

## Boundary and review trigger

This is provisional Phase 8 presentation around the accepted Phase 7 canonical
lifecycle. It changes no authority or state scope. Phase 10 must ratify or
replace it. Reopen before retaining stale avatars, disabling the whole engine,
offline command buffering, persistent resume, live replacement, or automatic
fresh identity creation.

## Owner approval

Approved by the project owner on 2026-09-03: recommended Option A presentation
behavior and all proposed acceptance scenarios.
