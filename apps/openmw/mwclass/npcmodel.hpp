#ifndef OPENMW_MWCLASS_NPCMODEL_H
#define OPENMW_MWCLASS_NPCMODEL_H

#include <components/esm3/loadrace.hpp>
#include <components/vfs/pathutil.hpp>

namespace MWClass
{
    inline VFS::Path::NormalizedView npcModel(const ESM::Race& race,
        VFS::Path::NormalizedView base, VFS::Path::NormalizedView beast)
    {
        return (race.mData.mFlags & ESM::Race::Beast) ? beast : base;
    }
}

#endif
