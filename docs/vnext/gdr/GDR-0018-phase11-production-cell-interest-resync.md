# GDR-0018: Phase 11 production cell, interest, and resync behavior

Status: **Implemented**

Date opened: 2026-09-06

Decision owner: project owner

Companion architecture record:
[`ADR-0058`](../adr/ADR-0058-phase11-cell-catalog-interest-baseline.md)

## Decision

The owner approved A/A/A:

1. A manifest declares at most 256 typed cell spaces and 4,096 exact cells.
   Interior cells and exterior worldspace/grid tuples are sorted, unique, and
   closed to client proposals outside the catalog. OpenMW owns a complete,
   injective, case-insensitive local record map.
2. Every active player observes exactly the active players in the same canonical
   `CellId`. Only the server derives membership.
3. Join, resume, and authenticated resync deliver a reliable complete membership
   baseline plus a latest-wins spatial snapshot. A client becomes ready only when
   it has both and the snapshot revision is at least the baseline revision.
   Applying a newer baseline atomically replaces membership. Resync requests are
   metadata-only and coalesced to one pending request per connection generation.

## Compatibility and limits

The profile advances from protocol 1.1 to 1.2 and scalar cell configuration is
replaced. Player-registry identity, canonical checksum version 2, server commit
authority, and the presentation-only pose lane do not change. This decision adds
no movement tuning, collision, teleport policy, persistence, adjacent-cell
visibility, or world-object streaming.

## Acceptance evidence

Contract tests cover bounded catalog parsing and exact admission, baseline and
resync codecs, atomic client replacement/completion, same-cell projection,
initial join delivery, request coalescing, and mapping collision rejection.
Affected protocol, client, server-app, adapter, integration, and OpenMW builds are
recorded in the rolling implementation notes.

## Owner approval

Approved as A/A/A on 2026-09-06.
