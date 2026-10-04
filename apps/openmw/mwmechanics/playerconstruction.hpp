#ifndef OPENMW_MWMECHANICS_PLAYERCONSTRUCTION_H
#define OPENMW_MWMECHANICS_PLAYERCONSTRUCTION_H

#include "stat.hpp"
#include <components/esm/refid.hpp>
#include <map>

namespace ESM { struct NPC; struct Race; struct Class; struct BodyPart; }
namespace MWWorld { class ESMStore; }

namespace MWMechanics
{
    bool isPlayablePlayerAppearance(const ESM::Race* race, const ESM::BodyPart* head,
        const ESM::BodyPart* hair, bool male);
    // Stock race selection preserves unrelated NPC flags and record fields.
    void setPlayerAppearance(ESM::NPC& body, const ESM::RefId& race, bool male,
        const ESM::RefId& head, const ESM::RefId& hair);
    // Detached multiplayer bodies inherit geometry, never template gameplay.
    ESM::NPC buildPlayerBody(const ESM::NPC& base, const ESM::RefId& race, bool male,
        const ESM::RefId& head, const ESM::RefId& hair);

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
