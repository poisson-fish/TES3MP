#include <tes3mp/interactive_object_catalog.hpp>

#include <array>
#include <span>
#include <vector>

namespace
{
    using namespace TES3MP;

    template <class T>
    T id(std::uint64_t value)
    {
        return *T::fromValue(value);
    }

    Transform tr(std::uint64_t cell, std::int64_t x = 0)
    {
        const auto zero = Turn32::fromValue(0);
        return Transform(CellId::interior(id<CellSpaceId>(cell)), Position3(x, 0, 0), Orientation3(zero, zero, zero));
    }

    Transform tr(CellId cell, std::int64_t x = 0)
    {
        const auto zero = Turn32::fromValue(0);
        return Transform(cell, Position3(x, 0, 0), Orientation3(zero, zero, zero));
    }

    InteractiveObjectCatalogEntry standardDoor(std::uint64_t objId, std::uint64_t cell, std::int64_t x = 0,
        bool locked = false, std::uint32_t lockLevel = 0, std::optional<KeyPrototypeId> key = std::nullopt,
        bool trapped = false, std::optional<TrapPrototypeId> trap = std::nullopt)
    {
        return { id<InteractiveObjectId>(objId), InteractiveObjectKind::StandardDoor,
            CellId::interior(id<CellSpaceId>(cell)), tr(cell, x), std::nullopt,
            ObjectLockDeclaration{ locked, lockLevel, key }, ObjectTrapDeclaration{ trapped, trap } };
    }

    InteractiveObjectCatalogEntry teleportDoor(std::uint64_t objId, std::uint64_t srcCell, std::uint64_t dstCell)
    {
        return { id<InteractiveObjectId>(objId), InteractiveObjectKind::TeleportDoor,
            CellId::interior(id<CellSpaceId>(srcCell)), tr(srcCell),
            TeleportDestination{ CellId::interior(id<CellSpaceId>(dstCell)), tr(dstCell, 100) },
            ObjectLockDeclaration{}, ObjectTrapDeclaration{} };
    }

    bool catalog_is_manifest_bound_sorted_and_findable()
    {
        const std::array unsorted{ standardDoor(2, 7, 50), standardDoor(1, 7, 10) };
        const auto catalog = InteractiveObjectCatalog::create(testContentManifest(), unsorted);
        return catalog && catalog->contentManifestId() == testContentManifestId()
            && catalog->entries()[0].objectId == id<InteractiveObjectId>(1)
            && catalog->find(id<InteractiveObjectId>(2))->transform.position().x() == 50
            && catalog->find(id<InteractiveObjectId>(3)) == nullptr;
    }

    bool duplicate_identity_unknown_cell_and_invalid_declarations_fail_closed()
    {
        const std::array duplicateId{ standardDoor(1, 7), standardDoor(1, 7) };
        const std::array unknownCell{ standardDoor(1, 9999) }; // not in testContentManifest
        const std::array unknownDestCell{ InteractiveObjectCatalogEntry{ id<InteractiveObjectId>(1),
            InteractiveObjectKind::TeleportDoor, CellId::interior(id<CellSpaceId>(7)), tr(7),
            TeleportDestination{ CellId::interior(id<CellSpaceId>(9999)), tr(9999) }, ObjectLockDeclaration{},
            ObjectTrapDeclaration{} } };
        const std::array invalidDoorDest{ InteractiveObjectCatalogEntry{ id<InteractiveObjectId>(1),
            InteractiveObjectKind::StandardDoor, CellId::interior(id<CellSpaceId>(7)), tr(7),
            TeleportDestination{
                CellId::interior(id<CellSpaceId>(7)), tr(7) }, // StandardDoor must not have destination
            ObjectLockDeclaration{}, ObjectTrapDeclaration{} } };
        const std::array invalidLock{ standardDoor(1, 7, 0, true, 0) }; // locked by default but lockLevel is 0
        const std::array invalidTrap{ standardDoor(
            1, 7, 0, false, 0, std::nullopt, true, std::nullopt) }; // trapped by default but no trapId
        const auto exteriorCell = CellId::exterior(id<CellSpaceId>(8), 0, 0);
        const std::array mismatchedObjectTransform{ InteractiveObjectCatalogEntry{ id<InteractiveObjectId>(1),
            InteractiveObjectKind::StandardDoor, CellId::interior(id<CellSpaceId>(7)), tr(exteriorCell), std::nullopt,
            ObjectLockDeclaration{}, ObjectTrapDeclaration{} } };
        const std::array mismatchedDestinationTransform{ InteractiveObjectCatalogEntry{ id<InteractiveObjectId>(1),
            InteractiveObjectKind::TeleportDoor, CellId::interior(id<CellSpaceId>(7)), tr(7),
            TeleportDestination{ CellId::interior(id<CellSpaceId>(7)), tr(exteriorCell) }, ObjectLockDeclaration{},
            ObjectTrapDeclaration{} } };

        const auto manifest = testContentManifest();
        return !InteractiveObjectCatalog::create(manifest, duplicateId)
            && !InteractiveObjectCatalog::create(manifest, unknownCell)
            && !InteractiveObjectCatalog::create(manifest, unknownDestCell)
            && !InteractiveObjectCatalog::create(manifest, invalidDoorDest)
            && !InteractiveObjectCatalog::create(manifest, invalidLock)
            && !InteractiveObjectCatalog::create(manifest, invalidTrap)
            && !InteractiveObjectCatalog::create(manifest, mismatchedObjectTransform)
            && !InteractiveObjectCatalog::create(manifest, mismatchedDestinationTransform);
    }

