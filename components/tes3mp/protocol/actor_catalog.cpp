#include <tes3mp/actor_catalog.hpp>

#include <algorithm>

namespace TES3MP
{
    std::optional<ActorAiPackage> ActorAiPackage::create(
        ActorAiPackageKind kind, std::span<const Position3> waypoints) noexcept
    try
    {
        if (kind != ActorAiPackageKind::Idle && kind != ActorAiPackageKind::Travel
            && kind != ActorAiPackageKind::Wander)
            return std::nullopt;
        if (waypoints.size() > MaximumActorWaypoints)
            return std::nullopt;
        if ((kind == ActorAiPackageKind::Idle && !waypoints.empty())
            || (kind != ActorAiPackageKind::Idle && waypoints.empty()))
            return std::nullopt;
        return ActorAiPackage(kind, std::vector<Position3>(waypoints.begin(), waypoints.end()));
    }
    catch (...)
    {
        return std::nullopt;
    }

    std::optional<ActorCatalog> ActorCatalog::create(
        const ContentManifest& manifest, std::span<const ActorCatalogEntry> entries) noexcept
    try
    {
        if (entries.size() > MaximumActorCatalogEntries)
            return std::nullopt;
        std::vector<ActorCatalogEntry> sorted(entries.begin(), entries.end());
        std::ranges::sort(sorted, {}, &ActorCatalogEntry::actorId);
        std::vector<EntityId> entityIds;
        entityIds.reserve(sorted.size());
        for (std::size_t index = 0; index < sorted.size(); ++index)
        {
            if (!manifest.contains(sorted[index].initialRoot.cell()))
                return std::nullopt;
            if (index != 0 && sorted[index - 1].actorId == sorted[index].actorId)
                return std::nullopt;
            entityIds.push_back(sorted[index].entityId);
        }
        std::ranges::sort(entityIds);
        if (std::ranges::adjacent_find(entityIds) != entityIds.end())
            return std::nullopt;
        return ActorCatalog(manifest.id(), std::move(sorted));
    }
    catch (...)
    {
        return std::nullopt;
    }

    const ActorCatalogEntry* ActorCatalog::find(ActorId actorId) const noexcept
    {
        const auto found = std::ranges::lower_bound(mEntries, actorId, {}, &ActorCatalogEntry::actorId);
        return found != mEntries.end() && found->actorId == actorId ? &*found : nullptr;
    }
}
