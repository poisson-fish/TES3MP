#include <tes3mp/content_identity.hpp>
#include <tes3mp/interactive_object_world.hpp>
#include <tes3mp/inventory_world.hpp>

#include <cassert>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <type_traits>

using namespace TES3MP;

namespace
{
    static_assert(std::is_same_v<decltype(CanonicalItemStack::soulPrototype), std::optional<ActorPrototypeId>>);
    static_assert(
        std::is_same_v<decltype(std::declval<CanonicalInventoryWorld&>().findPlayer(std::declval<PlayerId>())),
            const CanonicalPlayerInventoryState*>);

    void require(bool condition, int line)
    {
        if (!condition)
        {
            std::cerr << "inventory_world_tests assertion failed at line " << line << '\n';
            std::abort();
        }
    }

#undef assert
#define assert(condition) require(static_cast<bool>(condition), __LINE__)

    template <class T>
    T id(std::uint64_t value)
    {
        return *T::fromValue(value);
    }

    Transform tr(std::uint64_t cell = 7, std::int64_t x = 0, std::int64_t y = 0, std::int64_t z = 0)
    {
        const auto zero = Turn32::fromValue(0);
        return Transform(CellId::interior(id<CellSpaceId>(cell)), Position3(x, y, z), Orientation3(zero, zero, zero));
    }

    CanonicalServerState players(std::uint64_t cell = 7, std::int64_t x = 0, std::uint64_t playerId = 1,
        std::int64_t y = 0, std::int64_t z = 0, std::uint64_t lastSpatialTick = 0)
    {
        const std::array values{ CanonicalPlayerEntityState(id<PlayerId>(playerId), id<EntityId>(100 + playerId),
            id<AppearanceId>(1), tr(cell, x, y, z), LinearVelocity3(0, 0, 0), EntityRevision::initial(),
            AuthorityEpoch::initial(), id<ServerTick>(lastSpatialTick)) };
        const std::array sessions{ CanonicalSessionProgress(id<SessionId>(playerId), SessionGeneration::initial(),
            id<PlayerId>(playerId), id<EntityId>(100 + playerId), std::nullopt) };
        return std::get<CanonicalServerState>(createCanonicalServerState(values, sessions));
    }

    ItemPrototypeCatalog createTestCatalog(const ContentManifest& manifest)
    {
        const std::vector<ItemPrototypeDeclaration> decls = {
            ItemPrototypeDeclaration{
                .id = id<ItemPrototypeId>(1),
                .category = ItemCategory::Miscellaneous,
                .weightUnits = 1,
                .value = 1,
                .slotMask = 0,
                .stackable = true,
                .keyId = std::nullopt,
            },
            ItemPrototypeDeclaration{
                .id = id<ItemPrototypeId>(2),
                .category = ItemCategory::Armor,
                .weightUnits = 100, // 10.0 lbs
                .value = 50,
                .maxCondition = 100,
                .slotMask = slotToMask(EquipmentSlot::Cuirass),
                .stackable = false,
                .keyId = std::nullopt,
            },
            ItemPrototypeDeclaration{
                .id = id<ItemPrototypeId>(3),
                .category = ItemCategory::Armor,
                .weightUnits = 30, // 3.0 lbs
                .value = 25,
                .maxCondition = 50,
                .slotMask = slotToMask(EquipmentSlot::Helmet),
                .stackable = false,
                .keyId = std::nullopt,
            },
            ItemPrototypeDeclaration{
                .id = id<ItemPrototypeId>(4),
                .category = ItemCategory::Miscellaneous,
                .weightUnits = 2,
                .value = 10,
                .slotMask = 0,
                .stackable = false,
                .keyId = id<KeyPrototypeId>(42),
            },
            ItemPrototypeDeclaration{
                .id = id<ItemPrototypeId>(5),
                .category = ItemCategory::Clothing,
                .weightUnits = 1,
                .value = 10,
                .slotMask = slotToMask(EquipmentSlot::LeftRing) | slotToMask(EquipmentSlot::RightRing),
                .stackable = true,
            },
        };

        const auto cat = ItemPrototypeCatalog::create(manifest, decls);
        assert(cat.has_value());
        return *cat;
    }

