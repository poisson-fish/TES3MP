#ifndef OPENMW_MWWORLD_CONTAINERADD_H
#define OPENMW_MWWORLD_CONTAINERADD_H

#include "cellref.hpp"
#include <cmath>
#include <limits>

namespace MWWorld
{
    enum class ContainerPutCheck { Allowed, Organic, Capacity };
    inline ContainerPutCheck checkContainerPut(bool organic, float capacity, float weight, float itemWeight, int count)
    {
        if (organic) return ContainerPutCheck::Organic;
        const float total = weight + itemWeight * count;
        if (count <= 0 || !std::isfinite(capacity) || !std::isfinite(total) || itemWeight < 0 || weight < 0
            || capacity <= 0 || std::nextafterf(capacity, std::numeric_limits<float>::max()) < total)
            return ContainerPutCheck::Capacity;
        return ContainerPutCheck::Allowed;
    }
    // Shared by immediate stock add and protected transfer preparation.
    inline void normalizeContainerAddReference(CellRef& ref)
    {
        ref.setPosition({});
        ref.setOwner(ESM::RefId());
        ref.resetGlobalVariable();
        ref.setFaction(ESM::RefId());
        ref.setFactionRank(-2);
    }
}
#endif
