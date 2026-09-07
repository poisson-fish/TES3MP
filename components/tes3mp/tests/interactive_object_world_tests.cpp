#include <tes3mp/interactive_object_world.hpp>

#include <array>
#include <limits>
#include <vector>

namespace
{
    using namespace TES3MP;

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

    CanonicalServerState players(std::uint64_t cell = 7, std::int64_t x = 0)
    {
        const std::array values{ CanonicalPlayerEntityState(id<PlayerId>(1), id<EntityId>(100), id<AppearanceId>(1),
            tr(cell, x), LinearVelocity3(0, 0, 0), EntityRevision::initial(), AuthorityEpoch::initial(),
            ServerTick::initial()) };
        const std::array sessions{ CanonicalSessionProgress(
            id<SessionId>(1), SessionGeneration::initial(), id<PlayerId>(1), id<EntityId>(100), std::nullopt) };
        return std::get<CanonicalServerState>(createCanonicalServerState(values, sessions));
    }

    InteractiveObjectCatalog sampleCatalog()
    {
        const auto key = id<KeyPrototypeId>(500);
        const auto trap = id<TrapPrototypeId>(600);
        const std::array entries{ // Door 1: simple standard door in cell 7 at x=100
            InteractiveObjectCatalogEntry{ id<InteractiveObjectId>(1), InteractiveObjectKind::StandardDoor,
                CellId::interior(id<CellSpaceId>(7)), tr(7, 100), std::nullopt,
                ObjectLockDeclaration{ false, 0, std::nullopt }, ObjectTrapDeclaration{ false, std::nullopt } },
            // Door 2: locked door in cell 7 at x=200 requiring key 500
            InteractiveObjectCatalogEntry{ id<InteractiveObjectId>(2), InteractiveObjectKind::StandardDoor,
                CellId::interior(id<CellSpaceId>(7)), tr(7, 200), std::nullopt, ObjectLockDeclaration{ true, 50, key },
                ObjectTrapDeclaration{ true, trap } },
            // Door 3: teleport door in cell 7 at x=50 leading to cell 7 at x=1000
            InteractiveObjectCatalogEntry{ id<InteractiveObjectId>(3), InteractiveObjectKind::TeleportDoor,
                CellId::interior(id<CellSpaceId>(7)), tr(7, 50),
                TeleportDestination{ CellId::interior(id<CellSpaceId>(7)), tr(7, 1000) }, ObjectLockDeclaration{},
                ObjectTrapDeclaration{} },
            // Door 4: trapped-only door in cell 7 at x=150
            InteractiveObjectCatalogEntry{ id<InteractiveObjectId>(4), InteractiveObjectKind::StandardDoor,
                CellId::interior(id<CellSpaceId>(7)), tr(7, 150), std::nullopt,
                ObjectLockDeclaration{ false, 0, std::nullopt }, ObjectTrapDeclaration{ true, trap } }
        };
        return *InteractiveObjectCatalog::create(testContentManifest(), entries);
    }

    bool initial_world_matches_catalog_defaults()
    {
        const auto catalog = sampleCatalog();
        const auto created = createInitialCanonicalInteractiveObjectWorld(catalog);
        const auto* world = std::get_if<CanonicalInteractiveObjectWorld>(&created);
        if (!world || world->objects().size() != 4)
            return false;

        const auto* d1 = world->find(id<InteractiveObjectId>(1));
        const auto* d2 = world->find(id<InteractiveObjectId>(2));
        const auto* d4 = world->find(id<InteractiveObjectId>(4));
        return d1 && d1->doorState() == DoorState::Closed && d1->lockState() == LockState::Unlocked
            && d1->trapState() == TrapState::Disarmed && d2 && d2->doorState() == DoorState::Closed
            && d2->lockState() == LockState::Locked && d2->trapState() == TrapState::Armed
            && d2->keyId() == id<KeyPrototypeId>(500) && d4 && d4->doorState() == DoorState::Closed
            && d4->lockState() == LockState::Unlocked && d4->trapState() == TrapState::Armed;
    }