    void testKeyCollectionAndDoorUnlockIntegration()
    {
        const auto manifest = testContentManifest();
        const auto catalog = createTestCatalog(manifest);

        const auto player1 = id<PlayerId>(1);
        const auto player2 = id<PlayerId>(2);

        // Player 1 has Key 42 (item prototype 4)
        CanonicalPlayerInventoryState p1State{
            .player = player1,
            .revision = id<InventoryRevision>(1),
            .stacks = {
                CanonicalItemStack{
                    .stackId = id<ItemStackId>(1),
                    .prototypeId = id<ItemPrototypeId>(4),
                    .count = 1,
                },
            },
        };

        // Player 2 has only Gold (item prototype 1)
        CanonicalPlayerInventoryState p2State{
            .player = player2,
            .revision = id<InventoryRevision>(1),
            .stacks = {
                CanonicalItemStack{
                    .stackId = id<ItemStackId>(2),
                    .prototypeId = id<ItemPrototypeId>(1),
                    .count = 100,
                },
            },
        };

        auto world = CanonicalInventoryWorld::create(
            manifest, catalog, std::vector<CanonicalPlayerInventoryState>{ p1State, p2State }, {});
        assert(world.has_value());

        // Verify key extraction
        const auto p1Keys = world->collectVerifiedKeys(player1);
        assert(p1Keys.size() == 1);
        assert(p1Keys[0] == id<KeyPrototypeId>(42));

        const auto p2Keys = world->collectVerifiedKeys(player2);
        assert(p2Keys.empty());

        // Integration with Phase 14 applyObjectInteraction
        const auto cellId = CellId::interior(id<CellSpaceId>(7));
        const auto doorId = id<InteractiveObjectId>(10);
        const Transform doorTransform = tr(7, 100);

        InteractiveObjectCatalogEntry doorEntry{
            .objectId = doorId,
            .kind = InteractiveObjectKind::StandardDoor,
            .cell = cellId,
            .transform = doorTransform,
            .destination = std::nullopt,
            .lock = ObjectLockDeclaration{
                .lockedByDefault = true,
                .lockLevel = 50,
                .keyId = id<KeyPrototypeId>(42),
            },
            .trap = ObjectTrapDeclaration{},
        };

        const auto objCatalog = InteractiveObjectCatalog::create(manifest, std::vector{ doorEntry });
        assert(objCatalog.has_value());

        auto objWorldResult = createInitialCanonicalInteractiveObjectWorld(*objCatalog);
        assert(std::holds_alternative<CanonicalInteractiveObjectWorld>(objWorldResult));
        auto objWorld = std::get<CanonicalInteractiveObjectWorld>(std::move(objWorldResult));

        // Player 2 attempts to unlock door with key 42 -> fails because verified keys is empty
        const auto playerPos = Position3(100, 0, 0);
        const ObjectInteractionValidationContext p2Context{
            .maxReach = 384,
            .verifiedPlayerKeys = p2Keys,
        };

        InteractObjectCommand unlockCmd{
            .player = player2,
            .objectId = doorId,
            .cell = cellId,
            .interactionOrigin = playerPos,
            .expectedRevision = ObjectRevision::initial(),
            .kind = ObjectInteractionKind::UnlockWithKey,
            .requestedKey = id<KeyPrototypeId>(42),
        };

        auto outcomeP2 = applyObjectInteraction(
            objWorld, *objCatalog, players(7, 100, 2), unlockCmd, id<ServerTick>(1), p2Context);
        assert(outcomeP2.outcome.code == ObjectInteractionResultCode::Locked);

        // Player 1 attempts to unlock door with key 42 -> succeeds because verified keys contains key 42!
        const ObjectInteractionValidationContext p1Context{
            .maxReach = 384,
            .verifiedPlayerKeys = p1Keys,
        };

        unlockCmd.player = player1;
        auto outcomeP1 = applyObjectInteraction(
            objWorld, *objCatalog, players(7, 100, 1), unlockCmd, id<ServerTick>(1), p1Context);
        assert(outcomeP1.outcome.code == ObjectInteractionResultCode::Success);
        assert(outcomeP1.outcome.newLockState == LockState::Unlocked);
        assert(outcomeP1.outcome.newRevision == id<ObjectRevision>(2));
    }

    void testContainerTransferTakeAndStackMerging()
    {
        const auto manifest = testContentManifest();
        const auto catalog = createTestCatalog(manifest);

        const auto player = id<PlayerId>(1);
        const auto containerId = id<ContainerId>(1);
        const auto cellId = CellId::interior(id<CellSpaceId>(7));

        CanonicalPlayerInventoryState pState{
            .player = player,
            .revision = id<InventoryRevision>(1),
            .stacks = {
                CanonicalItemStack{
                    .stackId = id<ItemStackId>(1),
                    .prototypeId = id<ItemPrototypeId>(1),
                    .count = 10,
                },
            },
        };

        CanonicalContainerInventoryState cState{
            .containerId = containerId,
            .cell = cellId,
            .position = Position3(100, 100, 0),
            .revision = id<ContainerRevision>(1),
            .capacityWeight = 500,
            .stacks = {
                CanonicalItemStack{
                    .stackId = id<ItemStackId>(2),
                    .prototypeId = id<ItemPrototypeId>(1),
                    .count = 50,
                },
            },
        };

        auto world = CanonicalInventoryWorld::create(manifest, catalog, std::vector{ pState }, std::vector{ cState });
        assert(world.has_value());

        const InventoryValidationContext context{ .maxReach = 384 };
        const auto serverPlayers = players(7, 100, 1, 100);

        // Take 25 from container
        InventoryTransactionCommand cmd{
            .player = player,
            .kind = InventoryTransactionKind::TakeFromContainer,
            .containerId = containerId,
            .prototypeId = id<ItemPrototypeId>(1),
            .stackId = id<ItemStackId>(2),
            .count = 25,
            .expectedInventoryRevision = id<InventoryRevision>(1),
            .expectedContainerRevision = id<ContainerRevision>(1),
            .interactionOrigin = Position3(100, 100, 0),
        };

        auto ambiguousCommand = cmd;
        ambiguousCommand.stackId = std::nullopt;
        const auto beforeAmbiguousCommand = *world;
        assert(applyInventoryTransaction(*world, serverPlayers, ambiguousCommand, context, id<ServerTick>(1)).code
            == InventoryTransactionResultCode::ItemNotFound);
        assert(*world == beforeAmbiguousCommand);

        const auto outcome = applyInventoryTransaction(*world, serverPlayers, cmd, context, id<ServerTick>(1));
        assert(outcome.code == InventoryTransactionResultCode::Success);
        assert(outcome.newInventoryRevision == id<InventoryRevision>(2));
        assert(outcome.newContainerRevision == id<ContainerRevision>(2));

        const auto* updatedPlayer = world->findPlayer(player);
        assert(updatedPlayer->stacks.size() == 1);
        assert(updatedPlayer->stacks[0].count == 35); // Merged 10 + 25

        const auto* updatedContainer = world->findContainer(containerId);
        assert(updatedContainer->stacks.size() == 1);
        assert(updatedContainer->stacks[0].count == 25); // 50 - 25
    }

