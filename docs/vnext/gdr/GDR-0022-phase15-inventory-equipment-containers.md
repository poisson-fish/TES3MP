# GDR-0022: Phase 15 inventory, equipment, and container transactions

Status: **Accepted**

Date opened: 2026-09-08

Date approved: 2026-09-08

Decision owner: project owner

Companion architecture record:
[`ADR-0062`](../adr/ADR-0062-phase15-server-authoritative-inventory-transactions.md)

## Decision

The owner approved the bounded server-authoritative inventory and container package:

1. Items are instances of manifest-declared item prototypes. Item stacks merge only
   when prototype ID, condition/health, enchantment charge, and trapped actor prototype
   are identical.
2. Players own private backpacks and 19 discrete equipment slots (Helmet, Cuirass,
   Greaves, Pauldrons, Gauntlets, Boots, Shirt, Pants, Skirt, Robe, Rings, Amulet,
   Belt, Weapon, Shield/Torch, Ammunition).
3. Containers (chests, crates, urns, sacks, corpses) in exact cells hold item stacks
   governed by weight capacity.
4. Moving items between player inventory and containers, or dropping/picking up world
   items, is an atomic transaction. If concurrent looters attempt to take the same
   item, the first committed transaction succeeds and the second fails safely
   without duplication or loss.
5. Equipping an item validates the prototype's slot mask and updates the player's
   public equipment presentation.
6. Keys in player canonical inventory enable door and container unlocking via the
   Phase 14 key verification bridge.
7. Reach validation derives player location from canonical server state and enforces
   that players must be in the same exact cell and within bounded distance (384 units)
   of the container or ground item.
8. Unloaded cells freeze container state in canonical memory without loss.

## Bounds

- At most 65,536 item prototypes per manifest.
- At most 1,024 item stacks per player inventory.
- At most 512 item stacks per container.
- At most 65,536 cell-scoped ground-item stacks.
- Stack counts use bounded unsigned 32-bit integers with overflow protection.
- Item stack IDs are globally unique across players, containers, and ground items;
  whole-stack moves preserve identity and splits allocate a new monotonic ID.
- Equipment covers exactly 19 discrete slots (0 to 18).

## Deferred behavior

Barter, trade windows, merchant restocking, lockpicking minigames, probe trap disarming,
weapon/armor durability damage from hits, full spell casting, and disk persistence
remain deferred to later phases.

## Approval

The project owner approved Package A in the 2026-09-08 working session.
