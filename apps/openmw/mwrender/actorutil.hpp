#ifndef OPENMW_APPS_OPENMW_MWRENDER_ACTORUTIL_H
#define OPENMW_APPS_OPENMW_MWRENDER_ACTORUTIL_H

#include <components/vfs/pathutil.hpp>

#include <string>

namespace ESM
{
    struct Position;
}

namespace osg
{
    class Quat;
}

namespace MWRender
{
    // Actor roots stay upright; look pitch belongs to the camera/animation.
    osg::Quat makeActorRootRotation(const ESM::Position& position);

    const std::string& getActorSkeleton(bool firstPerson, bool female, bool beast, bool werewolf);
    bool isDefaultActorSkeleton(VFS::Path::NormalizedView model);
    std::string addSuffixBeforeExtension(const std::string& filename, const std::string& suffix);
}

#endif
