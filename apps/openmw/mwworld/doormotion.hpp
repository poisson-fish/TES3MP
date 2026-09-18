#ifndef OPENMW_MWWORLD_DOORMOTION_H
#define OPENMW_MWWORLD_DOORMOTION_H

#include "doorstate.hpp"

#include <osg/Vec3f>

namespace MWWorld
{
    // Ordinary doors rotate about their authored Z angle. Idle is a motion
    // state, not a synonym for closed: an idle door may be open or partway open.
    DoorState activatedDoorState(DoorState state, float closedAngle, float currentAngle);
    float doorSoundOffset(DoorState movement, float closedAngle, float currentAngle);

    struct DoorMotion
    {
        float mDelta;
        float mTargetAngle;
        bool mReached;
    };

    // Stock rotation proposal, before actor collision can cancel the step.
    // Idle deliberately retains World::activateDoor(Idle)'s explicit close/snap
    // behavior. A scheduler must not call this for a dormant idle door.
    DoorMotion doorMotion(DoorState state, float closedAngle, float currentAngle, float duration);
    bool doorContactBlocks(float delta, const osg::Vec3f& doorPosition,
        const osg::Vec3f& contactPoint, const osg::Vec3f& contactNormal);
}

#endif
