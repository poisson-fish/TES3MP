# Phase 15 inventory, equipment, and container transactions discovery

Date: 2026-09-08

Status: **Complete; Package A approved**

This pass traces OpenMW player inventory, container store, equipment slots, container
activation, item stacking, and looting mechanics; reviews legacy TES3MP 0.8.x
vulnerabilities and requirements evidence; and defines the smallest production
inventory, equipment, and container foundation. It changes no runtime, protocol,
authority, persistence, movement, or rendering behavior.

## Current boundary

| Area / Lane | Repository-backed OpenMW behavior | Current bound or gap |
|---|---|---|
| Player inventory & stacking | MWWorld::InventoryStore and MWWorld::ContainerStore manage categorized item lists (potions, apparatus, armor, books, clothing, ingredients, lights, lockpicks, misc, probes, repair tools, weapons). Stacking requires matching RefId, health/charge, enchantment charge, and trapped soul. Total weight and encumbrance affect movement speed and fatigue. | In vNext server core, there is no canonical inventory state, item prototype catalog, stack tracking, or weight calculation. Player spatial state exists, but player possession is entirely absent. |
| Equipment slots & presentation | MWWorld::InventoryStore maintains 19 discrete equipment slots (Helmet, Cuirass, Greaves, Pauldrons, Gauntlets, Boots, Clothing, Jewelry, CarriedRight weapon, CarriedLeft shield/torch, Ammunition). Equipping modifies actor stats, active magic effects, and attaches 3D models via MWRender::NpcAnimation. | In vNext, remote player presentation renders only base appearance presets and canonical locomotion roots. Replicated actors have no equipment mesh attachment or weapon/shield visibility. |
| World containers & corpses | MWClass::Container owns ContainerStore for placed chests, crates, urns, and sacks. MWWorld::LiveCellRef<ESM::Container> tracks container weight capacity. Dead NPCs/creatures expose their inventory for looting. Opening an unlocked, untrapped container pushes MWGui::GM_Container. | Phase 14 introduced InteractiveObjectCatalog and CanonicalInteractiveObjectWorld for door lock and trap states, but containers and their item contents are not modeled there. |
| Container transfer mechanics | In single-player OpenMW, MWGui::ContainerItemModel directly adds and removes items between ContainerStore instances in local memory (addItem, copyItem, removeItem). | Multiplayer cannot allow client-local frame simulation to mutate inventories or create/destroy items. Container-to-player and player-to-container transfers must be atomic transactions to prevent duplication and item loss. |
| Key verification & unlocking | In single-player, Container::activate and Door::activate inspect player InventoryStore for CellRef::getKey(). If found, the object is unlocked and disarmed. In vNext Phase 14, applyObjectInteraction accepts UnlockWithKey but explicitly rejects it fail-closed because verifiedPlayerKeys is not yet populated from canonical state. | Phase 15 must supply the canonical key ownership bridge: the server validates key possession in player canonical inventory before authorizing UnlockWithKey on doors or containers. |
| Dropped & placed items | Single-player OpenMW represents world items as discrete cell references in MWWorld::CellRefList. Dropping an item creates a placed cell reference; picking up an item removes the cell reference and adds it to InventoryStore. | In vNext, dropping and picking up items requires cell-scoped placement tracking and transactional exchange with player inventory, subject to same-cell and reach checks. |
| Legacy TES3MP 0.8.x vulnerabilities | Archived 0.8.x used uncoupled client-authored packets: PacketPlayerInventory (ID_PLAYER_INVENTORY), PacketContainer (ID_CONTAINER), and PacketPlayerEquipment (ID_PLAYER_EQUIPMENT). The server forwarded packets directly to external Lua scripts (OnPlayerInventory, OnContainer) without transactional atomicity. | Critical vulnerabilities in legacy multiplayer: no transactional coupling between container removal and player inventory addition (allowing rampant item duplication), no reach or cell validation on container access, client-dictated stack counts, and no optimistic concurrency control for multiple players looting the same container simultaneously. |

