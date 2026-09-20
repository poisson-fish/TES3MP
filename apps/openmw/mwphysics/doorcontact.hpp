#ifndef OPENMW_MWPHYSICS_DOORCONTACT_H
#define OPENMW_MWPHYSICS_DOORCONTACT_H

#include "collisiontype.hpp"
#include "../mwworld/doormotion.hpp"
#include <components/misc/convert.hpp>
#include <BulletCollision/CollisionDispatch/btCollisionWorld.h>

namespace MWPhysics
{
    // Shared by player sensing and detached authoritative NPC physics. The
    // caller owns both collision objects; no engine Ptr/user-pointer is read.
    class DoorContactResult final : public btCollisionWorld::ContactResultCallback
    {
        const btCollisionObject* mDoor;
        const btCollisionObject* mActor;
        osg::Vec3f mOrigin;
        float mDelta;
    public:
        bool mBlocked = false;
        DoorContactResult(const btCollisionObject* door, const btCollisionObject* actor,
            const osg::Vec3f& origin, float delta)
            : mDoor(door), mActor(actor), mOrigin(origin), mDelta(delta)
        {
            m_collisionFilterGroup = CollisionType_Door;
            m_collisionFilterMask = CollisionType_Actor;
        }
        btScalar addSingleResult(btManifoldPoint& point, const btCollisionObjectWrapper* a, int, int,
            const btCollisionObjectWrapper* b, int, int) override
        {
            const bool first = a->getCollisionObject() == mDoor;
            const auto* other = first ? b->getCollisionObject() : a->getCollisionObject();
            if (point.getDistance() <= 0 && other == mActor)
                // doorContactBlocks expects the door surface's outward normal,
                // independent of Bullet's pair/algorithm ordering.
                mBlocked |= MWWorld::doorContactBlocks(mDelta, mOrigin,
                    Misc::Convert::toOsg(first ? point.getPositionWorldOnA() : point.getPositionWorldOnB()),
                    Misc::Convert::toOsg(first ? -point.m_normalWorldOnB : point.m_normalWorldOnB));
            return 0;
        }
    };
}
#endif
