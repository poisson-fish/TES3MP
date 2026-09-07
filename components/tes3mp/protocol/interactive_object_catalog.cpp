#include <tes3mp/interactive_object_catalog.hpp>

#include <algorithm>
#include <map>

namespace TES3MP
{
    std::optional<InteractiveObjectCatalog> InteractiveObjectCatalog::create(
        const ContentManifest& manifest, std::span<const InteractiveObjectCatalogEntry> entries) noexcept
    try
    {
        if (entries.size() > MaximumInteractiveObjectCatalogEntries)
            return std::nullopt;

        std::vector<InteractiveObjectCatalogEntry> sorted(entries.begin(), entries.end());
        std::ranges::sort(sorted, {}, &InteractiveObjectCatalogEntry::objectId);

        std::map<CellId, std::size_t> cellCounts;

        for (std::size_t index = 0; index < sorted.size(); ++index)
        {
            const auto& entry = sorted[index];

            if (entry.objectId.value() == 0)
                return std::nullopt;

            if (index != 0 && sorted[index - 1].objectId == entry.objectId)
                return std::nullopt;

            if (!manifest.contains(entry.cell) || entry.transform.cell() != entry.cell)
                return std::nullopt;

            if (++cellCounts[entry.cell] > MaximumInteractiveObjectsPerCell)
                return std::nullopt;

            if (entry.kind == InteractiveObjectKind::TeleportDoor)
            {
                if (!entry.destination.has_value())
                    return std::nullopt;
                if (!manifest.contains(entry.destination->cell)
                    || entry.destination->transform.cell() != entry.destination->cell)
                    return std::nullopt;
            }
            else if (entry.kind == InteractiveObjectKind::StandardDoor)
            {
                if (entry.destination.has_value())
                    return std::nullopt;
            }
            else
            {
                return std::nullopt;
            }

            if (entry.lock.lockedByDefault && entry.lock.lockLevel == 0)
                return std::nullopt;

            if (entry.trap.trappedByDefault && !entry.trap.trapId.has_value())
                return std::nullopt;
        }

        return InteractiveObjectCatalog(manifest.id(), std::move(sorted));
    }
    catch (...)
    {
        return std::nullopt;
    }

    const InteractiveObjectCatalogEntry* InteractiveObjectCatalog::find(InteractiveObjectId id) const noexcept
    {
        const auto found = std::ranges::lower_bound(mEntries, id, {}, &InteractiveObjectCatalogEntry::objectId);
        return found != mEntries.end() && found->objectId == id ? &*found : nullptr;
    }
}
