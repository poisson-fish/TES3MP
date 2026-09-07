#include <tes3mp/actor_catalog.hpp>

#include <array>
#include <vector>

namespace
{
    using namespace TES3MP;

    template <class T>
    T id(std::uint64_t value)
    {
        return *T::fromValue(value);
    }

    Transform root(std::uint64_t cell, std::int64_t x = 0)
    {
        const auto zero = Turn32::fromValue(0);
        return Transform(CellId::interior(id<CellSpaceId>(cell)), Position3(x, 0, 0), Orientation3(zero, zero, zero));
    }

    ActorCatalogEntry entry(std::uint64_t actor, std::uint64_t entity, std::uint64_t cell,
        ActorAiPackageKind kind = ActorAiPackageKind::Idle, std::span<const Position3> waypoints = {})
    {
        return { id<ActorId>(actor), id<EntityId>(entity), id<ActorPrototypeId>(actor + 100), root(cell),
            *ActorAiPackage::create(kind, waypoints) };
    }

    bool ai_packages_are_bounded_and_shape_checked()
    {
        const std::array one{ Position3(1, 2, 3) };
        std::vector<Position3> tooMany(MaximumActorWaypoints + 1, Position3(0, 0, 0));
        return ActorAiPackage::create(ActorAiPackageKind::Idle, {}).has_value()
            && !ActorAiPackage::create(static_cast<ActorAiPackageKind>(255), one)
            && !ActorAiPackage::create(ActorAiPackageKind::Idle, one)
            && ActorAiPackage::create(ActorAiPackageKind::Travel, one).has_value()
            && ActorAiPackage::create(ActorAiPackageKind::Wander, one).has_value()
            && !ActorAiPackage::create(ActorAiPackageKind::Travel, {})
            && !ActorAiPackage::create(ActorAiPackageKind::Wander, tooMany);
    }

    bool catalog_is_manifest_bound_sorted_and_findable()
    {
        const std::array unsorted{ entry(2, 12, 7), entry(1, 11, 7) };
        const auto catalog = ActorCatalog::create(testContentManifest(), unsorted);
        return catalog && catalog->contentManifestId() == testContentManifestId()
            && catalog->entries()[0].actorId == id<ActorId>(1)
            && catalog->find(id<ActorId>(2))->entityId == id<EntityId>(12)
            && catalog->find(id<ActorId>(3)) == nullptr;
    }

    bool duplicate_identity_unknown_cell_and_capacity_fail_closed()
    {
        const std::array duplicateActor{ entry(1, 11, 7), entry(1, 12, 7) };
        const std::array duplicateEntity{ entry(1, 11, 7), entry(2, 11, 7) };
        const std::array unknownCell{ entry(1, 11, 9) };
        std::vector<ActorCatalogEntry> tooMany;
        tooMany.reserve(MaximumActorCatalogEntries + 1);
        for (std::size_t index = 0; index <= MaximumActorCatalogEntries; ++index)
            tooMany.push_back(entry(index + 1, index + 5000, 7));
        const auto manifest = testContentManifest();
        return !ActorCatalog::create(manifest, duplicateActor)
            && !ActorCatalog::create(manifest, duplicateEntity)
            && !ActorCatalog::create(manifest, unknownCell)
            && !ActorCatalog::create(manifest, tooMany);
    }
}

int main()
{
    return ai_packages_are_bounded_and_shape_checked()
            && catalog_is_manifest_bound_sorted_and_findable()
            && duplicate_identity_unknown_cell_and_capacity_fail_closed()
        ? 0
        : 1;
}
