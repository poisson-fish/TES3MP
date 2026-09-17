#ifndef OPENMW_MECHANICS_LEVELLEDLIST_H
#define OPENMW_MECHANICS_LEVELLEDLIST_H

#include <components/misc/rng.hpp>

#include <optional>

namespace ESM
{
    struct LevelledListBase;
    class RefId;
}

namespace MWWorld { class ESMStore; }

namespace MWMechanics
{

    /// @return ID of resulting item, or empty if none
    ESM::RefId getLevelledItem(
        const ESM::LevelledListBase* levItem, bool creature, Misc::Rng::Generator& prng, std::optional<int> level = {});

    ESM::RefId getLevelledItem(const ESM::LevelledListBase* levItem, bool creature,
        Misc::Rng::Generator& prng, int level, const MWWorld::ESMStore& content, unsigned depth = 0);

}

#endif
