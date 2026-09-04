# GDR-0017: Phase 9 VR fixture input and presentation

Status: **Proposed**

Date opened: 2026-09-04

Decision owner: project owner

Needed by: Phase 9, Slice 9.4

Companion architecture record:
[`ADR-0056`](../adr/ADR-0056-phase9-vr-provider-composition.md)

## Decision requested

Slice 9.4 needs provisional VR movement-input and canonical-presentation
behavior. This gate decides whether tracked view/hand direction may affect the
authoritative movement intent and whether tracked pose is presented now.

## Retained authority and state scope

- The server remains the only canonical movement writer. VR supplies the same
  bounded `PlayerMotionIntent` as desktop.
- No VR platform flag, tracking value, locomotion mode, or presentation state is
  canonical, persisted, replayed, checksummed, or sent in Slice 9.4.
- GDR-0014's fixture speed, normalization, correction, and excluded actions
  remain provisional. GDR-0004 still owns production movement and pose.
- C-R1 remote actors remain presentation-only and excluded from gameplay,
  physics, mechanics, Lua, focus, and activation.

## Decision 1: movement direction

### Option A — exact desktop root-yaw semantics (recommended)

Read the VR fork's already-resolved OpenMW `MoveLeft`, `MoveRight`,
`MoveForward`, and `MoveBackward` action values. Normalize and rotate them by
the local player root yaw using the existing 4,096-quanta-per-tick mapping.
Controls-disabled or invalid/non-finite input produces zero intent.

Headset yaw, room-scale position, hand aim, `hand directed movement`, raw
OpenXR axes, jump, run, sneak, turning, and interaction do not alter the
multiplayer intent in this slice. Controller handedness still works because the
fork maps bindings into the same semantic actions before the provider samples.

### Option B — preserve configured view/hand-directed locomotion

Use root yaw for view-directed mode and add the fork's current tracked hand
movement offset when hand-directed mode is enabled.

Tradeoff: closer to single-player VR feel, but tracking validity and loss would
now affect authoritative velocity direction and require pose-derived fallback
rules before Slice 9.5/Phase 12.

### Option C — direct OpenXR movement mapping

Read controller axes and tracking spaces directly, with VR-specific speed and
dead-zone rules.

Tradeoff: bypasses semantic bindings, duplicates input policy, exposes fork
types, and prematurely selects production locomotion behavior.

## Decision 2: presentation scope

### Option A — canonical parity only (recommended)

Apply the same accepted cell mapping, exact local authoritative correction,
bounded remote smoothing, C-R1 renderer-only remote actors, disconnect clearing,
and resume barrier as desktop. Keep head/hand pose advertising, routing,
sampling, and application disabled until Slices 9.5 and 9.6.

### Option B — local tracked-pose presentation now

Add head/hand capture or remote rig updates inside the provider before network
routing exists.

Tradeoff: creates unobservable partial behavior and chooses tracking conversion,
lifetime, and application rules owned by later slices.

### Option C — gameplay-integrated remote VR actors

Register remote avatars with normal mechanics, physics, focus, or activation so
VR interactions see them as ordinary actors.

Tradeoff: violates C-R1 and silently creates gameplay authority and interaction
semantics outside their phases.

## Recommendation

Approve A/A. It proves VR connection and semantic movement without letting raw
tracking affect canonical motion or pulling pose/runtime policy forward.

## Proposed acceptance scenarios

1. Equivalent desktop and VR semantic action values plus root yaw produce exact
   equal intents, including cardinal, diagonal, zero, and ties-to-even cases.
2. Controller handedness changes bindings, not provider semantics.
3. Changing head/hand pose with identical action values and root yaw does not
   change the Slice 9.4 movement intent.
4. Disabled controls and non-finite values produce exact zero intent.
5. Cell transition, correction, remote smoothing, clearing, and resume behavior
   match the accepted desktop contracts.
6. Room-scale motion cannot move the canonical root.
7. Remote actors remain C-R1 renderer-only and non-interactive.
8. No pose capability is advertised and no pose frame can enter runtime queues.
9. Desktop behavior and all existing protocol/session tests remain unchanged.

## Failure and review boundary

Provider failures use existing bounded status categories, clear presentation,
and stop the connection without partial canonical mutation. Reopen this GDR if
root-yaw-only movement is unusable for the accepted proof, tracked direction is
requested for commands, pose application begins, or any remote VR avatar needs
gameplay registration.

## Owner approval

Pending. Neither decision nor the proposed scenarios are approved yet.