    void testConcurrentAccessPreventsDuplication()
    {
        const auto manifest = testContentManifest();
        const auto catalog = createTestCatalog(manifest);

        const auto player1 = id<PlayerId>(1);
        const auto player2 = id<PlayerId>(2);
        const auto containerId = id<ContainerId>(1);
        const auto cellId = CellId::interior(id<CellSpaceId>(7));

        CanonicalPlayerInventoryState p1State{ .player = player1 };
        CanonicalPlayerInventoryState p2State{ .player = player2 };

        CanonicalContainerInventoryState cState{
            .containerId = containerId,
            .cell = cellId,
            .position = Position3(50, 50, 0),
            .revision = id<ContainerRevision>(1),
            .stacks = {
                CanonicalItemStack{
                    .stackId = id<ItemStackId>(1),
                    .prototypeId = id<ItemPrototypeId>(2), // Unique cuirass
                    .count = 1,
                },
            },
        };

        auto world = CanonicalInventoryWorld::create(
            manifest, catalog, std::vector{ p1State, p2State }, std::vector{ cState });
        assert(world.has_value());

        const InventoryValidationContext context{ .maxReach = 384 };

        // Both players send Take command expecting ContainerRevision 1
        InventoryTransactionCommand cmdP1{
            .player = player1,
            .kind = InventoryTransactionKind::TakeFromContainer,
            .containerId = containerId,
            .prototypeId = id<ItemPrototypeId>(2),
            .stackId = id<ItemStackId>(1),
            .count = 1,
            .expectedInventoryRevision = id<InventoryRevision>(1),
            .expectedContainerRevision = id<ContainerRevision>(1),
            .interactionOrigin = Position3(50, 50, 0),
        };

        InventoryTransactionCommand cmdP2{
            .player = player2,
            .kind = InventoryTransactionKind::TakeFromContainer,
            .containerId = containerId,
            .prototypeId = id<ItemPrototypeId>(2),
            .stackId = id<ItemStackId>(1),
            .count = 1,
            .expectedInventoryRevision = id<InventoryRevision>(1),
            .expectedContainerRevision = id<ContainerRevision>(1),
            .interactionOrigin = Position3(50, 50, 0),
        };

        // First player succeeds
        const auto outcome1
            = applyInventoryTransaction(*world, players(7, 50, 1, 50), cmdP1, context, id<ServerTick>(1));
        assert(outcome1.code == InventoryTransactionResultCode::Success);

        // Second player fails with StaleContainerRevision (preventing item duplication!)
        const auto outcome2
            = applyInventoryTransaction(*world, players(7, 50, 2, 50), cmdP2, context, id<ServerTick>(1));
        assert(outcome2.code == InventoryTransactionResultCode::StaleContainerRevision);

        // Verify player 1 has item, player 2 has none, container is empty
        assert(world->findPlayer(player1)->stacks.size() == 1);
        assert(world->findPlayer(player2)->stacks.empty());
        assert(world->findContainer(containerId)->stacks.empty());
    }