    bool per_cell_and_global_capacity_limits_are_enforced()
    {
        std::vector<InteractiveObjectCatalogEntry> perCellEntries;
        perCellEntries.reserve(MaximumInteractiveObjectsPerCell + 1);
        for (std::size_t index = 0; index <= MaximumInteractiveObjectsPerCell; ++index)
            perCellEntries.push_back(standardDoor(index + 1, 7));

        const auto manifest = testContentManifest();
        if (!InteractiveObjectCatalog::create(
                manifest, std::span(perCellEntries).first(MaximumInteractiveObjectsPerCell))
            || InteractiveObjectCatalog::create(manifest, perCellEntries))
            return false;

        const auto worldspace = id<CellSpaceId>(8);
        const std::array spaces{ CellSpaceDeclaration{ worldspace, CellSpaceKind::Exterior } };
        std::vector<CellId> cells;
        constexpr std::size_t RequiredCells
            = (MaximumInteractiveObjectCatalogEntries + 1 + MaximumInteractiveObjectsPerCell - 1)
            / MaximumInteractiveObjectsPerCell;
        cells.reserve(RequiredCells);
        for (std::size_t index = 0; index < RequiredCells; ++index)
            cells.push_back(CellId::exterior(worldspace, static_cast<std::int32_t>(index), 0));
        const auto largeManifest = ContentManifest::create(
            testContentManifestId(), spaces, cells, id<AppearanceId>(1), testMovementProfile());
        if (!largeManifest)
            return false;

        std::vector<InteractiveObjectCatalogEntry> globalEntries;
        globalEntries.reserve(MaximumInteractiveObjectCatalogEntries + 1);
        for (std::size_t index = 0; index <= MaximumInteractiveObjectCatalogEntries; ++index)
        {
            const auto cell = cells[index / MaximumInteractiveObjectsPerCell];
            globalEntries.push_back({ id<InteractiveObjectId>(index + 1), InteractiveObjectKind::StandardDoor, cell,
                tr(cell), std::nullopt, ObjectLockDeclaration{}, ObjectTrapDeclaration{} });
        }

        return InteractiveObjectCatalog::create(
                   *largeManifest, std::span(globalEntries).first(MaximumInteractiveObjectCatalogEntries))
            && !InteractiveObjectCatalog::create(*largeManifest, globalEntries);
    }

    bool teleport_door_and_locking_declarations_work()
    {
        const auto key = id<KeyPrototypeId>(50);
        const auto trap = id<TrapPrototypeId>(60);
        const std::array entries{ standardDoor(1, 7, 0, true, 50, key, true, trap), teleportDoor(2, 7, 7) };
        const auto catalog = InteractiveObjectCatalog::create(testContentManifest(), entries);
        if (!catalog)
            return false;

        const auto* door1 = catalog->find(id<InteractiveObjectId>(1));
        const auto* door2 = catalog->find(id<InteractiveObjectId>(2));
        return door1 && door1->lock.lockedByDefault && door1->lock.lockLevel == 50 && door1->lock.keyId == key
            && door1->trap.trappedByDefault && door1->trap.trapId == trap && door2
            && door2->kind == InteractiveObjectKind::TeleportDoor && door2->destination.has_value();
    }
}

int main()
{
    return catalog_is_manifest_bound_sorted_and_findable()
            && duplicate_identity_unknown_cell_and_invalid_declarations_fail_closed()
            && per_cell_and_global_capacity_limits_are_enforced() && teleport_door_and_locking_declarations_work()
        ? 0
        : 1;
}
