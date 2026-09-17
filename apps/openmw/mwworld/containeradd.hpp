#ifndef OPENMW_MWWORLD_CONTAINERADD_H
#define OPENMW_MWWORLD_CONTAINERADD_H

#include "cellref.hpp"

namespace MWWorld
{
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