## Separation of lanes

To preserve vNext invariants, inventory, equipment, and container operations require
strict separation across five distinct lanes:

1. **Durable / Canonical inventory & container state (server-owned):**
   - **Item prototype catalog:** Bounded manifest-scoped catalog declaring valid
     item prototypes (ItemPrototypeId), base weight, value, equipment slot mask,
     and maximum condition/charge. OpenMW maps ItemPrototypeId locally to ESM::RefId.
   - **Canonical player inventory:** Server-owned mapping of item stacks per player.
     Each stack has a stable ItemStackId, ItemPrototypeId, count, condition/charge,
     enchantment charge, and optional trapped actor prototype. Tracks 19 equipment slot
     bindings and a monotonic InventoryRevision.
   - **Canonical container inventory:** Server-owned mapping of container contents for
     placed containers and corpses in exact cells. Tracks container contents, capacity,
     and a monotonic ContainerRevision.
   - **Authoritative key ownership:** Fast query deriving verified KeyPrototypeIds
     from player canonical inventory to satisfy ObjectInteractionValidationContext::verifiedPlayerKeys.

2. **Reliable transaction commands (client -> server):**
   - **Transfer command (ClientTransferItemCommand):** Bounded, authenticated command
     on the reliable channel with a unique command ID. Specifies source (player or
     container), destination (container or player), exact source ItemStackId,
     ItemPrototypeId, transfer count, expected InventoryRevision, and expected
     ContainerRevision.
   - **Equip command (ClientEquipItemCommand):** Specifies slot index (0-18),
     target ItemStackId (or none to unequip), and expected InventoryRevision.
   - **Drop / Pickup commands (ClientDropItemCommand, ClientPickupItemCommand):**
     Atomic transfer between player inventory and cell-placed world items with
     interaction origin and reach validation.

3. **Canonical reducer outcomes (server-evaluated):**
   - **Validation invariants:** Server derives the player root and exact cell from
     canonical server state, then checks reach to the container/item (<= 384 units),
     expected revision matching (optimistic concurrency), item existence and sufficient
     count in source, and capacity/encumbrance limits.
   - **Atomic commit:** A transfer either completes fully (decrementing source and
     incrementing destination in the same tick) or aborts completely with zero state
     mutation and an explicit typed outcome code (ItemNotFound, InsufficientCount,
     StaleRevision, OutOfReach, ContainerFull).
   - **Safe stack merging:** Items only merge into an existing stack when prototype,
     condition, enchantment charge, and trapped soul are identical.

4. **Interest projection & baseline replication:**
   - **Private player inventory:** Joining, resuming, or resyncing delivers a complete
     authoritative ReliablePlayerInventoryBaseline to the owning player only. Private
     inventory contents are never broadcast to other players.
   - **Public equipment presentation:** Remote players receive only public equipment
     slot updates (PlayerEquipmentBaseline / latest-wins equipment views) containing
     visible item prototypes across the 19 slots for mesh rendering.
   - **Exact-cell container interest:** Observing a cell or opening a container delivers
     a complete ReliableContainerInventoryBaseline. Inactive cells freeze container
     state in canonical memory without ticking.
   - **Exact-cell ground-item interest:** Observing a cell delivers canonical placed-item
     identities, stacks, positions, and revisions so drop/pickup presentation cannot
     invent or retain items independently of the server world.

5. **Presentation & local UI (client-only):**
   - OpenMW GUI (InventoryWindow, ContainerWindow) displays items from authoritative
     baselines.
   - Client-side prediction of transfers is reversible and reconciled against server
     outcomes; rejected transactions restore local visual items immediately.
   - 3D equipment mesh attachment (MWRender::NpcAnimation) reflects confirmed equipment
     slots for local and remote players.
   - Sound effects (item pickup, drop, gold jingling, armor equip) and message box
     notifications are client-local presentation and never author canonical state.

## Recommended production seam

