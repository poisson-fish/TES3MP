# Phase 12 production movement discovery

Date: 2026-09-06

Status: **Complete**

This pass traces the shipped fixture path and names the evidence and decisions
needed before production movement. It changes no movement, cell, interest,
resync, collision, correction, animation, or pose behavior.

## Current path and measured bounds

| Lane | Repository-backed behavior | Current bound or gap |
|---|---|---|
| Local input | `DesktopSemanticInput` reads four resolved OpenMW move actions once per engine frame, combines opposing values, rotates by player-root yaw, and emits a planar desired velocity. Desktop and VR share this semantic path. | Input is normalized and fixed at 4,096 position quanta per server tick: 4 OpenMW units/tick, or 120 units/second at 30 Hz. Disabled controls and non-finite input produce zero. Run, sneak, jump, automove, vertical movement, and orientation are absent. |
| Client command | `MotionIntentTracker` keeps one pending reliable ordered command and one latest desired value. A changed or stop intent waits for the pending sequence acknowledgement. | Exactly one in flight. No input tick, duration, prediction history, resend deadline, or command-latency metric exists. |
| Server intake | The session boundary checks session, generation, entity, epoch, ordering, and queue admission, then forwards the signed 64-bit desired velocity. Observed entity revision is context rather than an exact movement precondition. | No velocity magnitude, axis, acceleration, or locomotion-state validation exists. The queue bounds are 4,096 total, 128 per session generation, and 1,024 per tick. |
| Canonical advance | The reducer replaces velocity on an accepted command, then adds velocity to position on every 30 Hz tick using checked integer arithmetic. Moving entities publish a latest-wins view each tick. | Four catch-up ticks may be emitted per scheduler pump. There is no speed model, acceleration, collision, gravity, ground/water state, orientation advance, or automatic exterior-cell transition. Integration overflow is atomic in the reducer, but is fatal to the composed server pump. |
| Local correction | OpenMW continues its normal mechanics and 60 Hz Bullet movement. Each newer same-cell canonical snapshot calls the existing position-only `moveObject` seam. | Zero deadband and zero blend: every accepted snapshot corrects exactly. Facing and local animation survive, but prediction is not correlated with command history and correction distance is not measured. |
| Remote root | `RemoteMotionBuffer` owns four samples per entity, presents two ticks (66.7 ms) behind, extrapolates at most three ticks (100 ms), then holds. Corrections through 16,384 quanta (16 OpenMW units) blend for 66.7 ms; larger corrections and cell/epoch discontinuities snap. | Snapshot age, depth, extrapolation, correction distance, and snaps are typed, but production desktop composition uses the null sink. Orientation is selected from a sample, not smoothed. |
| Animation | Renderer-only replicated actors load their appearance, disable root accumulation, loop `idle`, and advance it by frame time. | Velocity, locomotion mode, facing, jump, and one-shot state do not drive remote animation. |
| Optional pose | A capable VR client samples head and optional hands every 50 ms. The server checks session/generation/root/epoch/sequence and exact same-cell interest, then relays latest-wins. | Pose is not applied to the remote skeleton. There is no age/loss blend metric or production staleness policy. It remains unable to move the canonical root. |

## Consequence

The current path is a useful fixture, not a safe production policy. In
particular, an authenticated client may submit an arbitrary signed 64-bit
velocity. The protocol and reducer accept it; a later checked position overflow
fails the tick and the composed server stops. Production exposure therefore
requires validation before canonical mutation, independent of later tuning.

OpenMW's local player path cannot simply become the canonical simulation. It
derives movement and animation from engine stats and state, and resolves
collision inside client-local Bullet at a default 60 Hz. Accepting that resulting
transform would reopen ADR-0006's server-writer decision.

## Production design seam

Retain the established direction:

1. The desktop and VR leaves capture one shared, bounded semantic locomotion
   sample. Root yaw may affect direction; tracked head and hands may not.
2. A platform-neutral client tracker assigns input ticks/sequences, retains a
   bounded unacknowledged history, and exposes timing/correction observations.
3. The server validates locomotion state and magnitude before canonical mutation.
   A fixed-tick movement kernel is the sole producer of canonical root transform
   and velocity. In the recommended collision option, a content-backed
   server-side query boundary supplies collision results; neither OpenMW nor
   Bullet types enter protocol or server core.
4. Canonical snapshots continue on the existing latest-wins lane. Reliable cell
   transition, exact same-cell interest, baseline, and resync rules remain
   separate and unchanged.
5. Local presentation predicts reversibly from retained semantic input and
   reconciles to snapshots. Remote presentation retains bounded history.
   Animation derives from accepted locomotion state; pose stays optional,
   root-relative, latest-wins, and presentation-only.

The existing protocol can carry only planar fixture velocity. Production input
ticks, locomotion modes, facing, animation state, and discontinuity reasons need
an additive versioned schema rather than overloading `PlayerMotionIntent` or an
extreme snapshot.

## Evidence before tuning

The next behavior-neutral pass should connect bounded, identity-free observations
for command enqueue-to-ack time, stop latency, local correction distance,
snapshot age, remote buffer depth, extrapolation, hard snaps, server tick lag,
and pose age/loss. Capture the same scripted walk/turn/stop route on desktop and
PC VR under direct, jitter, loss, and stall profiles. No speed, delay, blend, or
snap value should be ratified from the current null-sink fixture.

## Owner choices after evidence

These choices change architecture or player-facing behavior and require approval
before production implementation:

- **Collision authority:** A) server-side content-backed collision query
  (**recommended**); B) client collision-result proposals with coarse server
  checks, reopening ADR-0006; C) collision-free canonical movement, suitable
  only for another fixture.
- **Speed and locomotion:** A) manifest-scoped server movement profiles with
  explicit walk/run/sneak/jump states (**recommended first production scope**);
  B) wait for full character stats and effects; C) trust client-derived speed,
  reopening the authority model.
- **Local correction:** A) bounded input-history replay plus measured soft-error
  convergence and explicit hard discontinuities (**recommended**); B) retain
  exact per-snapshot correction; C) server-only visible movement.
- **Remote lag policy:** A) bounded adaptive playback inside approved floors and
  ceilings (**recommended if captures show materially different jitter**); B)
  ratify the current fixed 66.7/100/66.7 ms fixture; C) snapshot-only movement.
- **Animation and pose:** A) replicate a small canonical locomotion state while
  keeping one-shots reliable and VR pose ephemeral (**recommended**); B) derive
  all animation locally from velocity; C) make pose canonical, which conflicts
  with ADR-0006 and ADR-0055.

## Named proof scenarios

1. `motion_magnitude_is_bounded_before_canonical_mutation`
2. `extreme_motion_input_cannot_fail_the_server_tick`
3. `desktop_and_vr_semantic_locomotion_are_equivalent`
4. `server_collision_is_the_only_canonical_root_result`
5. `stop_and_direction_change_converge_under_delay_and_loss`
6. `local_prediction_replay_cannot_feed_back_as_input`
7. `local_and_remote_corrections_remain_within_approved_budgets`
8. `teleport_and_cell_transition_are_explicit_discontinuities`
9. `movement_does_not_change_exact_cell_interest_or_resync_rules`
10. `animation_state_cannot_author_canonical_transform`
11. `stale_or_missing_pose_blends_to_platform_neutral_fallback`
12. `vr_pose_cannot_move_root_or_satisfy_gameplay_reach`
