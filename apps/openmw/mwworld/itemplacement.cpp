#include "itemplacement.hpp"

#include "../mwrender/vismask.hpp"
#include <osg/ComputeBoundsVisitor>
#include <osg/Math>
#include <cmath>

namespace MWWorld
{
    bool canPlaceItem(const ItemPlacementHit& hit)
    {
        if (!hit.mHit)
            return false;
        // Preserve the stock angle test, including the 30-degree boundary.
        return !(std::acos((hit.mNormal / hit.mNormal.length()) * osg::Vec3f(0, 0, 1))
            >= osg::DegreesToRadians(30.f));
    }

    ESM::Position cursorItemPlacement(ESM::Position actor, const ItemPlacementHit& hit)
    {
        if (hit.mHit)
            for (int i = 0; i < 3; ++i) actor.pos[i] = hit.mPoint[i];
        actor.rot[0] = 0;
        actor.rot[1] = 0;
        return actor;
    }

    ESM::Position groundItemPlacement(ESM::Position actor, const ItemPlacementRay& castRay)
    {
        actor.rot[0] = 0;
        actor.rot[1] = 0;
        osg::Vec3f origin = actor.asVec3();
        origin.z() += 20;
        const auto hit = castRay(origin, origin + osg::Vec3f(0, 0, -1) * 1000000.f);
        if (hit.mHit) actor.pos[2] = hit.mPoint.z();
        return actor;
    }

    std::optional<ESM::Position> adjustItemPlacement(ESM::Position position, osg::Node& node)
    {
        osg::ComputeBoundsVisitor computeBounds;
        computeBounds.setTraversalMask(~MWRender::Mask_ParticleSystem);
        node.accept(computeBounds);
        osg::BoundingBox bounds = computeBounds.getBoundingBox();
        if (!bounds.valid()) return std::nullopt;
        bounds.set(bounds._min - position.asVec3(), bounds._max - position.asVec3());
        position.pos[0] -= (bounds.xMin() + bounds.xMax()) / 2;
        position.pos[1] -= (bounds.yMin() + bounds.yMax()) / 2;
        position.pos[2] -= bounds.zMin();
        return position;
    }
}