    void testReachAndCellValidation()
    {
        const auto manifest = testContentManifest();
        const auto catalog = createTestCatalog(manifest);

        const auto player = id<PlayerId>(1);
        const auto containerId = id<ContainerId>(1);
        const auto cellId1 = CellId::interior(id<CellSpaceId>(7));
        const auto cellId2 = CellId::exterior(id<CellSpaceId>(8), 0, 0);

        CanonicalPlayerInventoryState pState{ .player = player };
        CanonicalContainerInventoryState cState{
            .containerId = containerId,
            .cell = cellId1,
            .position = Position3(0, 0, 0),
            .stacks = {
                CanonicalItemStack{
                    .stackId = id<ItemStackId>(1),
                    .prototypeId = id<ItemPrototypeId>(1),
                    .count = 10,
                },
            },
        };

        auto world = CanonicalInventoryWorld::create(manifest, catalog, std::vector{ pState }, std::vector{ cState });
        assert(world.has_value());

        // Cell mismatch test
        const InventoryValidationContext context{ .maxReach = 384 };

        InventoryTransactionCommand cmd{
            .player = player,
            .kind = InventoryTransactionKind::TakeFromContainer,
            .containerId = containerId,
            .prototypeId = id<ItemPrototypeId>(1),
            .stackId = id<ItemStackId>(1),
            .count = 1,
            .expectedInventoryRevision = id<InventoryRevision>(1),
            .expectedContainerRevision = id<ContainerRevision>(1),
            .interactionOrigin = Position3(0, 0, 0),
        };

        auto outcome = applyInventoryTransaction(*world, players(8), cmd, context, id<ServerTick>(1));
        assert(outcome.code == InventoryTransactionResultCode::CellMismatch);

        // Out of reach test (> 384 units)
        outcome = applyInventoryTransaction(*world, players(7, 500), cmd, context, id<ServerTick>(1));
        assert(outcome.code == InventoryTransactionResultCode::PlayerOutOfReach);

        const auto beforeMissingPlayer = *world;
        outcome = applyInventoryTransaction(*world, players(7, 0, 2), cmd, context, id<ServerTick>(1));
        assert(outcome.code == InventoryTransactionResultCode::PlayerNotFound);
        assert(*world == beforeMissingPlayer);

        outcome = applyInventoryTransaction(*world, players(7, 0, 1, 0, 0, 2), cmd, context, id<ServerTick>(1));
        assert(outcome.code == InventoryTransactionResultCode::TickRegression);
        assert(*world == beforeMissingPlayer);
    }

    void testEquipmentSlotAssignment()
    {
        const auto manifest = testContentManifest();
        const auto catalog = createTestCatalog(manifest);

        const auto player = id<PlayerId>(1);

        CanonicalPlayerInventoryState pState{
            .player = player,
            .stacks = {
                CanonicalItemStack{
                    .stackId = id<ItemStackId>(10),
                    .prototypeId = id<ItemPrototypeId>(2), // Cuirass
                    .count = 1,
                },
                CanonicalItemStack{
                    .stackId = id<ItemStackId>(20),
                    .prototypeId = id<ItemPrototypeId>(3), // Helmet
                    .count = 1,
                },
            },
        };

        auto world = CanonicalInventoryWorld::create(manifest, catalog, std::vector{ pState }, {});
        assert(world.has_value());

        const InventoryValidationContext context{ .maxReach = 384 };
        const auto serverPlayers = players();

        // Attempt to equip Cuirass into Helmet slot -> Rejected
        InventoryTransactionCommand badEquip{
            .player = player,
            .kind = InventoryTransactionKind::EquipItem,
            .prototypeId = id<ItemPrototypeId>(2),
            .stackId = id<ItemStackId>(10),
            .slot = EquipmentSlot::Helmet,
            .expectedInventoryRevision = id<InventoryRevision>(1),
            .interactionOrigin = Position3(0, 0, 0),
        };

        auto outcome = applyInventoryTransaction(*world, serverPlayers, badEquip, context, id<ServerTick>(1));
        assert(outcome.code == InventoryTransactionResultCode::SlotNotCompatible);

        // Equip Cuirass into Cuirass slot -> Success
        InventoryTransactionCommand goodEquip{
            .player = player,
            .kind = InventoryTransactionKind::EquipItem,
            .prototypeId = id<ItemPrototypeId>(2),
            .stackId = id<ItemStackId>(10),
            .slot = EquipmentSlot::Cuirass,
            .expectedInventoryRevision = id<InventoryRevision>(1),
            .interactionOrigin = Position3(0, 0, 0),
        };

        outcome = applyInventoryTransaction(*world, serverPlayers, goodEquip, context, id<ServerTick>(1));
        assert(outcome.code == InventoryTransactionResultCode::Success);
        assert(outcome.newInventoryRevision == id<InventoryRevision>(2));

        const auto* updatedPlayer = world->findPlayer(player);
        assert(updatedPlayer->equipment[static_cast<std::size_t>(EquipmentSlot::Cuirass)] == id<ItemStackId>(10));

        // Unequip Cuirass
        InventoryTransactionCommand unequipCmd{
            .player = player,
            .kind = InventoryTransactionKind::UnequipItem,
            .prototypeId = id<ItemPrototypeId>(2),
            .slot = EquipmentSlot::Cuirass,
            .expectedInventoryRevision = id<InventoryRevision>(2),
            .interactionOrigin = Position3(0, 0, 0),
        };

        outcome = applyInventoryTransaction(*world, serverPlayers, unequipCmd, context, id<ServerTick>(1));
        assert(outcome.code == InventoryTransactionResultCode::Success);
        assert(outcome.newInventoryRevision == id<InventoryRevision>(3));

        const auto* unequippedPlayer = world->findPlayer(player);
        assert(!unequippedPlayer->equipment[static_cast<std::size_t>(EquipmentSlot::Cuirass)].has_value());
    }

