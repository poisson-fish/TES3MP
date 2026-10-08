#ifndef OPENMW_MWMECHANICS_DISPOSITION_H
#define OPENMW_MWMECHANICS_DISPOSITION_H

#include <functional>

namespace ESM { struct NPC; class RefId; struct GameSetting; }
namespace MWWorld { template<class T> class Store; }

namespace MWMechanics
{
    class NpcStats;
    // Stock disposition with explicit actors, detached stats and content. The
    // stock caller supplies dialogue overrides; scoped callers supply owned reactions.
    int derivedDisposition(const ESM::NPC& speaker, const NpcStats& speakerStats,
        const ESM::NPC& player, const NpcStats& playerStats, const MWWorld::Store<ESM::GameSetting>&,
        const std::function<int(const ESM::RefId&, const ESM::RefId&)>& factionReaction, bool clamp = true);
}

#endif
