#ifndef OPENMW_MWMECHANICS_PLAYERCONSTRUCTION_H
#define OPENMW_MWMECHANICS_PLAYERCONSTRUCTION_H

#include "stat.hpp"
#include <components/esm/refid.hpp>
#include <map>

namespace ESM { struct NPC; struct Race; struct Class; }
namespace MWWorld { class ESMStore; }

namespace MWMechanics
{
    struct PlayerConstructionStats
    {
        std::map<ESM::RefId, AttributeValue> mAttributes;
        std::map<ESM::RefId, SkillValue> mSkills;
    };

    // Shared chargen base calculation. Explicit records/store, without a player,
    // inventory, active effects or presentation. Null choices preserve stock
    // pre-selection behavior; installation belongs to the caller.
    PlayerConstructionStats buildPlayerStats(const ESM::NPC& base, const ESM::Race* race,
        const ESM::Class* characterClass, const MWWorld::ESMStore& store);
}

#endif