    void testBrokenWeaponCannotBeEquipped()
    {
        const auto manifest = testContentManifest();
        const std::array declarations{ ItemPrototypeDeclaration{ id<ItemPrototypeId>(20), ItemCategory::Weapon,
            10, 10, 100, 0, slotToMask(EquipmentSlot::CarriedRight), false, std::nullopt } };
        const auto catalog = *ItemPrototypeCatalog::create(manifest, declarations);
        CanonicalPlayerInventoryState player{ .player = id<PlayerId>(1),
            .stacks = { { id<ItemStackId>(20), id<ItemPrototypeId>(20), 1, 0, 0, std::nullopt } } };
        auto world = *CanonicalInventoryWorld::create(manifest, catalog, std::array{ player }, {});
        const InventoryTransactionCommand command{ .player = id<PlayerId>(1),
            .kind = InventoryTransactionKind::EquipItem,
            .prototypeId = id<ItemPrototypeId>(20),
            .stackId = id<ItemStackId>(20),
            .slot = EquipmentSlot::CarriedRight,
            .expectedInventoryRevision = InventoryRevision::initial(),
            .interactionOrigin = Position3(0, 0, 0) };
        const auto outcome = applyInventoryTransaction(
            world, players(), command, InventoryValidationContext{}, id<ServerTick>(1));
        assert(outcome.code == InventoryTransactionResultCode::ItemBroken
            && !world.findPlayer(id<PlayerId>(1))
                    ->equipment[static_cast<std::size_t>(EquipmentSlot::CarriedRight)]);
        player.equipment[static_cast<std::size_t>(EquipmentSlot::CarriedRight)] = id<ItemStackId>(20);
        assert(!CanonicalInventoryWorld::create(manifest, catalog, std::array{ player }, {}));
    }

    void testWorldCreationRejectsBrokenCanonicalState()
    {
        const auto manifest = testContentManifest();
        const auto catalog = createTestCatalog(manifest);
        const auto cell = CellId::interior(id<CellSpaceId>(7));

        CanonicalPlayerInventoryState zeroCount{
            .player = id<PlayerId>(1),
            .stacks = { CanonicalItemStack{ id<ItemStackId>(1), id<ItemPrototypeId>(1), 0 } },
        };
        assert(!CanonicalInventoryWorld::create(manifest, catalog, std::vector{ zeroCount }, {}));

        CanonicalPlayerInventoryState player{
            .player = id<PlayerId>(1),
            .stacks = { CanonicalItemStack{ id<ItemStackId>(1), id<ItemPrototypeId>(1), 1 } },
        };
        CanonicalContainerInventoryState duplicate{
            .containerId = id<ContainerId>(1),
            .cell = cell,
            .position = Position3(0, 0, 0),
            .stacks = { CanonicalItemStack{ id<ItemStackId>(1), id<ItemPrototypeId>(1), 1 } },
        };
        assert(!CanonicalInventoryWorld::create(manifest, catalog, std::vector{ player }, std::vector{ duplicate }));

        player.equipment[static_cast<std::size_t>(EquipmentSlot::Helmet)] = id<ItemStackId>(99);
        assert(!CanonicalInventoryWorld::create(manifest, catalog, std::vector{ player }, {}));

        CanonicalPlayerInventoryState overEquipped{
            .player = id<PlayerId>(1),
            .stacks = { CanonicalItemStack{ id<ItemStackId>(2), id<ItemPrototypeId>(5), 1 } },
        };
        overEquipped.equipment[static_cast<std::size_t>(EquipmentSlot::LeftRing)] = id<ItemStackId>(2);
        overEquipped.equipment[static_cast<std::size_t>(EquipmentSlot::RightRing)] = id<ItemStackId>(2);
        assert(!CanonicalInventoryWorld::create(manifest, catalog, std::vector{ overEquipped }, {}));

        CanonicalPlayerInventoryState valid{
            .player = id<PlayerId>(1),
            .stacks = { CanonicalItemStack{ id<ItemStackId>(10), id<ItemPrototypeId>(1), 1 } },
        };
        assert(!CanonicalInventoryWorld::create(manifest, catalog, std::vector{ valid }, {}, {}, id<ItemStackId>(10)));
    }

    void testWorldCreationCanonicalizesStackOrder()
    {
        const auto manifest = testContentManifest();
        const auto catalog = createTestCatalog(manifest);
        CanonicalPlayerInventoryState player{
            .player = id<PlayerId>(1),
            .stacks = {
                CanonicalItemStack{ id<ItemStackId>(9), id<ItemPrototypeId>(2), 1 },
                CanonicalItemStack{ id<ItemStackId>(2), id<ItemPrototypeId>(3), 1 },
            },
        };

        const auto world = CanonicalInventoryWorld::create(manifest, catalog, std::vector{ player }, {});
        assert(world);
        assert(world->players()[0].stacks[0].stackId == id<ItemStackId>(2));
        assert(world->players()[0].stacks[1].stackId == id<ItemStackId>(9));
    }

