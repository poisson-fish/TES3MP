# ADR-0062: Phase 15 server-authoritative inventory transactions

Status: **Accepted**

Date opened: 2026-09-08

Date approved: 2026-09-08

Decision owner: project owner

Companion gameplay record:
[`GDR-0022`](../gdr/GDR-0022-phase15-inventory-equipment-containers.md)

## Decision

The owner approved Package A from the Phase 15 discovery:

1. Item prototypes have stable manifest-scoped identity (`ItemPrototypeId`), base
   weight, value, equipment slot masks, and maximum condition/charge, declared in a
   bounded manifest-bound `ItemPrototypeCatalog`.
2. The server canonical world owns player inventory (`CanonicalPlayerInventory`) and
   exact-cell container inventories (`CanonicalContainerInventory`).
3. Item transfers (take, put, drop, pickup) and equipment slot changes are
   single-command atomic transactions evaluated by the server reducer inside fixed
   ticks.
4. Transaction evaluation derives the player cell and root from canonical server
   state, then enforces same-cell matching, reach bounds (<= 384 units between the
   player root and container/item), exact source stack identity and quantity,
   destination capacity, and optimistic concurrency via `InventoryRevision` and
   `ContainerRevision`.
5. Reducer execution is atomic: both source and destination mutate together, or the
   entire transaction rolls back with an explicit fail-closed outcome.
6. Authoritative key verification: held keys in player canonical inventory populate
   `ObjectInteractionValidationContext::verifiedPlayerKeys`, authoritatively
   enabling Phase 14 `UnlockWithKey` door and container interactions.
7. Private player inventory is replicated reliably to the owning client only. Remote
   clients receive only public equipment slot changes (19 slots) for 3D mesh
   rendering.
8. Unloaded cells freeze container state in canonical memory without ticking.
9. Trading, bartering, merchant restocking, lockpicking minigames, combat durability
   loss, and disk persistence remain excluded from Phase 15.

## First implementation boundary

The first pass implements the engine-independent item prototype catalog, canonical
inventory, container, and cell-scoped ground-item state, key verification bridge,
reach/capacity validation rules, and atomic transfer reducer in `components/tes3mp`.
Whole-stack moves preserve globally unique stack identity; partial moves allocate a
new bounded identity. Mutable canonical state remains encapsulated behind the reducer.
Protocol replication, wire codecs, and OpenMW presentation follow.

## Consequences

- Legacy TES3MP 0.8.1 client-dictated packets (`PacketPlayerInventory`,
  `PacketContainer`) and Lua forwarding remain excluded.
- Reducer-level item duplication and race conditions from concurrent looting are
  prevented by atomic mutation and revision fencing; disconnect composition remains
  part of the later server-integration work.
- Inventory privacy is protected: other players cannot inspect backpack contents.
- Visual equipment synchronization uses minimal bandwidth (19 slots).

## Approval

The project owner approved Package A in the 2026-09-08 working session.
