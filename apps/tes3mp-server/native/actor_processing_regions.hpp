#ifndef TES3MP_NATIVE_ACTOR_PROCESSING_REGIONS_HPP
#define TES3MP_NATIVE_ACTOR_PROCESSING_REGIONS_HPP

#include <components/esm/refid.hpp>
#include <algorithm>
#include <cstdint>
#include <set>
#include <span>
#include <stdexcept>
#include <vector>

namespace TES3MP::Native
{
    inline constexpr size_t MaxActorProcessingCells = 256;

    // Connected exterior cells share one collision world, independent of which
    // players demand them. Interiors and disconnected exterior regions are separate.
    // Retain input order (and the old 3x3 anchor) for existing campaign bindings.
    inline std::vector<std::vector<ESM::RefId>> actorProcessingRegions(std::span<const ESM::RefId> cells)
    {
        if (cells.empty() || cells.size() > MaxActorProcessingCells)
            throw std::invalid_argument("Actor processing cell capacity exceeded");
        std::set<ESM::RefId> unique;
        for (auto cell : cells)
        {
            const auto* exterior = cell.getIf<ESM::ESM3ExteriorCellRefId>();
            if (cell.empty() || !unique.insert(cell).second || (exterior
                && (exterior->getX() < -32768 || exterior->getX() > 32767
                    || exterior->getY() < -32768 || exterior->getY() > 32767)))
                throw std::invalid_argument("Actor processing cell identity outside bounds");
        }
        const auto adjacent = [](ESM::RefId a, ESM::RefId b) {
            const auto* first = a.getIf<ESM::ESM3ExteriorCellRefId>();
            const auto* second = b.getIf<ESM::ESM3ExteriorCellRefId>();
            if (!first || !second) return false;
            const int64_t x = int64_t(first->getX()) - second->getX();
            const int64_t y = int64_t(first->getY()) - second->getY();
            return x >= -1 && x <= 1 && y >= -1 && y <= 1;
        };
        std::vector<std::vector<ESM::RefId>> regions;
        std::vector<bool> assigned(cells.size());
        for (size_t i = 0; i < cells.size(); ++i)
        {
            if (assigned[i]) continue;
            std::vector<size_t> members{i};
            assigned[i] = true;
            for (size_t at = 0; at < members.size(); ++at)
                for (size_t j = 0; j < cells.size(); ++j)
                    if (!assigned[j] && adjacent(cells[members[at]], cells[j]))
                    { assigned[j] = true; members.push_back(j); }
            std::ranges::sort(members);
            auto& region = regions.emplace_back();
            for (auto member : members) region.push_back(cells[member]);
            if (region.front().is<ESM::ESM3ExteriorCellRefId>())
            {
                const auto center = std::ranges::find_if(region, [&](auto candidate) {
                    return std::ranges::all_of(region, [&](auto cell) { return adjacent(candidate, cell); });
                });
                if (center != region.end()) std::iter_swap(region.begin(), center);
            }
        }
        return regions;
    }
}
#endif
