#ifndef OPENMW_MWPHYSICS_ACTORSHAPE_H
#define OPENMW_MWPHYSICS_ACTORSHAPE_H

#include <memory>
#include <osg/Vec3f>
#include <BulletCollision/CollisionShapes/btConvexShape.h>
#include <components/detournavigator/collisionshapetype.hpp>

namespace MWPhysics
{
    struct ActorShape
    {
        std::unique_ptr<btConvexShape> mShape;
        DetourNavigator::CollisionShapeType mType;
        bool mRotationallyInvariant;
    };

    // Stock hull selection from the resource collision box, before reference scaling.
    ActorShape makeActorShape(const osg::Vec3f& halfExtents, const osg::Vec3f& translation,
        DetourNavigator::CollisionShapeType requested);
}

#endif
