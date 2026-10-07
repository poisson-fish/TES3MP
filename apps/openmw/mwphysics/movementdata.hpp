#ifndef OPENMW_MWPHYSICS_MOVEMENTDATA_H
#define OPENMW_MWPHYSICS_MOVEMENTDATA_H

#include <limits>
#include <span>

#include <osg/Vec2f>
#include <osg/Vec3f>

class btCollisionObject;
namespace MWWorld { class Ptr; }

namespace MWPhysics
{
    class Projectile;

    // Explicit inputs/state for the shared movement solver. Engine context is
    // resolved by the caller before simulation, never by a physics worker.
    // Collision objects are borrowed for the duration of the simulation. These
    // are internal engine values, not a wire or persistence representation.
    struct ActorFrameData
    {
        osg::Vec3f mPosition;
        osg::Vec3f mInertia;
        const btCollisionObject* mStandingOn = nullptr;
        bool mIsOnGround = false;
        bool mIsOnSlope = false;
        bool mWalkingOnWater = false;
        const bool mInert = false;
        btCollisionObject* mCollisionObject = nullptr;
        const float mSwimLevel = -std::numeric_limits<float>::max();
        const float mSlowFall = 1.f;
        osg::Vec2f mRotation;
        osg::Vec3f mMovement;
        osg::Vec3f mLastStuckPosition;
        const float mWaterlevel = -std::numeric_limits<float>::max();
        const float mHalfExtentsZ = 0.f;
        float mOldHeight = 0.f;
        unsigned int mStuckFrames = 0;
        const bool mFlying = false;
        const bool mWasOnGround = false;
        const bool mIsAquatic = false;
        const bool mWaterCollision = false;
        const bool mSkipCollisionDetection = false;
        const bool mIsPlayer = false;
    };

    struct ProjectileFrameData
    {
        explicit ProjectileFrameData(Projectile& projectile);
        osg::Vec3f mPosition;
        osg::Vec3f mMovement;
        const btCollisionObject* mCaster;
        const btCollisionObject* mCollisionObject;
    };

    struct WorldFrameData
    {
        bool mIsInStorm = false;
        osg::Vec3f mStormDirection;
        float mStormWalkMultiplier = 0.f;
    };

    struct ActorReplayBody
    {
        const MWWorld::Ptr* actor = nullptr;
        osg::Vec3f position;
        float yaw = 0;
    };
    struct ActorReplayState
    {
        osg::Vec3f position, inertia, lastStuckPosition;
        unsigned stuckFrames = 0;
        bool grounded = false, slope = false, walkingOnWater = false;
        bool discontinuity = false;
        float* correctionDistance = nullptr; // Optional engine result, never persisted.
        std::span<const ActorReplayBody> bodies;
    };
    struct ActorReplayInput
    {
        osg::Vec3f axes;
        osg::Vec2f rotation;
        float walkSpeed = 0, swimSpeed = 0, flySpeed = 0, jumpSpeed = 0, airControl = 1;
        bool jump = false, sneak = false;
        float xyScale = 1;
    };
}

#endif
