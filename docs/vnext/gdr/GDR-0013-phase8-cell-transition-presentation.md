# GDR-0013: Phase 8 cell-transition presentation

Status: **Accepted**

Date opened/approved: 2026-09-03

Decision owner: project owner

Needed by: Phase 8 Slice 8.4

Companion architecture record: [ADR-0050](../adr/ADR-0050-phase8-cell-and-remote-presentation.md)

## Decision

The local OpenMW cell change is semantic intent, not canonical authority. The
adapter captures it before applying snapshots, sends the requested fixture
cell as a reliable command, and keeps the local transition visible while that
command is pending. One later local transition may replace the deferred value.
After acknowledgement, authoritative state confirms or corrects the player.
Authoritative correction never feeds back as another command.

Remote avatars exist only when present in the session's authoritative observed
set and spatial view. They use the configured fixture NPC appearance and have
no gameplay authority, collision, scripts, inventory, persistence, or canonical
identity beyond their presentation key.

## Boundaries

This record adds no movement integration, interpolation, prediction threshold,
combat, activation, or inventory behavior. Those remain later gated slices.

## Owner approval

Approved by the project owner on 2026-09-03 as part of the breaking Slice 8.4
package, including P8-004.

## 2026-09-03 Slice 8.5 amendment

ADR-0051 treats the fixture transition's entity revision as observed context
rather than an equality gate, while retaining current session, binding, epoch,
sequence, and domain validation. This lets a transition converge while server
movement continues; all presentation behavior above is unchanged.

## 2026-09-03 content-backed demo mapping

The owner approved the base-game Option A mapping for the Phase 8 demo:

- interior cell `Seyda Neen, Census and Excise Office`;
- exterior worldspace `sys::default`; and
- remote avatar NPC `player`.

These remain explicit command-line fixture values, not protocol or canonical
state. Their content records must be validated against licensed `Morrowind.esm`
before the demo can count as evidence. Any replacement mapping requires owner
review before use.