    bool standard_door_toggles_and_advances_revision()
    {
        const auto catalog = sampleCatalog();
        auto world = std::get<CanonicalInteractiveObjectWorld>(createInitialCanonicalInteractiveObjectWorld(catalog));
        const auto p = players(7, 100);

        // First activate: toggles Closed -> Open
        InteractObjectCommand cmd{ id<PlayerId>(1), id<InteractiveObjectId>(1), CellId::interior(id<CellSpaceId>(7)),
            Position3(100, 0, 0), ObjectRevision::initial(), ObjectInteractionKind::Activate };

        const auto res1 = applyObjectInteraction(world, catalog, p, cmd, id<ServerTick>(10));
        if (res1.outcome.code != ObjectInteractionResultCode::Success || res1.outcome.newDoorState != DoorState::Open
            || res1.outcome.newRevision != *ObjectRevision::initial().next() || !res1.updatedWorld.has_value())
            return false;

        world = *res1.updatedWorld;

        // Second activate: toggles Open -> Closed with next revision
        cmd.expectedRevision = res1.outcome.newRevision;
        const auto res2 = applyObjectInteraction(world, catalog, p, cmd, id<ServerTick>(11));
        return res2.outcome.code != ObjectInteractionResultCode::ObjectNotFound
            && res2.outcome.code == ObjectInteractionResultCode::Success
            && res2.outcome.newDoorState == DoorState::Closed
            && res2.outcome.newRevision == *cmd.expectedRevision.next() && res2.updatedWorld.has_value();
    }

    bool reach_validation_rejects_out_of_reach_activations()
    {
        const auto catalog = sampleCatalog();
        const auto world
            = std::get<CanonicalInteractiveObjectWorld>(createInitialCanonicalInteractiveObjectWorld(catalog));

        // Player is at x=0, door 1 is at x=100. Max reach = 50 -> out of reach!
        const auto p = players(7, 0);
        InteractObjectCommand cmd{ id<PlayerId>(1), id<InteractiveObjectId>(1), CellId::interior(id<CellSpaceId>(7)),
            Position3(0, 0, 0), ObjectRevision::initial(), ObjectInteractionKind::Activate };

        const auto res = applyObjectInteraction(
            world, catalog, p, cmd, id<ServerTick>(1), ObjectInteractionValidationContext{ 50 });
        return res.outcome.code == ObjectInteractionResultCode::PlayerOutOfReach && !res.updatedWorld.has_value();
    }

    bool cell_mismatch_is_rejected()
    {
        const auto catalog = sampleCatalog();
        const auto world
            = std::get<CanonicalInteractiveObjectWorld>(createInitialCanonicalInteractiveObjectWorld(catalog));

        // Player is in cell 7, command claims cell 8
        const auto p = players(7, 100);
        InteractObjectCommand cmd{ id<PlayerId>(1), id<InteractiveObjectId>(1), CellId::interior(id<CellSpaceId>(8)),
            Position3(100, 0, 0), ObjectRevision::initial(), ObjectInteractionKind::Activate };

        const auto res = applyObjectInteraction(world, catalog, p, cmd, id<ServerTick>(1));
        return res.outcome.code == ObjectInteractionResultCode::CellMismatch && !res.updatedWorld.has_value();
    }