1. **Manifest-bound item prototype catalog:** Add bounded manifest-scoped
   ItemPrototypeCatalog declaring valid item prototypes (at most 65,536 items per
   manifest). Maps opaque ItemPrototypeId to base item properties.
2. **Canonical inventory world:** Add CanonicalInventoryWorld to server core,
   managing player inventories and cell container inventories with strict capacity
   bounds, monotonic revisions, and overflow-safe arithmetic.
3. **Key ownership query:** Provide a server-side bridge extracting verified
   KeyPrototypeIds from player inventory to populate verifiedPlayerKeys in
   ObjectInteractionValidationContext, enabling UnlockWithKey on doors and preparing
   the future container interaction path.
4. **Atomic transfer reducer:** Implement applyInventoryTransaction executing
   exact-stack transfers as single-command atomic operations inside the fixed scheduler
   tick, deriving spatial authority from canonical server state and enforcing optimistic
   concurrency control (InventoryRevision, ContainerRevision).
5. **Capability-negotiated protocol:** Add inventoryReplicationCapability()
   delivering private player inventory baselines, public equipment views, and
   cell container baselines via FlatBuffers wire schemas.

## Owner package choices

These choices materially affect architecture, network overhead, security, and authority.
Select one package before implementation:

- **Package A — server-authoritative inventory with atomic preflight and commit (recommended):**
  The server canonical world owns player and container inventories. All transfers (take,
  put, drop, pickup, equip) execute as single-command atomic transactions with optimistic
  concurrency control (InventoryRevision, ContainerRevision), same-cell and reach
  enforcement, and capacity bounds. Clients receive authoritative baselines for their own
  inventory and opened containers; remote clients receive only public equipment slots for
  mesh rendering. This eliminates item duplication, race conditions, and inventory desync
  while preserving privacy, security, and ADR-0006 compliance.
- **Package B — client-optimistic inventory with delayed server reconciliation:**
  Clients immediately apply inventory and container operations locally and report mutations
  to the server. The server verifies feasibility asynchronously and issues rollbacks on
  conflicts or race conditions. This reopens client authority vulnerabilities, introduces
  complex rollback cascades for items that may have already been consumed, dropped, or
  re-transferred, and risks visual inventory rubber-banding and desync.
- **Package C — legacy-style decoupled packet streaming:**
  Reimplements separate PlayerInventoryPacket and ContainerPacket streams with
  client-dictated additions and removals forwarded to scripting. This recreates legacy
  TES3MP 0.8.x duplication glitches, lacks transaction atomicity, requires unbounded
  script intervention, and violates ADR-0006 invariants.

Package A is the recommendation. It establishes a robust, bounded, server-authoritative
foundation that closes the legacy reducer-level duplication path and preserves a clean
privacy separation for the replication work that follows.

## Owner decision

The project owner approved Package A on 2026-09-08. The durable decision is
recorded in [ADR-0062](adr/ADR-0062-phase15-server-authoritative-inventory-transactions.md)
and [GDR-0022](gdr/GDR-0022-phase15-inventory-equipment-containers.md).

## Named proof scenarios

1. item_catalog_is_manifest_bound_bounded_and_unique
2. player_inventory_is_server_owned_and_isolated_from_other_players
3. container_transfer_is_atomic_with_single_command_commit_or_rollback
4. item_duplication_prevented_under_concurrent_container_access
5. transfer_rejected_when_container_is_out_of_reach_or_different_cell
6. transfer_rejected_when_item_count_exceeds_available_stack
7. stack_merging_requires_identical_prototype_condition_charge_and_soul
8. equipment_slot_assignment_validates_prototype_slot_mask_and_revises_state
9. remote_presentation_receives_only_public_equipment_not_private_backpack
10. canonical_key_ownership_authoritatively_satisfies_object_unlock_validation
11. unloaded_cell_preserves_container_inventory_without_ticking
12. late_join_resume_and_resync_receive_complete_authoritative_inventory_baseline
13. ground_item_drop_and_pickup_preserve_canonical_item_conservation