    void testTransferOverflowRollsBackCompletely()
    {
        const auto manifest = testContentManifest();
        const auto catalog = createTestCatalog(manifest);
        const auto cell = CellId::interior(id<CellSpaceId>(7));
        const InventoryValidationContext context{};
        const auto serverPlayers = players();

        CanonicalPlayerInventoryState player{
            .player = id<PlayerId>(1),
            .stacks = { CanonicalItemStack{
                id<ItemStackId>(1), id<ItemPrototypeId>(1), std::numeric_limits<std::uint32_t>::max() } },
        };
        CanonicalContainerInventoryState container{
            .containerId = id<ContainerId>(1),
            .cell = cell,
            .position = Position3(0, 0, 0),
            .stacks = { CanonicalItemStack{ id<ItemStackId>(2), id<ItemPrototypeId>(1), 1 } },
        };
        auto takeWorld
            = CanonicalInventoryWorld::create(manifest, catalog, std::vector{ player }, std::vector{ container });
        assert(takeWorld);
        const auto beforeTake = *takeWorld;
        const InventoryTransactionCommand take{
            .player = id<PlayerId>(1),
            .kind = InventoryTransactionKind::TakeFromContainer,
            .containerId = id<ContainerId>(1),
            .prototypeId = id<ItemPrototypeId>(1),
            .stackId = id<ItemStackId>(2),
            .count = 1,
            .expectedInventoryRevision = InventoryRevision::initial(),
            .expectedContainerRevision = ContainerRevision::initial(),
            .interactionOrigin = Position3(0, 0, 0),
        };
        assert(applyInventoryTransaction(*takeWorld, serverPlayers, take, context, id<ServerTick>(1)).code
            == InventoryTransactionResultCode::ArithmeticOverflow);
        assert(*takeWorld == beforeTake);

        player.stacks[0].count = 1;
        container.stacks[0].count = std::numeric_limits<std::uint32_t>::max();
        auto putWorld
            = CanonicalInventoryWorld::create(manifest, catalog, std::vector{ player }, std::vector{ container });
        assert(putWorld);
        const auto beforePut = *putWorld;
        const InventoryTransactionCommand put{
            .player = id<PlayerId>(1),
            .kind = InventoryTransactionKind::PutIntoContainer,
            .containerId = id<ContainerId>(1),
            .prototypeId = id<ItemPrototypeId>(1),
            .stackId = id<ItemStackId>(1),
            .count = 1,
            .expectedInventoryRevision = InventoryRevision::initial(),
            .expectedContainerRevision = ContainerRevision::initial(),
            .interactionOrigin = Position3(0, 0, 0),
        };
        assert(applyInventoryTransaction(*putWorld, serverPlayers, put, context, id<ServerTick>(1)).code
            == InventoryTransactionResultCode::ArithmeticOverflow);
        assert(*putWorld == beforePut);
    }

    void testContainerRevisionIsMandatory()
    {
        const auto manifest = testContentManifest();
        const auto catalog = createTestCatalog(manifest);
        const auto cell = CellId::interior(id<CellSpaceId>(7));
        CanonicalPlayerInventoryState player{ .player = id<PlayerId>(1) };
        CanonicalContainerInventoryState container{
            .containerId = id<ContainerId>(1),
            .cell = cell,
            .position = Position3(0, 0, 0),
            .stacks = { CanonicalItemStack{ id<ItemStackId>(1), id<ItemPrototypeId>(1), 1 } },
        };
        auto world
            = CanonicalInventoryWorld::create(manifest, catalog, std::vector{ player }, std::vector{ container });
        assert(world);
        const auto before = *world;
        const InventoryTransactionCommand command{
            .player = id<PlayerId>(1),
            .kind = InventoryTransactionKind::TakeFromContainer,
            .containerId = id<ContainerId>(1),
            .prototypeId = id<ItemPrototypeId>(1),
            .stackId = id<ItemStackId>(1),
            .interactionOrigin = Position3(0, 0, 0),
        };
        const InventoryValidationContext context{};
        assert(applyInventoryTransaction(*world, players(), command, context, id<ServerTick>(1)).code
            == InventoryTransactionResultCode::MissingExpectedRevision);
        assert(*world == before);
    }