    bool locked_door_requires_key_and_unlocks()
    {
        const auto catalog = sampleCatalog();
        auto world = std::get<CanonicalInteractiveObjectWorld>(createInitialCanonicalInteractiveObjectWorld(catalog));
        const auto p = players(7, 200);

        // Direct activate on locked door fails with Locked
        InteractObjectCommand cmd{ id<PlayerId>(1), id<InteractiveObjectId>(2), CellId::interior(id<CellSpaceId>(7)),
            Position3(200, 0, 0), ObjectRevision::initial(), ObjectInteractionKind::Activate };

        const auto resLocked = applyObjectInteraction(world, catalog, p, cmd, id<ServerTick>(1));
        if (resLocked.outcome.code != ObjectInteractionResultCode::Locked || resLocked.updatedWorld.has_value())
            return false;

        // Unlock with wrong key fails
        cmd.kind = ObjectInteractionKind::UnlockWithKey;
        cmd.requestedKey = id<KeyPrototypeId>(999);
        const std::array wrongOwnedKey{ id<KeyPrototypeId>(999) };
        const auto resWrongKey = applyObjectInteraction(
            world, catalog, p, cmd, id<ServerTick>(2), ObjectInteractionValidationContext{ 384, wrongOwnedKey });
        if (resWrongKey.outcome.code != ObjectInteractionResultCode::Locked || resWrongKey.updatedWorld.has_value())
            return false;

        // Claiming the correct key without server-verified possession fails
        cmd.requestedKey = id<KeyPrototypeId>(500);
        const auto resForgedKey = applyObjectInteraction(world, catalog, p, cmd, id<ServerTick>(3));
        if (resForgedKey.outcome.code != ObjectInteractionResultCode::Locked || resForgedKey.updatedWorld.has_value())
            return false;

        // A correct key verified from server-owned state unlocks the door and disarms its trap
        const std::array verifiedKeys{ id<KeyPrototypeId>(500) };
        const auto resUnlocked = applyObjectInteraction(
            world, catalog, p, cmd, id<ServerTick>(3), ObjectInteractionValidationContext{ 384, verifiedKeys });
        if (resUnlocked.outcome.code != ObjectInteractionResultCode::Success
            || resUnlocked.outcome.newLockState != LockState::Unlocked
            || resUnlocked.outcome.newTrapState != TrapState::Disarmed || !resUnlocked.updatedWorld.has_value())
            return false;

        // Once unlocked, activating opens door
        world = *resUnlocked.updatedWorld;
        cmd.kind = ObjectInteractionKind::Activate;
        cmd.expectedRevision = resUnlocked.outcome.newRevision;
        cmd.requestedKey = std::nullopt;

        const auto resOpen = applyObjectInteraction(world, catalog, p, cmd, id<ServerTick>(4));
        return resOpen.outcome.code == ObjectInteractionResultCode::Success
            && resOpen.outcome.newDoorState == DoorState::Open && resOpen.updatedWorld.has_value();
    }

    bool armed_trap_springs_on_activation()
    {
        const auto catalog = sampleCatalog();
        const auto world
            = std::get<CanonicalInteractiveObjectWorld>(createInitialCanonicalInteractiveObjectWorld(catalog));
        const auto p = players(7, 150);

        // Door 4 is unlocked but trapped with trap 600
        InteractObjectCommand cmd{ id<PlayerId>(1), id<InteractiveObjectId>(4), CellId::interior(id<CellSpaceId>(7)),
            Position3(150, 0, 0), ObjectRevision::initial(), ObjectInteractionKind::Activate };

        const auto res = applyObjectInteraction(world, catalog, p, cmd, id<ServerTick>(1));
        return res.outcome.code == ObjectInteractionResultCode::TrapSprung
            && res.outcome.sprungTrap == id<TrapPrototypeId>(600) && res.outcome.newTrapState == TrapState::Disarmed
            && res.outcome.newDoorState == DoorState::Closed && res.updatedWorld.has_value();
    }

    bool teleport_door_initiates_player_teleport()
    {
        const auto catalog = sampleCatalog();
        const auto world
            = std::get<CanonicalInteractiveObjectWorld>(createInitialCanonicalInteractiveObjectWorld(catalog));
        const auto p = players(7, 50);

        InteractObjectCommand cmd{ id<PlayerId>(1), id<InteractiveObjectId>(3), CellId::interior(id<CellSpaceId>(7)),
            Position3(50, 0, 0), ObjectRevision::initial(), ObjectInteractionKind::Activate };

        const auto res = applyObjectInteraction(world, catalog, p, cmd, id<ServerTick>(1));
        return res.outcome.code == ObjectInteractionResultCode::Success && res.outcome.playerTeleport.has_value()
            && res.outcome.playerTeleport->cell == CellId::interior(id<CellSpaceId>(7))
            && res.outcome.playerTeleport->transform.position().x() == 1000 && res.updatedWorld.has_value();
    }

    bool stale_revision_is_rejected()
    {
        const auto catalog = sampleCatalog();
        const auto world
            = std::get<CanonicalInteractiveObjectWorld>(createInitialCanonicalInteractiveObjectWorld(catalog));
        const auto p = players(7, 100);

        // Command with non-matching revision
        InteractObjectCommand cmd{ id<PlayerId>(1), id<InteractiveObjectId>(1), CellId::interior(id<CellSpaceId>(7)),
            Position3(100, 0, 0),
            *ObjectRevision::initial().next(), // Expected is 2, current is 1
            ObjectInteractionKind::Activate };

        const auto res = applyObjectInteraction(world, catalog, p, cmd, id<ServerTick>(1));
        return res.outcome.code == ObjectInteractionResultCode::StaleRevision && !res.updatedWorld.has_value();
    }

