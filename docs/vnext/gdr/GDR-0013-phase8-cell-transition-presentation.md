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