    void testDropAndPickupConserveCanonicalItems()
    {
        const auto manifest = testContentManifest();
        const auto catalog = createTestCatalog(manifest);
        const auto cell = CellId::interior(id<CellSpaceId>(7));
        CanonicalPlayerInventoryState player{
            .player = id<PlayerId>(1),
            .stacks = { CanonicalItemStack{ id<ItemStackId>(10), id<ItemPrototypeId>(1), 5 } },
        };
        auto world = CanonicalInventoryWorld::create(manifest, catalog, std::vector{ player }, {});
        assert(world);
        const InventoryValidationContext context{};
        const auto serverPlayers = players();

        const InventoryTransactionCommand drop{
            .player = id<PlayerId>(1),
            .kind = InventoryTransactionKind::DropItem,
            .prototypeId = id<ItemPrototypeId>(1),
            .stackId = id<ItemStackId>(10),
            .count = 2,
            .interactionOrigin = Position3(20, 0, 0),
        };
        const auto dropOutcome = applyInventoryTransaction(*world, serverPlayers, drop, context, id<ServerTick>(1));
        assert(dropOutcome.code == InventoryTransactionResultCode::Success);
        assert(dropOutcome.affectedStackId == id<ItemStackId>(11));
        assert(dropOutcome.newWorldItemRevision == WorldItemRevision::initial());
        assert(world->findPlayer(id<PlayerId>(1))->stacks[0].count == 3);
        assert(world->findWorldItem(id<ItemStackId>(11))->stack.count == 2);

        const InventoryTransactionCommand fabricatedPickup{
            .player = id<PlayerId>(1),
            .kind = InventoryTransactionKind::PickupItem,
            .prototypeId = id<ItemPrototypeId>(1),
            .stackId = id<ItemStackId>(999),
            .expectedInventoryRevision = id<InventoryRevision>(2),
            .expectedWorldItemRevision = WorldItemRevision::initial(),
            .interactionOrigin = Position3(20, 0, 0),
        };
        const auto beforeFabricated = *world;
        assert(applyInventoryTransaction(*world, serverPlayers, fabricatedPickup, context, id<ServerTick>(2)).code
            == InventoryTransactionResultCode::WorldItemNotFound);
        assert(*world == beforeFabricated);

        InventoryTransactionCommand pickup{
            .player = id<PlayerId>(1),
            .kind = InventoryTransactionKind::PickupItem,
            .prototypeId = id<ItemPrototypeId>(1),
            .stackId = id<ItemStackId>(11),
            .count = 1,
            .expectedInventoryRevision = id<InventoryRevision>(2),
            .expectedWorldItemRevision = WorldItemRevision::initial(),
            .interactionOrigin = Position3(20, 0, 0),
        };
        assert(applyInventoryTransaction(*world, serverPlayers, pickup, context, id<ServerTick>(2)).code
            == InventoryTransactionResultCode::Success);
        assert(world->findPlayer(id<PlayerId>(1))->stacks[0].count == 4);
        assert(world->findWorldItem(id<ItemStackId>(11))->stack.count == 1);
        assert(world->findWorldItem(id<ItemStackId>(11))->revision == id<WorldItemRevision>(2));

        pickup.expectedInventoryRevision = id<InventoryRevision>(3);
        const auto beforeStale = *world;
        assert(applyInventoryTransaction(*world, serverPlayers, pickup, context, id<ServerTick>(3)).code
            == InventoryTransactionResultCode::StaleWorldItemRevision);
        assert(*world == beforeStale);

        pickup.expectedWorldItemRevision = id<WorldItemRevision>(2);
        assert(applyInventoryTransaction(*world, serverPlayers, pickup, context, id<ServerTick>(3)).code
            == InventoryTransactionResultCode::Success);
        assert(world->findWorldItem(id<ItemStackId>(11)) == nullptr);
        assert(world->findPlayer(id<PlayerId>(1))->stacks[0].count == 5);
    }

    void testEquipmentQuantityAndEquippedTransferRules()
    {
        const auto manifest = testContentManifest();
        const auto catalog = createTestCatalog(manifest);
        CanonicalPlayerInventoryState player{
            .player = id<PlayerId>(1),
            .stacks = { CanonicalItemStack{ id<ItemStackId>(1), id<ItemPrototypeId>(5), 1 } },
        };
        auto world = CanonicalInventoryWorld::create(manifest, catalog, std::vector{ player }, {});
        assert(world);
        const auto cell = CellId::interior(id<CellSpaceId>(7));
        const InventoryValidationContext context{};
        const auto serverPlayers = players();
        InventoryTransactionCommand equip{
            .player = id<PlayerId>(1),
            .kind = InventoryTransactionKind::EquipItem,
            .prototypeId = id<ItemPrototypeId>(5),
            .stackId = id<ItemStackId>(1),
            .slot = EquipmentSlot::LeftRing,
            .interactionOrigin = Position3(0, 0, 0),
        };
        assert(applyInventoryTransaction(*world, serverPlayers, equip, context, id<ServerTick>(1)).code
            == InventoryTransactionResultCode::Success);
        equip.slot = EquipmentSlot::RightRing;
        equip.expectedInventoryRevision = id<InventoryRevision>(2);
        assert(applyInventoryTransaction(*world, serverPlayers, equip, context, id<ServerTick>(2)).code
            == InventoryTransactionResultCode::EquipmentQuantityExceeded);

        const InventoryTransactionCommand drop{
            .player = id<PlayerId>(1),
            .kind = InventoryTransactionKind::DropItem,
            .prototypeId = id<ItemPrototypeId>(5),
            .stackId = id<ItemStackId>(1),
            .expectedInventoryRevision = id<InventoryRevision>(2),
            .interactionOrigin = Position3(0, 0, 0),
        };
        assert(applyInventoryTransaction(*world, serverPlayers, drop, context, id<ServerTick>(2)).code
            == InventoryTransactionResultCode::ItemEquipped);
    }