    bool reported_origin_must_reach_object_and_remain_in_root_envelope()
    {
        const auto catalog = sampleCatalog();
        const auto world
            = std::get<CanonicalInteractiveObjectWorld>(createInitialCanonicalInteractiveObjectWorld(catalog));
        const auto p = players(7, 0);
        InteractObjectCommand cmd{ id<PlayerId>(1), id<InteractiveObjectId>(1), CellId::interior(id<CellSpaceId>(7)),
            Position3(-300, 0, 0), ObjectRevision::initial(), ObjectInteractionKind::Activate };

        const auto originCannotReach = applyObjectInteraction(world, catalog, p, cmd, id<ServerTick>(1));
        cmd.interactionOrigin = Position3(400, 0, 0);
        const auto originOutsideRootEnvelope = applyObjectInteraction(world, catalog, p, cmd, id<ServerTick>(1));
        return originCannotReach.outcome.code == ObjectInteractionResultCode::PlayerOutOfReach
            && !originCannotReach.updatedWorld.has_value()
            && originOutsideRootEnvelope.outcome.code == ObjectInteractionResultCode::PlayerOutOfReach
            && !originOutsideRootEnvelope.updatedWorld.has_value();
    }

    bool extreme_positions_and_maximum_reach_are_checked_without_overflow()
    {
        const std::array farEntries{ InteractiveObjectCatalogEntry{ id<InteractiveObjectId>(1),
            InteractiveObjectKind::StandardDoor, CellId::interior(id<CellSpaceId>(7)),
            tr(7, std::numeric_limits<std::int64_t>::min()), std::nullopt, ObjectLockDeclaration{},
            ObjectTrapDeclaration{} } };
        const auto farCatalog = *InteractiveObjectCatalog::create(testContentManifest(), farEntries);
        const auto farWorld
            = std::get<CanonicalInteractiveObjectWorld>(createInitialCanonicalInteractiveObjectWorld(farCatalog));
        const auto farPlayer = players(7, std::numeric_limits<std::int64_t>::max());
        const InteractObjectCommand farCommand{ id<PlayerId>(1), id<InteractiveObjectId>(1),
            CellId::interior(id<CellSpaceId>(7)), Position3(std::numeric_limits<std::int64_t>::max(), 0, 0),
            ObjectRevision::initial(), ObjectInteractionKind::Activate };
        const auto farResult = applyObjectInteraction(farWorld, farCatalog, farPlayer, farCommand, id<ServerTick>(1));
        if (farResult.outcome.code != ObjectInteractionResultCode::PlayerOutOfReach
            || farResult.updatedWorld.has_value())
            return false;

        const std::array boundaryEntries{ InteractiveObjectCatalogEntry{ id<InteractiveObjectId>(1),
            InteractiveObjectKind::StandardDoor, CellId::interior(id<CellSpaceId>(7)), tr(7), std::nullopt,
            ObjectLockDeclaration{}, ObjectTrapDeclaration{} } };
        const auto boundaryCatalog = *InteractiveObjectCatalog::create(testContentManifest(), boundaryEntries);
        const auto boundaryWorld
            = std::get<CanonicalInteractiveObjectWorld>(createInitialCanonicalInteractiveObjectWorld(boundaryCatalog));
        constexpr auto MaximumReach = std::numeric_limits<std::uint32_t>::max();
        const auto boundaryPlayer = players(7, static_cast<std::int64_t>(MaximumReach));
        const InteractObjectCommand boundaryCommand{ id<PlayerId>(1), id<InteractiveObjectId>(1),
            CellId::interior(id<CellSpaceId>(7)), Position3(MaximumReach, 0, 0), ObjectRevision::initial(),
            ObjectInteractionKind::Activate };
        const auto boundaryResult = applyObjectInteraction(boundaryWorld, boundaryCatalog, boundaryPlayer,
            boundaryCommand, id<ServerTick>(1), ObjectInteractionValidationContext{ MaximumReach });
        return boundaryResult.outcome.code == ObjectInteractionResultCode::Success
            && boundaryResult.updatedWorld.has_value();
    }