    void testWholeStackGroundRoundTripPreservesIdentity()
    {
        const auto manifest = testContentManifest();
        const auto catalog = createTestCatalog(manifest);
        const auto stackId = id<ItemStackId>(7);
        CanonicalPlayerInventoryState player{
            .player = id<PlayerId>(1),
            .stacks = { CanonicalItemStack{ stackId, id<ItemPrototypeId>(2), 1 } },
        };
        auto world = CanonicalInventoryWorld::create(manifest, catalog, std::vector{ player }, {});
        assert(world);
        const InventoryValidationContext context{};
        const auto serverPlayers = players();
        const InventoryTransactionCommand drop{
            .player = id<PlayerId>(1),
            .kind = InventoryTransactionKind::DropItem,
            .prototypeId = id<ItemPrototypeId>(2),
            .stackId = stackId,
            .interactionOrigin = Position3(0, 0, 0),
        };
        assert(applyInventoryTransaction(*world, serverPlayers, drop, context, id<ServerTick>(1)).code
            == InventoryTransactionResultCode::Success);
        assert(world->findWorldItem(stackId));
        assert(world->findPlayer(id<PlayerId>(1))->stacks.empty());

        const InventoryTransactionCommand pickup{
            .player = id<PlayerId>(1),
            .kind = InventoryTransactionKind::PickupItem,
            .prototypeId = id<ItemPrototypeId>(2),
            .stackId = stackId,
            .expectedInventoryRevision = id<InventoryRevision>(2),
            .expectedWorldItemRevision = WorldItemRevision::initial(),
            .interactionOrigin = Position3(0, 0, 0),
        };
        assert(applyInventoryTransaction(*world, serverPlayers, pickup, context, id<ServerTick>(2)).code
            == InventoryTransactionResultCode::Success);
        assert(world->findWorldItem(stackId) == nullptr);
        assert(world->findPlayer(id<PlayerId>(1))->findStack(stackId));
    }

    void testExtremeReachAndStackIdExhaustionFailClosed()
    {
        assert(!positionsWithinReach(Position3(std::numeric_limits<std::int64_t>::min(), 0, 0),
            Position3(std::numeric_limits<std::int64_t>::max(), 0, 0), 384));
        assert(positionsWithinReach(Position3(0, 0, 0), Position3(384, 0, 0), 384));

        const auto manifest = testContentManifest();
        const auto catalog = createTestCatalog(manifest);
        CanonicalPlayerInventoryState player{
            .player = id<PlayerId>(1),
            .stacks = { CanonicalItemStack{
                id<ItemStackId>(std::numeric_limits<std::uint64_t>::max()), id<ItemPrototypeId>(1), 2 } },
        };
        auto world = CanonicalInventoryWorld::create(manifest, catalog, std::vector{ player }, {});
        assert(world);
        const auto before = *world;
        const InventoryTransactionCommand drop{
            .player = id<PlayerId>(1),
            .kind = InventoryTransactionKind::DropItem,
            .prototypeId = id<ItemPrototypeId>(1),
            .stackId = id<ItemStackId>(std::numeric_limits<std::uint64_t>::max()),
            .count = 1,
            .interactionOrigin = Position3(0, 0, 0),
        };
        const InventoryValidationContext context{};
        assert(applyInventoryTransaction(*world, players(), drop, context, id<ServerTick>(1)).code
            == InventoryTransactionResultCode::StackIdExhausted);
        assert(*world == before);
    }

    void testWorldOwnsItsValidatedCatalogSnapshot()
    {
        const auto playerId = id<PlayerId>(1);
        auto world = [playerId]() {
            const auto manifest = testContentManifest();
            const auto catalog = createTestCatalog(manifest);
            CanonicalPlayerInventoryState player{
                .player = playerId,
                .stacks = { CanonicalItemStack{ id<ItemStackId>(1), id<ItemPrototypeId>(4), 1 } },
            };
            return CanonicalInventoryWorld::create(manifest, catalog, std::vector{ player }, {});
        }();
        assert(world);

        const auto keys = world->collectVerifiedKeys(playerId);
        assert(keys.size() == 1);
        assert(keys[0] == id<KeyPrototypeId>(42));
    }

}

int main()
{
    testKeyCollectionAndDoorUnlockIntegration();
    testContainerTransferTakeAndStackMerging();
    testConcurrentAccessPreventsDuplication();
    testReachAndCellValidation();
    testEquipmentSlotAssignment();
    testBrokenWeaponCannotBeEquipped();
    testWorldCreationRejectsBrokenCanonicalState();
    testWorldCreationCanonicalizesStackOrder();
    testTransferOverflowRollsBackCompletely();
    testContainerRevisionIsMandatory();
    testDropAndPickupConserveCanonicalItems();
    testEquipmentQuantityAndEquippedTransferRules();
    testWholeStackGroundRoundTripPreservesIdentity();
    testExtremeReachAndStackIdExhaustionFailClosed();
    testWorldOwnsItsValidatedCatalogSnapshot();

    std::cout << "All inventory world tests passed." << std::endl;
    return 0;
}