    bool catalog_mismatch_is_rejected()
    {
        const auto catalog = sampleCatalog();
        const auto initial
            = std::get<CanonicalInteractiveObjectWorld>(createInitialCanonicalInteractiveObjectWorld(catalog));
        std::vector<CanonicalInteractiveObjectState> states(initial.objects().begin(), initial.objects().end());
        const auto& object = states.front();
        states.front()
            = CanonicalInteractiveObjectState(object.objectId(), object.cell(), object.doorState(), object.lockState(),
                1, object.keyId(), object.trapState(), object.trapId(), object.revision(), object.lastChangeTick());
        const auto mismatched
            = std::get<CanonicalInteractiveObjectWorld>(createCanonicalInteractiveObjectWorld(states));
        const auto p = players(7, 100);
        const InteractObjectCommand cmd{ id<PlayerId>(1), id<InteractiveObjectId>(1),
            CellId::interior(id<CellSpaceId>(7)), Position3(100, 0, 0), ObjectRevision::initial(),
            ObjectInteractionKind::Activate };

        const auto result = applyObjectInteraction(mismatched, catalog, p, cmd, id<ServerTick>(1));
        return result.outcome.code == ObjectInteractionResultCode::CatalogMismatch && !result.updatedWorld.has_value();
    }

    bool tick_regression_and_revision_exhaustion_are_explicit()
    {
        const auto catalog = sampleCatalog();
        const auto initial
            = std::get<CanonicalInteractiveObjectWorld>(createInitialCanonicalInteractiveObjectWorld(catalog));
        const auto p = players(7, 100);
        InteractObjectCommand cmd{ id<PlayerId>(1), id<InteractiveObjectId>(1), CellId::interior(id<CellSpaceId>(7)),
            Position3(100, 0, 0), ObjectRevision::initial(), ObjectInteractionKind::Activate };

        const auto advanced = applyObjectInteraction(initial, catalog, p, cmd, id<ServerTick>(10));
        if (!advanced.updatedWorld)
            return false;
        cmd.expectedRevision = advanced.outcome.newRevision;
        const auto regressed = applyObjectInteraction(*advanced.updatedWorld, catalog, p, cmd, id<ServerTick>(9));
        if (regressed.outcome.code != ObjectInteractionResultCode::TickRegression || regressed.updatedWorld.has_value())
            return false;

        std::vector<CanonicalInteractiveObjectState> states(initial.objects().begin(), initial.objects().end());
        const auto& object = states.front();
        const auto maximumRevision = *ObjectRevision::fromValue(std::numeric_limits<std::uint64_t>::max());
        states.front() = CanonicalInteractiveObjectState(object.objectId(), object.cell(), object.doorState(),
            object.lockState(), object.lockLevel(), object.keyId(), object.trapState(), object.trapId(),
            maximumRevision, object.lastChangeTick());
        const auto exhaustedWorld
            = std::get<CanonicalInteractiveObjectWorld>(createCanonicalInteractiveObjectWorld(states));
        cmd.expectedRevision = maximumRevision;
        const auto exhausted = applyObjectInteraction(exhaustedWorld, catalog, p, cmd, id<ServerTick>(1));
        return exhausted.outcome.code == ObjectInteractionResultCode::RevisionExhausted
            && !exhausted.updatedWorld.has_value();
    }
}

int main()
{
    return initial_world_matches_catalog_defaults() && standard_door_toggles_and_advances_revision()
            && reach_validation_rejects_out_of_reach_activations() && cell_mismatch_is_rejected()
            && locked_door_requires_key_and_unlocks() && armed_trap_springs_on_activation()
            && teleport_door_initiates_player_teleport() && stale_revision_is_rejected()
            && reported_origin_must_reach_object_and_remain_in_root_envelope()
            && extreme_positions_and_maximum_reach_are_checked_without_overflow() && catalog_mismatch_is_rejected()
            && tick_regression_and_revision_exhaustion_are_explicit()
        ? 0
        : 1;
}
